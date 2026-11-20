/**********************************************************************

  Audacity: A Digital Audio Editor

  ImportRaw.cpp

  Dominic Mazzoni

*******************************************************************//**

\file ImportRaw.cpp
\brief Functions for guessing audio type and attempting to read from
unknown sample audio data.  Implements ImportRawDialog.

*//****************************************************************//**

\class ImportRawDialog
\brief ImportRawDialog prompts you with options such as endianness
and sample size to help you importing data of an unknown format.

*//*******************************************************************/



#include "ImportRaw.h"

#include "ImportPlugin.h"
#include "ImportUtils.h"

#include "AudioIOBase.h"
#include "FileFormats.h"
#include "Prefs.h"
#include "ProjectRate.h"
#include "SelectFile.h"
#include "ShuttleGui.h"
#include "UserException.h"
#include "WaveTrack.h"
#include "ProgressDialog.h"

#include <cmath>
#include <stdint.h>
#include <string>
#include <vector>

#include <wx/crt.h>
#include <wx/defs.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/panel.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include "FormatClassifier.h"
#include "RawImportSettings.h"

#include "sndfile.h"

// Settings remembered between raw imports during a session
static RawImportSettings& LastRawImportSettings()
{
   static RawImportSettings settings;
   return settings;
}

class ImportRawDialog final : public wxDialogWrapper {

public:
   ImportRawDialog(wxWindow* parent, const wxString& fileName,
      RawImportSettings& settings);

   void OnOK(wxCommandEvent& event);
   void OnCancel(wxCommandEvent& event);
   void OnDetect(wxCommandEvent& event);
   void OnChoice(wxCommandEvent& event);

private:
   // libsndfile format value for the encoding and byte order selected
   int SelectedEncoding() const;

   RawImportSettings& mSettings;

   wxButton* mOK;
   wxChoice* mEncodingChoice;
   wxChoice* mEndianChoice;
   wxChoice* mChannelChoice;
   wxTextCtrl* mOffsetText;
   wxTextCtrl* mPercentText;
   wxComboBox* mRateText;

   std::vector<int> mEncodingSubtype;

   wxString mFileName;

   DECLARE_EVENT_TABLE()
};

// Prints libsndfile's latest error message for diagnostics
static void PrintSndfileError(SNDFILE* sndFile)
{
   char str[1000];
   sf_error_str(sndFile, str, sizeof(str));
   wxPrintf("%s\n", str);
}

// Opens fileName as raw audio described by sndInfo, starting at offset.
// file must stay open as long as the returned SFFile is used.
static SFFile OpenRawSndFile(wxFile& file, const wxString& fileName,
   SF_INFO& sndInfo, sf_count_t offset)
{
   SFFile sndFile;
   if (file.Open(fileName)) {
      // Even though there is an sf_open() that takes a filename, use the one
      // that takes a file descriptor since wxWidgets can open a file with a
      // Unicode name and libsndfile can't (under Windows).
      sndFile.reset(SFCall<SNDFILE*>(
         sf_open_fd, file.fd(), SFM_READ, &sndInfo, FALSE));
   }

   if (!sndFile) {
      PrintSndfileError(nullptr);
      throw FileException{ FileException::Cause::Open, fileName };
   }

   if (sf_command(sndFile.get(), SFC_SET_RAW_START_OFFSET,
      &offset, sizeof(offset)) != 0) {
      PrintSndfileError(sndFile.get());
      throw FileException{ FileException::Cause::Read, fileName };
   }

   SFCall<sf_count_t>(sf_seek, sndFile.get(), 0, SEEK_SET);
   return sndFile;
}

