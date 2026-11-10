/*!********************************************************************
*
 Audacity: A Digital Audio Editor

 WaveClipAdjustBorderHandle.cpp

 Vitaly Sverchinsky

 **********************************************************************/

#include "WaveClipAdjustBorderHandle.h"
#include "ProjectAudioIO.h"
#include "RefreshCode.h"

#include <wx/event.h>

#include <algorithm>
#include <functional>
#include <limits>

#include "ClipParameters.h"
#include "../../../../TrackArt.h"
#include "../../../../TrackArtist.h"
#include "Snap.h"
#include "../../../../TrackPanelDrawingContext.h"
#include "../../../../../images/Cursors.h"
#include "WaveClip.h"
#include "WaveTrack.h"
#include "WaveChannelView.h"
#include "HitTestResult.h"
#include "TrackPanelMouseEvent.h"
#include "ViewInfo.h"
#include "ProjectHistory.h"
#include "UndoManager.h"
#include "WaveClipUIUtilities.h"

namespace {

//! Distance (in pixels) from a clip border within which it can be grabbed
constexpr int BoundaryThreshold = 5;
//! Wider threshold used to find candidates when clips may be adjacent
constexpr int AdjacentBoundaryThreshold = BoundaryThreshold * 2;
//! Clips narrower than this (in pixels) can not be adjusted
constexpr int MinAdjustableClipWidth = 10;
//! Only the top part of the track's rectangle reacts to border hits
constexpr double HitAreaHeightFraction = 0.3;

using ClipInterval = WaveTrack::Interval;
using AdjustHandler = std::function<void(ClipInterval&, double)>;

AdjustHandler MakeAdjustHandler(bool isStretchMode, bool adjustLeftBorder)
{
   if (isStretchMode)
      return adjustLeftBorder
         ? AdjustHandler{ [](ClipInterval& i, double t) { i.StretchLeftTo(t); } }
         : AdjustHandler{ [](ClipInterval& i, double t) { i.StretchRightTo(t); } };
   return adjustLeftBorder
      ? AdjustHandler{ [](ClipInterval& i, double t) { i.TrimLeftTo(t); } }
      : AdjustHandler{ [](ClipInterval& i, double t) { i.TrimRightTo(t); } };
}

//! Lowest time to which the border may be moved
double GetLeftAdjustLimit(const ClipInterval& interval,
                          const WaveTrack& track,
                          bool adjustingLeftBorder,
                          bool isStretchMode)
{
   if (!adjustingLeftBorder)
      return std::min(
         interval.GetSequenceEndTime(),
         interval.Start() + 1.0 / track.GetRate()
      );

   const auto prevInterval = track.GetNextInterval(interval, PlaybackDirection::backward);
   if (isStretchMode)
      return prevInterval ? prevInterval->End() :
                            std::numeric_limits<double>::lowest();
   if (prevInterval)
      return std::max(interval.GetSequenceStartTime(),
                prevInterval->End());
   return interval.GetSequenceStartTime();
}

//! Highest time to which the border may be moved
double GetRightAdjustLimit(
   const ClipInterval& interval, const WaveTrack& track, bool adjustingLeftBorder,
   bool isStretchMode)
{
   if (adjustingLeftBorder)
      return std::max(
         interval.GetSequenceStartTime(),
         interval.End() - 1.0 / track.GetRate()
      );

   const auto nextInterval = track.GetNextInterval(interval, PlaybackDirection::forward);
   if (isStretchMode)
      return nextInterval ? nextInterval->Start() :
                            std::numeric_limits<double>::max();

   if (nextInterval)
      return std::min(interval.GetSequenceEndTime(),
                      nextInterval->Start());
   return interval.GetSequenceEndTime();
}

void PushTrimUndoState(AudacityProject& project, bool leftBorder, double dt)
{
   auto& history = ProjectHistory::Get(project);
   if (leftBorder)
      history.PushState(
         /*i18n-hint: This is about trimming a clip, a length in seconds like "2.4 seconds" is shown*/
         XO("Adjust left trim by %.02f seconds").Format(dt),
         /*i18n-hint: This is about trimming a clip, a length in seconds like "2.4s" is shown*/
         XO("Trim by %.02fs").Format(dt));
   else
      history.PushState(
         /*i18n-hint: This is about trimming a clip, a length in seconds like "2.4 seconds" is shown*/
         XO("Adjust right trim by %.02f seconds").Format(dt),
         /*i18n-hint: This is about trimming a clip, a length in seconds like "2.4s" is shown*/
         XO("Trim by %.02fs").Format(dt));
}

HitTestPreview MakeHitPreview(bool isStretchMode, bool unsafe, bool isLeftBorder)
{
   static auto disabledCursor =
      MakeCursor(wxCURSOR_NO_ENTRY, DisabledCursorXpm, 16, 16);
   static auto trimCursorLeft =
      MakeCursor(wxCURSOR_SIZEWE, ClipTrimLeftXpm, 16, 16);
   static auto trimCursorRight =
      MakeCursor(wxCURSOR_SIZEWE, ClipTrimRightXpm, 16, 16);
   static auto stretchCursorLeft =
      MakeCursor(wxCURSOR_SIZEWE, ClipStretchLeftXpm, 16, 16);
   static auto stretchCursorRight =
      MakeCursor(wxCURSOR_SIZEWE, ClipStretchRightXpm, 16, 16);

   const auto message = isStretchMode
      ? XO("Click and drag to stretch clip")
      : XO("Click and drag to move clip boundary in time");

   auto& cursor = isStretchMode
      ? (isLeftBorder ? stretchCursorLeft : stretchCursorRight)
      : (isLeftBorder ? trimCursorLeft : trimCursorRight);

   return { message, unsafe ? &*disabledCursor : &*cursor };
}

} // namespace

