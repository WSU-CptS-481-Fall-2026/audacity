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

//! Run `modifyTracks`; if it succeeded and playback was running (not paused),
//! restart playback from where it was, in the same (looped or not) mode.
/*!
 @return the result of `modifyTracks`
 */
inline bool RestartPlaybackAfter(
   PlaybackRestartHost& host, const std::function<bool()>& modifyTracks)
{
   const bool wasPlaying = host.Playing() && !host.Paused();
   const bool wasLooping = host.Looping();
   const double resumeTime = wasPlaying ? host.StreamTime() : 0.0;

   const bool success = modifyTracks();

   // Playback may have ended while the tracks were being modified
   if (success && wasPlaying && host.Playing()) {
      host.Stop();
      host.Play(resumeTime, wasLooping);
   }
   return success;
}
