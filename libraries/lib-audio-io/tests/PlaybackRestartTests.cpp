/*  SPDX-License-Identifier: GPL-2.0-or-later  */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  @file PlaybackRestartTests.cpp

  Tests for restarting playback after tracks are added (e.g. by drag & drop)

**********************************************************************/
#include "PlaybackRestart.h"

#include <catch2/catch.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
class MockHost final : public PlaybackRestartHost
{
public:
   bool playing = false;
   bool paused = false;
   bool looping = false;
   double time = 0.0;
   std::vector<std::string> calls;
   double playedFrom = -1.0;
   bool playedLooping = false;

   bool Playing() const override { return playing; }
   bool Paused() const override { return paused; }
   bool Looping() const override { return looping; }
   double StreamTime() const override { return time; }
   void Stop() override { calls.push_back("stop"); playing = false; }
   void Play(double t, bool loop) override
   {
      calls.push_back("play");
      playedFrom = t;
      playedLooping = loop;
      playing = true;
   }
};
} // namespace

TEST_CASE("RestartPlaybackAfter: restarts normal playback from current time")
{
   MockHost host;
   host.playing = true;
   host.time = 3.5;
   bool modified = false;

   const bool result = RestartPlaybackAfter(host, [&] {
      modified = true;
      host.time = 99.0; // time moved on during import must not be used
      return true;
   });

   REQUIRE(result);
   REQUIRE(modified);
   REQUIRE(host.calls == std::vector<std::string>{ "stop", "play" });
   REQUIRE(host.playedFrom == 3.5);
   REQUIRE_FALSE(host.playedLooping);
}

TEST_CASE("RestartPlaybackAfter: keeps loop mode")
{
   MockHost host;
   host.playing = true;
   host.looping = true;
   host.time = 1.25;

   REQUIRE(RestartPlaybackAfter(host, [] { return true; }));
   REQUIRE(host.calls == std::vector<std::string>{ "stop", "play" });
   REQUIRE(host.playedFrom == 1.25);
   REQUIRE(host.playedLooping);
}

TEST_CASE("RestartPlaybackAfter: does nothing when not playing")
{
   MockHost host;
   bool modified = false;

   REQUIRE(RestartPlaybackAfter(host, [&] { modified = true; return true; }));
   REQUIRE(modified);
   REQUIRE(host.calls.empty());
}

TEST_CASE("RestartPlaybackAfter: does not restart while paused")
{
   MockHost host;
   host.playing = true;
   host.paused = true;

   REQUIRE(RestartPlaybackAfter(host, [] { return true; }));
   REQUIRE(host.calls.empty());
}

TEST_CASE("RestartPlaybackAfter: failed modification leaves playback alone")
{
   MockHost host;
   host.playing = true;
   host.time = 2.0;

   REQUIRE_FALSE(RestartPlaybackAfter(host, [] { return false; }));
   REQUIRE(host.calls.empty());
   REQUIRE(host.playing);
}

TEST_CASE("RestartPlaybackAfter: no restart if playback ended meanwhile")
{
   MockHost host;
   host.playing = true;

   REQUIRE(RestartPlaybackAfter(host, [&] {
      host.playing = false; // clip finished while importing
      return true;
   }));
   REQUIRE(host.calls.empty());
}

TEST_CASE("RestartPlaybackAfter: propagates exceptions without restarting")
{
   MockHost host;
   host.playing = true;

   REQUIRE_THROWS_AS(
      RestartPlaybackAfter(host, []() -> bool { throw std::runtime_error{ "x" }; }),
      std::runtime_error);
   REQUIRE(host.calls.empty());
}