//Different policies implement different adjustment scenarios
class WaveClipAdjustBorderHandle::AdjustPolicy
{
public:
   virtual ~AdjustPolicy();

   virtual bool Init(const TrackPanelMouseEvent& event) = 0;
   virtual UIHandle::Result Drag(const TrackPanelMouseEvent& event, AudacityProject& project) = 0;
   virtual void Finish(AudacityProject& project) = 0;
   virtual void Cancel() = 0;

   virtual void Draw(
      TrackPanelDrawingContext &context,
      const wxRect &rect,
      unsigned iPass);

   virtual wxRect DrawingArea(
      TrackPanelDrawingContext&,
      const wxRect &rect,
      const wxRect &panelRect,
      unsigned iPass);
};

WaveClipAdjustBorderHandle::AdjustPolicy::~AdjustPolicy() = default;

void WaveClipAdjustBorderHandle::AdjustPolicy::Draw(TrackPanelDrawingContext&, const wxRect&, unsigned) { }

wxRect WaveClipAdjustBorderHandle::AdjustPolicy::DrawingArea(TrackPanelDrawingContext&, const wxRect& rect, const wxRect&, unsigned)
{
   return rect;
}

namespace {

//! Trims or stretches one border of a clip, with snapping
class AdjustClipBorder final : public WaveClipAdjustBorderHandle::AdjustPolicy
{
   std::shared_ptr<WaveTrack> mTrack;
   std::shared_ptr<ClipInterval> mInterval;
   int mDragStartX{ };
   const bool mAdjustingLeftBorder;
   const bool mIsStretchMode;
   const double mInitialBorderPosition;
   double mBorderPosition;
   const std::pair<double, double> mRange;
   AdjustHandler mAdjustHandler;

   std::unique_ptr<SnapManager> mSnapManager;
   SnapResults mSnap;

   void TrimTo(double t)
   {
      mBorderPosition = std::clamp(t, mRange.first, mRange.second);
      mAdjustHandler(*mInterval, mBorderPosition);
   }

