/**********************************************************************

  Audacity: A Digital Audio Editor

  @file PlaybackRestart.h
  @brief Restart running playback after the track list changed

  Playback works on a snapshot of the tracks taken when the stream starts, so
  tracks added during playback are not heard until playback is restarted.

**********************************************************************/
#pragma once

#include <functional>

//! Minimal view of the playback engine needed to restart playback
class PlaybackRestartHost
{
public:
   virtual ~PlaybackRestartHost() = default;
   virtual bool Playing() const = 0;
   virtual bool Paused() const = 0;
   virtual bool Looping() const = 0;
   virtual double StreamTime() const = 0;
   virtual void Stop() = 0;
   virtual void Play(double startTime, bool looping) = 0;
};

//! State of playback, taken before the tracks are modified
struct PlaybackSnapshot
{
   bool wasPlaying; //!< playing and not paused
   bool wasLooping;
   double resumeTime; //!< stream time to restart from; 0 if not playing
};

//! Read the playback state that decides whether and where to restart
inline PlaybackSnapshot CapturePlaybackSnapshot(const PlaybackRestartHost& host)
{
   const bool wasPlaying = host.Playing() && !host.Paused();
   const bool wasLooping = host.Looping();
   const double resumeTime = wasPlaying ? host.StreamTime() : 0.0;
   return { wasPlaying, wasLooping, resumeTime };
}

//! Whether playback must be restarted after the tracks were modified
/*!
 True only if the modification succeeded, playback was running (and not
 paused) before it, and is still running now. Playback may have ended while
 the tracks were being modified. `host.Playing()` is queried only if the
 first two conditions hold.
 */
inline bool ShouldRestart(
   const PlaybackSnapshot& snapshot, bool modificationSucceeded,
   const PlaybackRestartHost& host)
{
   return modificationSucceeded && snapshot.wasPlaying && host.Playing();
}

//! Run `modifyTracks`; if it succeeded and playback was running (not paused),
//! restart playback from where it was, in the same (looped or not) mode.
/*!
 @return the result of `modifyTracks`
 */
inline bool RestartPlaybackAfter(
   PlaybackRestartHost& host, const std::function<bool()>& modifyTracks)
{
   const auto snapshot = CapturePlaybackSnapshot(host);

   const bool success = modifyTracks();

   if (ShouldRestart(snapshot, success, host)) {
      host.Stop();
      host.Play(snapshot.resumeTime, snapshot.wasLooping);
   }
   return success;
}