TEST_CASE("RestartPlaybackAfter: decision truth table")
{
   // Every combination of the inputs the restart decision depends on.
   // Restart happens exactly when the modification succeeded, playback was
   // running (and not paused) before it, and is still running after it.
   for (int bits = 0; bits < 32; ++bits) {
      const bool playingBefore = bits & 1;
      const bool pausedBefore = bits & 2;
      const bool playingAfter = bits & 4;
      const bool success = bits & 8;
      const bool looping = bits & 16;

      INFO(
         "playingBefore=" << playingBefore << " pausedBefore=" << pausedBefore
                          << " playingAfter=" << playingAfter
                          << " success=" << success << " looping=" << looping);

      MockHost host;
      host.playing = playingBefore;
      host.paused = pausedBefore;
      host.looping = looping;
      host.time = 4.25;

      int modifyCount = 0;
      const bool result = RestartPlaybackAfter(host, [&] {
         ++modifyCount;
         host.calls.push_back("modify");
         host.playing = playingAfter;
         return success;
      });

      const bool expectRestart =
         success && playingBefore && !pausedBefore && playingAfter;

      REQUIRE(result == success);
      REQUIRE(modifyCount == 1);
      if (expectRestart) {
         REQUIRE(
            host.calls ==
            std::vector<std::string>{ "modify", "stop", "play" });
         REQUIRE(host.playedFrom == 4.25);
         REQUIRE(host.playedLooping == looping);
      }
      else
         REQUIRE(host.calls == std::vector<std::string>{ "modify" });
   }
}

TEST_CASE("RestartPlaybackAfter: pausing during the modification is not noticed")
{
   // Existing behaviour, recorded so that a change to it is deliberate:
   // only the state before the modification decides, so playback that was
   // running and got paused meanwhile is restarted.
   MockHost host;
   host.playing = true;
   host.time = 1.5;

   REQUIRE(RestartPlaybackAfter(host, [&] {
      host.paused = true;
      return true;
   }));
   REQUIRE(host.calls == std::vector<std::string>{ "stop", "play" });
   REQUIRE(host.playedFrom == 1.5);
}

namespace {
// Counts how often playback state is queried
class CountingHost final : public PlaybackRestartHost
{
public:
   bool playing = false;
   mutable int playingQueries = 0;

   bool Playing() const override { ++playingQueries; return playing; }
   bool Paused() const override { return false; }
   bool Looping() const override { return false; }
   double StreamTime() const override { return 0.0; }
   void Stop() override {}
   void Play(double, bool) override {}
};
} // namespace

TEST_CASE("ShouldRestart: truth table")
{
   for (int bits = 0; bits < 8; ++bits) {
      const bool wasPlaying = bits & 1;
      const bool success = bits & 2;
      const bool playingNow = bits & 4;

      INFO(
         "wasPlaying=" << wasPlaying << " success=" << success
                       << " playingNow=" << playingNow);

      CountingHost host;
      host.playing = playingNow;
      const PlaybackSnapshot snapshot { wasPlaying, false, 0.0 };

      REQUIRE(
         ShouldRestart(snapshot, success, host) ==
         (wasPlaying && success && playingNow));
   }
}

TEST_CASE("ShouldRestart: queries the host only when it matters")
{
   CountingHost host;
   host.playing = true;

   REQUIRE_FALSE(ShouldRestart({ true, false, 0.0 }, false, host));
   REQUIRE_FALSE(ShouldRestart({ false, false, 0.0 }, true, host));
   REQUIRE(host.playingQueries == 0);

   REQUIRE(ShouldRestart({ true, false, 0.0 }, true, host));
   REQUIRE(host.playingQueries == 1);
}

TEST_CASE("CapturePlaybackSnapshot: reads the state before the modification")
{
   MockHost host;
   host.playing = true;
   host.looping = true;
   host.time = 6.5;

   const auto playing = CapturePlaybackSnapshot(host);
   REQUIRE(playing.wasPlaying);
   REQUIRE(playing.wasLooping);
   REQUIRE(playing.resumeTime == 6.5);

   host.paused = true;
   const auto paused = CapturePlaybackSnapshot(host);
   REQUIRE_FALSE(paused.wasPlaying);
   REQUIRE(paused.resumeTime == 0.0);

   MockHost stopped;
   stopped.time = 9.0;
   const auto notPlaying = CapturePlaybackSnapshot(stopped);
   REQUIRE_FALSE(notPlaying.wasPlaying);
   REQUIRE_FALSE(notPlaying.wasLooping);
   REQUIRE(notPlaying.resumeTime == 0.0);
}