   //Search for a good snap points among all tracks except
   //one to which moving interval belongs to
   static SnapPointArray FindSnapPoints(
      const WaveTrack* currentTrack,
      const std::pair<double, double> range)
   {
      SnapPointArray result;

      auto addSnapPoint = [&](double t, const Track* track)
      {
         if (t > range.second || t < range.first)
            return;

         const bool known = std::any_of(result.begin(), result.end(),
            [t](const auto& snapPoint) { return snapPoint.t == t; });
         if (!known)
            result.emplace_back(t, track);
      };

      const auto trackList = currentTrack->GetOwner();
      if (!trackList)
         return result;

      for (const auto track : as_const(*trackList))
      {
         //skip track that interval belongs to
         if (track == currentTrack)
            continue;

         for (const auto &interval : track->Intervals()) {
            addSnapPoint(interval->Start(), track);
            if (interval->Start() != interval->End())
               addSnapPoint(interval->End(), track);
         }
      }
      return result;
   }

   //! Time under the mouse, moved by whole samples and kept within the range
   double GetDraggedTime(
      const TrackPanelMouseEvent& event, const AudacityProject& project) const
   {
      const auto dx = event.event.GetX() - mDragStartX;
      const auto& viewInfo = ViewInfo::Get(project);

      const auto eventT = viewInfo.PositionToTime(
         viewInfo.TimeToPosition(mInitialBorderPosition, event.rect.x) + dx,
         event.rect.x
      );

      const auto offset = sampleCount(floor((eventT - mInitialBorderPosition) * mTrack->GetRate())).as_double()
         / mTrack->GetRate();
      return std::clamp(mInitialBorderPosition + offset, mRange.first, mRange.second);
   }

   void PushUndoState(AudacityProject& project, double dt) const
   {
      if (mIsStretchMode)
         WaveClipUIUtilities::PushClipSpeedChangedUndoState(
            project, 100.0 / mInterval->GetStretchRatio());
      else
         PushTrimUndoState(project, mAdjustingLeftBorder, dt);
   }

public:
   AdjustClipBorder(AdjustHandler adjustHandler,
                    std::shared_ptr<WaveTrack> track,
                    std::shared_ptr<ClipInterval> interval,
                    bool adjustLeftBorder,
                    bool isStretchMode,
                    const ZoomInfo& zoomInfo)
      : mTrack { std::move(track) }
      , mInterval { std::move(interval) }
      , mAdjustingLeftBorder { adjustLeftBorder }
      , mIsStretchMode { isStretchMode }
      , mInitialBorderPosition { adjustLeftBorder ? mInterval->Start() :
                                             mInterval->End() }
      , mBorderPosition { mInitialBorderPosition }
      , mRange { GetLeftAdjustLimit( *mInterval, *mTrack, adjustLeftBorder, isStretchMode),
                 GetRightAdjustLimit(*mInterval, *mTrack, adjustLeftBorder, isStretchMode) }
      , mAdjustHandler { std::move(adjustHandler) }
   {
      assert(mRange.first <= mRange.second);
      if (const auto trackList = mTrack->GetOwner())
      {
         mSnapManager = std::make_unique<SnapManager>(
            *trackList->GetOwner(),
            FindSnapPoints(mTrack.get(), mRange),
            zoomInfo);
      }
   }

   bool Init(const TrackPanelMouseEvent& event) override
   {
      if (!event.event.LeftDown())
         return false;
      mDragStartX = event.event.GetX();
      return true;
   }

   UIHandle::Result Drag(const TrackPanelMouseEvent& event, AudacityProject& project) override
   {
      const auto t = GetDraggedTime(event, project);
      const auto wasSnapped = mSnap.Snapped();
      if (mSnapManager)
         mSnap = mSnapManager->Snap(mTrack.get(), t, !mAdjustingLeftBorder);
      if (mSnap.Snapped())
      {
         if (mSnap.outTime >= mRange.first && mSnap.outTime <= mRange.second)
         {
            //Make sure that outTime belongs to the adjustment range after snapping
            TrimTo(mSnap.outTime);
            return RefreshCode::RefreshAll;
         }
         mSnap = {};
      }
      TrimTo(t);
      //If there was a snap line, make sure it is removed
      //from the screen by redrawing whole TrackPanel
      return wasSnapped ? RefreshCode::RefreshAll : RefreshCode::RefreshCell;
   }

