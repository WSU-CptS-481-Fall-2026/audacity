/**********************************************************************

  Audacity: A Digital Audio Editor

  @file DropTarget.cpp
  @brief Inject drag-and-drop importation of files

  Paul Licameli split from ProjectManager.cpp

**********************************************************************/

#include <wx/dataobj.h>
#include <wx/dnd.h>

#include "AudacityException.h"
#include "AudioIO.h"
#include "FileNames.h"
#include "PlaybackRestart.h"
#include "Project.h"
#include "ProjectAudioIO.h"
#include "ProjectAudioManager.h"
#include "ProjectFileManager.h"
#include "TrackPanel.h"

#if wxUSE_DRAG_AND_DROP
class FileObject final : public wxFileDataObject
{
public:
   FileObject()
   {
   }

   bool IsSupportedFormat(const wxDataFormat & format, Direction WXUNUSED(dir = Get)) const
      // PRL:  This function does NOT override any inherited virtual!  What does it do?
   {
      if (format.GetType() == wxDF_FILENAME) {
         return true;
      }

#if defined(__WXMAC__)
#if !wxCHECK_VERSION(3, 0, 0)
      if (format.GetFormatId() == kDragPromisedFlavorFindFile) {
         return true;
      }
#endif
#endif

      return false;
   }
};

namespace {
class ProjectPlaybackRestartHost final : public PlaybackRestartHost
{
public:
   explicit ProjectPlaybackRestartHost(AudacityProject& project)
      : mProject{ project }
      , mManager{ ProjectAudioManager::Get(project) }
   {}
   bool Playing() const override { return mManager.Playing(); }
   bool Paused() const override { return AudioIO::Get()->IsPaused(); }
   bool Looping() const override { return mManager.Looping(); }
   double StreamTime() const override { return AudioIO::Get()->GetStreamTime(); }
   void Stop() override { mManager.Stop(); }
   void Play(double startTime, bool looping) override
   {
      auto options = ProjectAudioIO::GetDefaultOptions(mProject, looping);
      options.pStartTime.emplace(startTime);
      mManager.PlayPlayRegion(
         SelectedRegion(startTime, startTime), options,
         looping ? PlayMode::loopedPlay : PlayMode::normalPlay);
   }
private:
   AudacityProject& mProject;
   ProjectAudioManager& mManager;
};
} // namespace

class DropTarget final : public wxFileDropTarget
{
public:
   DropTarget(AudacityProject *proj)
   {
      mProject = proj;

      // SetDataObject takes ownership
      SetDataObject(safenew FileObject());
   }

   ~DropTarget()
   {
   }

#if defined(__WXMAC__)
#if !wxCHECK_VERSION(3, 0, 0)
   bool GetData() override
   {
      bool foundSupported = false;
      bool firstFileAdded = false;
      OSErr result;

      UInt16 items = 0;
      CountDragItems((DragReference)m_currentDrag, &items);

      for (UInt16 index = 1; index <= items; index++) {

         DragItemRef theItem = 0;
         GetDragItemReferenceNumber((DragReference)m_currentDrag, index, &theItem);

         UInt16 flavors = 0;
         CountDragItemFlavors((DragReference)m_currentDrag, theItem , &flavors ) ;

         for (UInt16 flavor = 1 ;flavor <= flavors; flavor++) {

            FlavorType theType = 0;
            result = GetFlavorType((DragReference)m_currentDrag, theItem, flavor, &theType);
            if (theType != kDragPromisedFlavorFindFile && theType != kDragFlavorTypeHFS) {
               continue;
            }
            foundSupported = true;

            Size dataSize = 0;
            GetFlavorDataSize((DragReference)m_currentDrag, theItem, theType, &dataSize);

            ArrayOf<char> theData{ dataSize };
            GetFlavorData((DragReference)m_currentDrag, theItem, theType, (void*) theData.get(), &dataSize, 0L);

            wxString name;
            if (theType == kDragPromisedFlavorFindFile) {
               name = wxMacFSSpec2MacFilename((FSSpec *)theData.get());
            }
            else if (theType == kDragFlavorTypeHFS) {
               name = wxMacFSSpec2MacFilename(&((HFSFlavor *)theData.get())->fileSpec);
            }

            if (!firstFileAdded) {
               // reset file list
               ((wxFileDataObject*)GetDataObject())->SetData(0, "");
               firstFileAdded = true;
            }

            ((wxFileDataObject*)GetDataObject())->AddFile(name);

            // We only want to process one flavor
            break;
         }
      }
      return foundSupported;
   }
#endif

   bool OnDrop(wxCoord x, wxCoord y) override
   {
      // bool foundSupported = false;
#if !wxCHECK_VERSION(3, 0, 0)
      bool firstFileAdded = false;
      OSErr result;

      UInt16 items = 0;
      CountDragItems((DragReference)m_currentDrag, &items);

      for (UInt16 index = 1; index <= items; index++) {

         DragItemRef theItem = 0;
         GetDragItemReferenceNumber((DragReference)m_currentDrag, index, &theItem);

         UInt16 flavors = 0;
         CountDragItemFlavors((DragReference)m_currentDrag, theItem , &flavors ) ;

         for (UInt16 flavor = 1 ;flavor <= flavors; flavor++) {

            FlavorType theType = 0;
            result = GetFlavorType((DragReference)m_currentDrag, theItem, flavor, &theType);
            if (theType != kDragPromisedFlavorFindFile && theType != kDragFlavorTypeHFS) {
               continue;
            }
            return true;
         }
      }
#endif
      return CurrentDragHasSupportedFormat();
   }

#endif

   bool OnDropFiles(wxCoord WXUNUSED(x), wxCoord WXUNUSED(y), const wxArrayString& filenames) override
   {
      // Experiment shows that this function can be reached while there is no
      // catch block above in wxWidgets.  So stop all exceptions here.
      return GuardedCall<bool>([&] {
         // Playback uses a snapshot of the tracks taken when it started, so
         // to hear the dropped files, restart playback from where it was.
         ProjectPlaybackRestartHost host{ *mProject };
         return RestartPlaybackAfter(host, [&] {
            return ProjectFileManager::Get(*mProject).Import(filenames);
         });
      });
   }

private:
   AudacityProject *mProject;
};

// Hook the construction of projects
static const AudacityProject::AttachedObjects::RegisteredFactory key{
   [](AudacityProject &project) {
      // We can import now, so become a drag target
      //   SetDropTarget(safenew AudacityDropTarget(this));
      //   mTrackPanel->SetDropTarget(safenew AudacityDropTarget(this));

      TrackPanel::Get( project )
         .SetDropTarget(
            // SetDropTarget takes ownership
            safenew DropTarget( &project ) );
      return nullptr;
   }
};
#endif