// This function leaves outTracks empty as an indication of error,
// but may also throw FileException to make use of the application's
// user visible error reporting.
void ImportRaw(const AudacityProject& project, wxWindow* parent, const wxString& fileName,
   WaveTrackFactory* trackFactory, TrackHolders& outTracks)
{
   outTracks.clear();

   TrackListHolder trackList;
   auto updateResult = ProgressResult::Success;

   {
      auto& settings = LastRawImportSettings();

      // On first run, set default sample rate from project rate
      if (settings.rate < MinRawRate)
         settings.rate = ProjectRate::Get(project).GetRate();

      ImportRawDialog dlog(parent, fileName, settings);
      if (dlog.ShowModal() != wxID_OK)
         return;

      const int encoding = settings.encoding;
      const unsigned numChannels = settings.channels;
      const double rate = settings.rate;
      const sf_count_t offset = static_cast<sf_count_t>(settings.offset);
      const double percent = settings.percent;

      SF_INFO sndInfo = { 0 };
      sndInfo.samplerate = static_cast<int>(rate);
      sndInfo.channels = static_cast<int>(numChannels);
      sndInfo.format = encoding | SF_FORMAT_RAW;

      wxFile f;   // must stay open while sndFile is in use
      auto sndFile = OpenRawSndFile(f, fileName, sndInfo, offset);

      auto totalFrames =
         // fraction of a sf_count_t value
         (sampleCount)(sndInfo.frames * percent / 100.0);

      //
      // Sample format:
      //
      // In general, go with the user's preferences.  However, if
      // the file is higher-quality, go with a format which preserves
      // the quality of the original file.
      //

      const auto effectiveFormat = sf_subtype_to_effective_format(encoding);
      const auto format = ImportUtils::ChooseFormat(effectiveFormat);

      // libsndfile reads 16-bit samples as short, everything else as float
      const sampleFormat readFormat =
         (format == int16Sample) ? int16Sample : floatSample;

      trackList = trackFactory->CreateMany(numChannels, format, rate);

      const auto maxBlockSize = (*trackList->Any<WaveTrack>().begin())->GetMaxBlockSize();

      SampleBuffer srcbuffer(maxBlockSize * numChannels, readFormat);
      SampleBuffer buffer(maxBlockSize, readFormat);

      decltype(totalFrames) framescompleted = 0;
      if (totalFrames < 0) {
         wxASSERT(false);
         totalFrames = 0;
      }

      auto msg = XO("Importing %s").Format(wxFileName::FileName(fileName).GetFullName());

      /* i18n-hint: 'Raw' means 'unprocessed' here and should usually be translated.*/
      ProgressDialog progress(XO("Import Raw"), msg);

      size_t block;
      do {
         block =
            limitSampleBufferSize(maxBlockSize, totalFrames - framescompleted);

         sf_count_t sf_result;
         if (readFormat == int16Sample)
            sf_result = SFCall<sf_count_t>(sf_readf_short, sndFile.get(),
               reinterpret_cast<short*>(srcbuffer.ptr()), block);
         else
            sf_result = SFCall<sf_count_t>(sf_readf_float, sndFile.get(),
               reinterpret_cast<float*>(srcbuffer.ptr()), block);

         if (sf_result >= 0) {
            block = sf_result;
         }
         else {
            // This is not supposed to happen, sndfile.h says result is always
            // a count, not an invalid value for error
            throw FileException{ FileException::Cause::Read, fileName };
         }

         if (block) {
            size_t c = 0;
            ImportUtils::ForEachChannel(*trackList, [&](auto& channel)
               {
                  if (readFormat == int16Sample)
                     DeinterleaveChannel(
                        reinterpret_cast<const short*>(srcbuffer.ptr()),
                        reinterpret_cast<short*>(buffer.ptr()),
                        block, numChannels, c);
                  else
                     DeinterleaveChannel(
                        reinterpret_cast<const float*>(srcbuffer.ptr()),
                        reinterpret_cast<float*>(buffer.ptr()),
                        block, numChannels, c);

                  channel.AppendBuffer(buffer.ptr(), readFormat, block,
                     1, effectiveFormat);
                  ++c;
               });
            framescompleted += block;
         }

         updateResult = progress.Update(
            framescompleted.as_long_long(),
            totalFrames.as_long_long()
         );
         if (updateResult != ProgressResult::Success)
            break;

      } while (block > 0 && framescompleted < totalFrames);
   }

   if (updateResult == ProgressResult::Failed || updateResult == ProgressResult::Cancelled)
      throw UserException{};

   ImportUtils::FinalizeImport(outTracks, move(*trackList));
}