   void Finish(AudacityProject& project) override
   {
      const auto dt = std::abs(mInitialBorderPosition - mBorderPosition);
      if (dt != 0)
         PushUndoState(project, dt);
   }

   void Cancel() override
   {
      TrimTo(mInitialBorderPosition);
   }

   void Draw(TrackPanelDrawingContext& context, const wxRect& rect, unsigned iPass) override
   {
      if (iPass == TrackArtist::PassSnapping && mSnap.Snapped())
         TrackArt::DrawSnapLines(&context.dc, rect.x + mSnap.outCoord, -1);
   }

   wxRect DrawingArea(TrackPanelDrawingContext&, const wxRect& rect, const wxRect& panelRect, unsigned iPass) override
   {
      if (iPass == TrackArtist::PassSnapping)
         return TrackPanelDrawable::MaximizeHeight(rect, panelRect);
      return rect;
   }
};

//! Clips whose borders are close to the mouse position
struct BorderCandidates {
   //! Clip whose right border is near the mouse
   std::shared_ptr<ClipInterval> endingClip;
   //! Clip whose left border is near the mouse
   std::shared_ptr<ClipInterval> startingClip;
};

struct BorderChoice {
   std::shared_ptr<ClipInterval> interval;
   bool leftBorder{ false };
};

//Test left and right boundaries of each clip
//to determine which kind of adjustment is
//more appropriate
BorderCandidates FindBorderCandidates(
   WaveTrack& waveTrack, const ZoomInfo& zoomInfo,
   const wxRect& rect, int px)
{
   BorderCandidates result;
   for (const auto &interval : waveTrack.Intervals()) {
      if (!WaveChannelView::ClipDetailsVisible(*interval, zoomInfo, rect))
         continue;

      const auto clipRect = ClipParameters::GetClipRect(*interval, zoomInfo, rect);

      if (clipRect.GetWidth() < MinAdjustableClipWidth)
         continue;

      if (std::abs(px - clipRect.GetLeft()) <= AdjacentBoundaryThreshold)
         result.startingClip = interval;
      else if (std::abs(px - clipRect.GetRight()) <= AdjacentBoundaryThreshold)
         result.endingClip = interval;
   }
   return result;
}

//! Decide which clip border (if any) the mouse is going to adjust
BorderChoice ChooseBorder(
   const BorderCandidates& candidates, const ZoomInfo& zoomInfo,
   const wxRect& rect, int px)
{
   const auto& endingClip = candidates.endingClip;
   const auto& startingClip = candidates.startingClip;

   if (endingClip && startingClip)
   {
      //between adjacent clips
      const bool nearerToEndingClip =
         ClipParameters::GetClipRect(*endingClip, zoomInfo, rect).GetRight() > px;
      return nearerToEndingClip
         ? BorderChoice{ endingClip, false }
         : BorderChoice{ startingClip, true };
   }

   const auto& interval = endingClip ? endingClip : startingClip;
   if (!interval)
      return {};

   //single clip case, determine the border,
   //hit testing area differs from one
   //used for general case
   const auto clipRect = ClipParameters::GetClipRect(*interval, zoomInfo, rect);
   if (std::abs(px - clipRect.GetLeft()) <= BoundaryThreshold)
      return { interval, true };
   if (std::abs(px - clipRect.GetRight()) <= BoundaryThreshold)
      return { interval, false };
   return {};
}

} // namespace

WaveClipAdjustBorderHandle::WaveClipAdjustBorderHandle(
   std::unique_ptr<AdjustPolicy> adjustPolicy,
   std::shared_ptr<const WaveTrack> pTrack,
   bool stretchMode,
   bool leftBorder)
   : mAdjustPolicy{ std::move(adjustPolicy) }
   , mpTrack{ std::move(pTrack) }
   , mIsStretchMode{stretchMode}
   , mIsLeftBorder{leftBorder}
{
}

WaveClipAdjustBorderHandle::~WaveClipAdjustBorderHandle() = default;

WaveClipAdjustBorderHandle::WaveClipAdjustBorderHandle(WaveClipAdjustBorderHandle&&) noexcept = default;

