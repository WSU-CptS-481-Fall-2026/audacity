/**********************************************************************

 Audacity: A Digital Audio Editor

 TagsEditorHelpers.h

 Logic used by the Metadata Editor that does not depend on the dialog,
 kept separate so it can be unit tested
 (see tests/TagsEditorHelpersTest.cpp).

 **********************************************************************/
#pragma once

#include <algorithm>
#include <cstddef>

#include <wx/arrstr.h>

#include "Tags.h"
#include "TranslatableString.h"

#define LABEL_ARTIST    XO("Artist Name")
#define LABEL_TITLE     XO("Track Title")
#define LABEL_ALBUM     XO("Album Title")
#define LABEL_TRACK     XO("Track Number")
#define LABEL_YEAR      XO("Year")
#define LABEL_GENRE     XO("Genre")
#define LABEL_COMMENTS  XO("Comments")

// A standard tag shown at the top of the grid: displayed (translated)
// label and the internal key used to store the tag
struct StandardTagLabel
{
   TranslatableString label;
   wxString name;
};

inline const StandardTagLabel labelmap[] =
{
   {  LABEL_ARTIST,     TAG_ARTIST     },
   {  LABEL_TITLE,      TAG_TITLE      },
   {  LABEL_ALBUM,      TAG_ALBUM      },
   {  LABEL_TRACK,      TAG_TRACK      },
   {  LABEL_YEAR,       TAG_YEAR       },
   {  LABEL_GENRE,      TAG_GENRE      },
   {  LABEL_COMMENTS,   TAG_COMMENTS   }
};

// Number of standard tag rows at the top of the grid
constexpr size_t StaticTagCount = sizeof(labelmap) / sizeof(labelmap[0]);

// Returns the internal key for one of the standard tag labels shown in the
// grid, or an empty string if the label is not a standard tag.
inline wxString StandardTagKeyForLabel(const wxString &label)
{
   for (const auto &entry : labelmap)
      if (label.CmpNoCase(entry.label.Translation()) == 0)
         return entry.name;
   return {};
}

// Blanks the title and/or track tag while the user may not edit them,
// and restores them when the guard is destroyed, even after an exception.
class ReadOnlyTagsBlanker final
{
public:
   ReadOnlyTagsBlanker(Tags &tags, bool editTitle, bool editTrack)
      : mTags{ tags }
      , mEditTitle{ editTitle }
      , mEditTrack{ editTrack }
      , mTitle{ tags.GetTag(TAG_TITLE) }
      , mTrack{ tags.GetTag(TAG_TRACK) }
   {
      if (!mEditTitle)
         mTags.SetTag(TAG_TITLE, wxEmptyString);
      if (!mEditTrack)
         mTags.SetTag(TAG_TRACK, wxEmptyString);
   }

   ~ReadOnlyTagsBlanker()
   {
      if (!mEditTitle)
         mTags.SetTag(TAG_TITLE, mTitle);
      if (!mEditTrack)
         mTags.SetTag(TAG_TRACK, mTrack);
   }

   ReadOnlyTagsBlanker(const ReadOnlyTagsBlanker &) = delete;
   ReadOnlyTagsBlanker &operator=(const ReadOnlyTagsBlanker &) = delete;

private:
   Tags &mTags;
   const bool mEditTitle;
   const bool mEditTrack;
   const wxString mTitle;
   const wxString mTrack;
};

// The user's genres, sorted alphabetically
inline wxArrayString SortedUserGenres(Tags &tags)
{
   wxArrayString genres;
   const int count = tags.GetNumUserGenres();
   for (int i = 0; i < count; ++i)
      genres.push_back(tags.GetUserGenre(i));
   std::sort(genres.begin(), genres.end());
   return genres;
}
