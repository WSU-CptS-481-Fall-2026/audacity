/**********************************************************************

  Audacity: A Digital Audio Editor

  RawImportSettings.h

  Settings and helpers for the Import Raw dialog, kept free of any UI
  code so they can be unit tested (see tests/RawImportSettingsTest.cpp).

**********************************************************************/
#pragma once

#include <algorithm>
#include <cstddef>
#include <iterator>

#include "sndfile.h"

constexpr unsigned MaxRawChannels = 16;
constexpr double MinRawRate = 100.0;
// Highest preset sample rate supported in Audacity 2.3.0 is 384 kHz
constexpr double MaxRawRate = 384000.0;

// Settings chosen in the Import Raw dialog
struct RawImportSettings
{
   int encoding{ SF_FORMAT_RAW | SF_ENDIAN_CPU | SF_FORMAT_PCM_16 };
   unsigned channels{ 1 };
   long long offset{ 0 };
   double rate{ 0 };      // 0 means "use the project rate"
   double percent{ 100. };
};

// Returns the settings limited to values the importer supports
inline RawImportSettings Clamped(RawImportSettings settings)
{
   if (settings.channels < 1 || settings.channels > MaxRawChannels)
      settings.channels = 1;
   settings.offset = std::max(settings.offset, 0LL);
   settings.percent = std::clamp(settings.percent, 0.0, 100.0);
   settings.rate = std::clamp(settings.rate, MinRawRate, MaxRawRate);
   return settings;
}

// Byte orders offered in the dialog, in the order of its choices
inline constexpr int kEndianFormats[] = {
   SF_ENDIAN_FILE, SF_ENDIAN_LITTLE, SF_ENDIAN_BIG, SF_ENDIAN_CPU
};

// Index of the dialog's byte-order choice for a libsndfile format value
inline int getEndianChoice(int sfFormat)
{
   const auto endian = sfFormat & SF_FORMAT_ENDMASK;
   const auto it = std::find(
      std::begin(kEndianFormats), std::end(kEndianFormats), endian);
   if (it == std::end(kEndianFormats))
      return 0;
   return static_cast<int>(std::distance(std::begin(kEndianFormats), it));
}

// Copies one channel out of interleaved frames
template<typename Sample>
void DeinterleaveChannel(const Sample *interleaved, Sample *channelOut,
   size_t frames, size_t numChannels, size_t channel)
{
   for (size_t j = 0; j < frames; ++j)
      channelOut[j] = interleaved[numChannels * j + channel];
}