WaveClipAdjustBorderHandle& WaveClipAdjustBorderHandle::operator=(WaveClipAdjustBorderHandle&&) noexcept = default;

std::shared_ptr<const Track> WaveClipAdjustBorderHandle::FindTrack() const
{
   return mpTrack;
}

UIHandlePtr WaveClipAdjustBorderHandle::HitAnywhere(
   std::weak_ptr<WaveClipAdjustBorderHandle>& holder,
   const std::shared_ptr<WaveTrack>& waveTrack,
   const AudacityProject* pProject,
   const TrackPanelMouseState& state)
{
   const auto& rect = state.rect;
   const auto px = state.state.m_x;
   const auto& zoomInfo = ViewInfo::Get(*pProject);

   const auto candidates = FindBorderCandidates(*waveTrack, zoomInfo, rect, px);
   const auto choice = ChooseBorder(candidates, zoomInfo, rect, px);
   if (!choice.interval)
      return {};

   const auto isStretchMode = state.state.AltDown();
   auto policy = std::make_unique<AdjustClipBorder>(
      MakeAdjustHandler(isStretchMode, choice.leftBorder),
      waveTrack, choice.interval, choice.leftBorder, isStretchMode, zoomInfo);

   return AssignUIHandlePtr(
      holder,
      std::make_shared<WaveClipAdjustBorderHandle>(
         std::move(policy), waveTrack, isStretchMode, choice.leftBorder));
}

UIHandlePtr WaveClipAdjustBorderHandle::HitTest(std::weak_ptr<WaveClipAdjustBorderHandle>& holder,
    WaveChannelView& view, const AudacityProject* pProject,
    const TrackPanelMouseState& state)
{
   auto waveChannel = view.FindWaveChannel();
   // For multichannel tracks, show adjustment handle only for the topmost
   // channel
   if (waveChannel->GetChannelIndex() != 0)
      return {};

   const auto& rect = state.rect;
   const auto py = state.state.m_y;
   const auto hitAreaBottom =
      rect.GetTop() + static_cast<int>(rect.GetHeight() * HitAreaHeightFraction);

   if (py < rect.GetTop() || py > hitAreaBottom)
      return {};

   return HitAnywhere(holder,
      waveChannel->GetTrack().SharedPointer<WaveTrack>(),
      pProject, state);
}

HitTestPreview WaveClipAdjustBorderHandle::Preview(const TrackPanelMouseState&, AudacityProject* pProject)
{
   const bool unsafe = ProjectAudioIO::Get(*pProject).IsAudioActive();
   return MakeHitPreview(mIsStretchMode, unsafe, mIsLeftBorder);
}

UIHandle::Result WaveClipAdjustBorderHandle::Click
(const TrackPanelMouseEvent& event, AudacityProject* pProject)
{
   if (!ProjectAudioIO::Get(*pProject).IsAudioActive() &&
       mAdjustPolicy->Init(event))
      return RefreshCode::RefreshNone;
   return RefreshCode::Cancelled;
}

UIHandle::Result WaveClipAdjustBorderHandle::Drag
(const TrackPanelMouseEvent& event, AudacityProject* project)
{
   return mAdjustPolicy->Drag(event, *project);
}

UIHandle::Result WaveClipAdjustBorderHandle::Release
(const TrackPanelMouseEvent&, AudacityProject* project, wxWindow*)
{
   mAdjustPolicy->Finish(*project);
   return RefreshCode::RefreshAll;
}

UIHandle::Result WaveClipAdjustBorderHandle::Cancel(AudacityProject*)
{
   mAdjustPolicy->Cancel();
   return RefreshCode::RefreshAll;
}

void WaveClipAdjustBorderHandle::Draw(TrackPanelDrawingContext& context, const wxRect& rect, unsigned iPass)
{
   mAdjustPolicy->Draw(context, rect, iPass);
}

wxRect WaveClipAdjustBorderHandle::DrawingArea(TrackPanelDrawingContext& context, const wxRect& rect,
   const wxRect& panelRect, unsigned iPass)
{
   return mAdjustPolicy->DrawingArea(context, rect, panelRect, iPass);
}
