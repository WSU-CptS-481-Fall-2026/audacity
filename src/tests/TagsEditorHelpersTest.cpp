/**********************************************************************

 Audacity: A Digital Audio Editor

 TagsEditorHelpersTest.cpp

 Unit tests for TagsEditorHelpers.h: mapping grid labels to tag keys,
 the read-only tags guard and the sorted genre list.

 **********************************************************************/

#include "TagsEditorHelpers.h"

#include <cstdio>
#include <stdexcept>
#include <string>

#include <wx/init.h>

namespace {

int gFailures = 0;

void Check(const std::string& name, bool ok)
{
   if (!ok)
      ++gFailures;
   std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name.c_str());
}

// Replaces the old 7-branch if/else chain in TransferDataFromWindow()
void TestStandardTagKeyForLabel()
{
   Check("seven standard tags", StaticTagCount == 7);

   for (const auto &entry : labelmap)
      Check("label maps to its key: " + entry.name.ToStdString(),
         StandardTagKeyForLabel(entry.label.Translation()) == entry.name);

   Check("lookup ignores case",
      StandardTagKeyForLabel(wxT("artist NAME")) == TAG_ARTIST);
   Check("Track Number maps to TAG_TRACK",
      StandardTagKeyForLabel(wxT("Track Number")) == TAG_TRACK);
   Check("custom tag is not standard",
      StandardTagKeyForLabel(wxT("Composer")).empty());
   Check("empty label is not standard",
      StandardTagKeyForLabel(wxString{}).empty());
}

Tags MakeTags()
{
   Tags tags;
   tags.SetTag(TAG_TITLE, wxT("My Song"));
   tags.SetTag(TAG_TRACK, wxT("5"));
   tags.SetTag(TAG_ARTIST, wxT("Someone"));
   return tags;
}

// Replaces the remember/clear/restore code in OnSave() and OnSaveDefaults()
void TestReadOnlyTagsBlanker()
{
   {
      Tags tags = MakeTags();
      {
         ReadOnlyTagsBlanker blanker{ tags, false, true };
         Check("read-only title is blanked", tags.GetTag(TAG_TITLE).empty());
         Check("editable track is kept", tags.GetTag(TAG_TRACK) == wxT("5"));
         Check("other tags are kept",
            tags.GetTag(TAG_ARTIST) == wxT("Someone"));
      }
      Check("title restored afterwards", tags.GetTag(TAG_TITLE) == wxT("My Song"));
   }
   {
      Tags tags = MakeTags();
      {
         ReadOnlyTagsBlanker blanker{ tags, false, false };
         Check("both read-only: title blanked", tags.GetTag(TAG_TITLE).empty());
         Check("both read-only: track blanked", tags.GetTag(TAG_TRACK).empty());
      }
      Check("both restored: title", tags.GetTag(TAG_TITLE) == wxT("My Song"));
      Check("both restored: track", tags.GetTag(TAG_TRACK) == wxT("5"));
   }
   {
      Tags tags = MakeTags();
      {
         ReadOnlyTagsBlanker blanker{ tags, true, true };
         Check("nothing read-only: title unchanged",
            tags.GetTag(TAG_TITLE) == wxT("My Song"));
         Check("nothing read-only: track unchanged",
            tags.GetTag(TAG_TRACK) == wxT("5"));
      }
   }
   {
      // The old OnSaveDefaults() did not restore the tags if writing failed
      Tags tags = MakeTags();
      try {
         ReadOnlyTagsBlanker blanker{ tags, false, false };
         throw std::runtime_error("simulated failure while saving");
      }
      catch (const std::runtime_error &) {
      }
      Check("restored after an exception: title",
         tags.GetTag(TAG_TITLE) == wxT("My Song"));
      Check("restored after an exception: track",
         tags.GetTag(TAG_TRACK) == wxT("5"));
   }
}

// Replaces the duplicated genre-list code in OnEdit() and PopulateGenres()
void TestSortedUserGenres()
{
   Tags tags;
   const auto genres = SortedUserGenres(tags);
   Check("one entry per user genre",
      static_cast<int>(genres.size()) == tags.GetNumUserGenres());

   bool sorted = true;
   for (size_t i = 1; i < genres.size(); ++i)
      if (genres[i - 1] > genres[i])
         sorted = false;
   Check("genres are sorted", sorted);
}

} // namespace

int main()
{
   // Tags uses wxWidgets services (file paths, config), so initialize them
   wxInitializer initializer;
   if (!initializer.IsOk()) {
      std::printf("Failed to initialize wxWidgets\n");
      return 1;
   }

   TestStandardTagKeyForLabel();
   TestReadOnlyTagsBlanker();
   TestSortedUserGenres();
   std::printf("\n%s\n", gFailures ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
   return gFailures ? 1 : 0;
}