//
// ImportRawDialog
//

enum {
   ChoiceID = 9000,
   DetectID,
};

BEGIN_EVENT_TABLE(ImportRawDialog, wxDialogWrapper)
EVT_BUTTON(wxID_OK, ImportRawDialog::OnOK)
EVT_BUTTON(wxID_CANCEL, ImportRawDialog::OnCancel)
EVT_BUTTON(DetectID, ImportRawDialog::OnDetect)
EVT_CHOICE(ChoiceID, ImportRawDialog::OnChoice)
END_EVENT_TABLE()

ImportRawDialog::ImportRawDialog(wxWindow* parent, const wxString& fileName,
   RawImportSettings& settings)
   : wxDialogWrapper(parent, wxID_ANY, XO("Import Raw Data"),
      wxDefaultPosition, wxDefaultSize,
      wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
   mSettings(settings),
   mFileName(fileName)
{
   wxASSERT(0 < mSettings.channels && mSettings.channels <= MaxRawChannels);

   SetName();

   // Append filename at window title
   wxFileName wfn{ fileName };
   wxString windowTitle = XO("%s: %s").Format(GetTitle(), wfn.GetFullName()).Translation();
   wxDialog::SetTitle(windowTitle);

   ShuttleGui S(this, eIsCreating);
   TranslatableStrings encodings;

   int num = sf_num_encodings();

   int selection = 0;
   for (int i = 0; i < num; i++) {
      SF_INFO info = { 0 };

      int subtype = sf_encoding_index_to_subtype(i);
      info.format = SF_FORMAT_RAW + SF_ENDIAN_LITTLE + subtype;
      info.channels = 1;
      info.samplerate = 44100;

      if (sf_format_check(&info)) {
         mEncodingSubtype.push_back(subtype);
         encodings.push_back(Verbatim(sf_encoding_index_name(i)));

         if ((mSettings.encoding & SF_FORMAT_SUBMASK) == subtype)
            selection = mEncodingSubtype.size() - 1;
      }
   }

   TranslatableStrings endians{
      /* i18n-hint: Refers to byte-order.  Don't translate "endianness" if you don't
          know the correct technical word. */
      XO("No endianness") ,
      /* i18n-hint: Refers to byte-order.  Don't translate this if you don't
       know the correct technical word. */
      XO("Little-endian") ,
      /* i18n-hint: Refers to byte-order.  Don't translate this if you don't
         know the correct technical word. */
      XO("Big-endian") ,
      /* i18n-hint: Refers to byte-order.  Don't translate "endianness" if you don't
         know the correct technical word. */
      XO("Default endianness") ,
   };

   int endian = getEndianChoice(mSettings.encoding);

   TranslatableStrings chans{
      XO("1 Channel (Mono)") ,
      XO("2 Channels (Stereo)") ,
   };
   for (int i = 2; i < static_cast<int>(MaxRawChannels); i++) {
      chans.push_back(XO("%d Channels").Format(i + 1));
   }

   S.StartVerticalLay(false);
   {
      S.SetBorder(5);
      S.StartTwoColumn();
      {
         mEncodingChoice = S.Id(ChoiceID).AddChoice(XXO("Encoding:"),
            encodings,
            selection);
         mEndianChoice = S.Id(ChoiceID).AddChoice(XXO("Byte order:"),
            endians,
            endian);
         mChannelChoice = S.Id(ChoiceID).AddChoice(XXO("Channels:"),
            chans,
            mSettings.channels - 1);
      }
      S.EndTwoColumn();

      S.SetBorder(5);
      S.StartMultiColumn(3);
      {
         // Offset text
         /* i18n-hint: (noun)*/
         mOffsetText = S.AddTextBox(XXO("Start offset:"),
            wxString(std::to_string(mSettings.offset)),
            12);
         S.AddUnits(XO("bytes"));

         // Percent text
         mPercentText = S.AddTextBox(XXO("Amount to import:"),
            wxT("100"),
            12);
         S.AddUnits(XO("%"));

         // Rate text
         wxArrayStringEx rates;
         for (int i = 0; i < AudioIOBase::NumStandardRates; i++) {
            rates.Add(
               wxString::Format(wxT("%d"), AudioIOBase::StandardRates[i]));
         }

         /* i18n-hint: (noun)*/
         mRateText = S.AddCombo(XXO("Sample rate:"),
            wxString::Format(wxT("%d"), static_cast<int>(mSettings.rate)),
            rates);
         /* i18n-hint: This is the abbreviation for "Hertz", or
            cycles per second. */
         S.AddUnits(XO("Hz"));
      }
      S.EndMultiColumn();

      S.SetBorder(5);
      S.StartTwoColumn();
      {
         /* i18n-hint: Guess format of raw file */
         S.Id(DetectID).AddButton(XXO("Detect"));

         S.AddStandardButtons();
      }
      S.EndTwoColumn();

      // Find the OK button, and change its text to 'Import'.
      // We MUST set mOK because it is used later.
      mOK = static_cast<wxButton*>(wxWindow::FindWindowById(wxID_OK, this));
      mOK->SetLabel(_("&Import"));
   }
   S.EndVerticalLay();

   Fit();
   SetSizeHints(GetSize());

   Centre(wxBOTH);
}

int ImportRawDialog::SelectedEncoding() const
{
   return mEncodingSubtype[mEncodingChoice->GetSelection()]
      | kEndianFormats[mEndianChoice->GetSelection()];
}

void ImportRawDialog::OnOK(wxCommandEvent& WXUNUSED(event))
{
   RawImportSettings chosen = mSettings;
   chosen.encoding = SelectedEncoding();
   chosen.channels = mChannelChoice->GetSelection() + 1;

   long long offset = 0;   // stays 0 if the text is not a number
   mOffsetText->GetValue().ToLongLong(&offset);
   chosen.offset = offset;

   mPercentText->GetValue().ToDouble(&chosen.percent);
   mRateText->GetValue().ToDouble(&chosen.rate);

   mSettings = Clamped(chosen);

   EndModal(wxID_OK);
}

void ImportRawDialog::OnCancel(wxCommandEvent& WXUNUSED(event))
{
   EndModal(wxID_CANCEL);
}

void ImportRawDialog::OnDetect(wxCommandEvent& event)
{
   try {
      // Yes, FormatClassifier currently handles filenames in UTF8 format only, that's a TODO ...
      FormatClassifier theClassifier(mFileName.utf8_str());
      mSettings.encoding = theClassifier.GetResultFormatLibSndfile();
      mSettings.channels = theClassifier.GetResultChannels();
   }
   catch (...) {
      // Something went wrong in FormatClassifier, abort.
      return;
   }

   int selection = 0;
   auto iter = std::find(mEncodingSubtype.begin(), mEncodingSubtype.end(),
      mSettings.encoding & SF_FORMAT_SUBMASK);
   if (iter != mEncodingSubtype.end())   // subtype found
      selection = std::distance(mEncodingSubtype.begin(), iter);

   int endian = getEndianChoice(mSettings.encoding);

   mEncodingChoice->SetSelection(selection);
   mEndianChoice->SetSelection(endian);
   mChannelChoice->SetSelection(mSettings.channels - 1);

   OnChoice(event);
}

void ImportRawDialog::OnChoice(wxCommandEvent& WXUNUSED(event))
{
   SF_INFO info = { 0 };

   mSettings.encoding = SelectedEncoding();

   info.format = mSettings.encoding | SF_FORMAT_RAW;
   info.channels = mChannelChoice->GetSelection() + 1;
   info.samplerate = 44100;

   if (sf_format_check(&info)) {
      mOK->Enable(true);
      return;
   }

   // Try it with 1-channel
   info.channels = 1;
   if (sf_format_check(&info)) {
      mChannelChoice->SetSelection(0);
      mOK->Enable(true);
      return;
   }

   // Otherwise, this is an unsupported format
   mOK->Enable(false);
}
