/*!********************************************************************

 Audacity: A Digital Audio Editor

 WaveClipBorderHit.h

 Pure (wx-free) hit-testing rules used by WaveClipAdjustBorderHandle to
 decide which clip border, if any, the mouse is pointing at.

 Extracted from WaveClipAdjustBorderHandle::HitAnywhere so that the
 decision logic can be unit tested without a TrackPanel.

 **********************************************************************/

#pragma once

#include <cstddef>
#include <cstdlib>
#include <optional>
#include <vector>

namespace WaveClipBorderHit {

//! Distance in pixels from a clip border within which a click selects it
constexpr int BoundaryThreshold = 5;

//! Wider threshold used to find candidate borders before choosing between
//! two neighbouring clips
constexpr int NeighbourBoundaryThreshold = BoundaryThreshold * 2;

//! Clips narrower than this (in pixels) can not be adjusted
constexpr int MinClipWidth = 10;

//! Horizontal pixel extent of a clip, both ends inclusive (like wxRect)
struct ClipEdges {
   int left;
   int right;

   constexpr int Width() const { return right - left + 1; }
};

//! The clip and border that an adjustment would apply to
struct Target {
   std::size_t clipIndex;
   bool adjustLeftBorder;

   constexpr bool operator==(const Target& other) const
   {
      return clipIndex == other.clipIndex &&
             adjustLeftBorder == other.adjustLeftBorder;
   }
};

//! Decide which clip border (if any) the pointer at x position `px` targets.
/*!
 Borders of two neighbouring clips may be closer to each other than the
 threshold, so the search area is doubled to find candidates, and then the
 final border is chosen:
  - when two candidates exist (a gap between adjacent clips), the border of
    the clip that contains `px` wins; otherwise the right clip's left border;
  - when only one candidate exists, the normal (single) threshold applies.
 If several clips qualify as the same kind of candidate, the last one wins.

 @param clips visible extents of the clips, in track order
 @param px mouse x position, in the same coordinate space as `clips`
 */
inline std::optional<Target>
FindAdjustTarget(const std::vector<ClipEdges>& clips, int px)
{
   std::optional<std::size_t> leftClip;  // candidate whose right border is near
   std::optional<std::size_t> rightClip; // candidate whose left border is near

   for (std::size_t i = 0; i < clips.size(); ++i) {
      const auto& clip = clips[i];

      if (clip.Width() < MinClipWidth)
         continue;

      if (std::abs(px - clip.left) <= NeighbourBoundaryThreshold)
         rightClip = i;
      else if (std::abs(px - clip.right) <= NeighbourBoundaryThreshold)
         leftClip = i;
   }

   if (leftClip && rightClip) {
      // between adjacent clips
      if (clips[*leftClip].right > px)
         return Target { *leftClip, false };
      return Target { *rightClip, true };
   }

   const auto candidate = leftClip ? leftClip : rightClip;
   if (!candidate)
      return std::nullopt;

   // single clip case: the hit area is narrower than in the general case
   const auto& clip = clips[*candidate];
   if (std::abs(px - clip.left) <= BoundaryThreshold)
      return Target { *candidate, true };
   if (std::abs(px - clip.right) <= BoundaryThreshold)
      return Target { *candidate, false };
   return std::nullopt;
}

} // namespace WaveClipBorderHit
