/**********************************************************************

  Audacity: A Digital Audio Editor

  @file SpectrumCache.h

  Paul Licameli split from WaveClip.h

*******************************************************************/

#ifndef __AUDACITY_WAVECLIP_SPECTRUM_CACHE__
#define __AUDACITY_WAVECLIP_SPECTRUM_CACHE__

class sampleCount;
class SpectrogramSettings;
class WaveClipChannel;
using WaveChannelInterval = WaveClipChannel;
class WideSampleSequence;

#include <vector>
#include "MemoryX.h"
#include "WaveClip.h" // to inherit WaveClipListener

using Floats = ArrayOf<float>;

class AUDACITY_DLL_API SpecCache {
public:

   // Make invalid cache
   SpecCache()
      : algorithm(-1)
      , spp(-1.0)
      , start(-1.0)
      , windowType(-1)
      , frequencyGain(-1)
      , dirty(-1)
   {
   }

   ~SpecCache()
   {
   }

   static bool isWaveletAnalysis(const SpectrogramSettings& settings);

   bool Matches(
      int dirty_, double samplesPerPixel,
      const SpectrogramSettings& settings) const;

   // Grow the cache while preserving the (possibly now invalid!) contents
   void Grow(
      size_t len_, SpectrogramSettings& settings, double samplesPerPixel,
      double start); /*relative to clip play start time*/

   // Calculate the dirty columns at the begin and end of the cache
   void Populate(
      const SpectrogramSettings& settings, const WaveChannelInterval& clip,
      int copyBegin, int copyEnd, size_t numPixels, double pixelsPerSecond);

   size_t       len { 0 }; // counts pixels, not samples
   int          algorithm;
   double       spp; // samples per pixel
   double       leftTrim{ .0 };
   double       rightTrim{ .0 };
   double       start; // relative to clip start
   int          windowType;
   size_t       windowSize { 0 };
   unsigned     zeroPaddingFactor { 0 };
   int          frequencyGain;
   std::vector<float> freq;
   std::vector<sampleCount> where;

   int          dirty;

private:
   
   struct ComplexValue {
      float real;
      float imag;
   };

   struct SpectrumRange {
      const int lower;
      const int upper;
   };

   struct SpectrumContext {
      const SpectrogramSettings& settings;
      const WaveChannelInterval& clip;
      double pixelsPerSecond;
      const std::vector<float>& gainFactors;
   };

   //Helpers for Calculations
   static void AdjustSampleWindowBounds(sampleCount& from, size_t& myLen, sampleCount numSamples, float*& adj, bool& copy);

   sampleCount GetSamplePosition(int xx, double samplesPerPixel) const;

   static ComplexValue GetFFTValue(const float* buffer, int index, size_t ii);

   static bool CalculateReassignment(
      const SpectrogramSettings& settings,
      int xx,
      double pixelsPerSecond,
      double sampleRate,
      struct SpectrumRange,
      float* scratch,
      float* out);

   float* PrepareSampleBuffer(size_t myLen, const WaveChannelInterval& clip, sampleCount from, float* adj, bool copy, std::vector<float>& floats) const;

   static void CalculateWaveletBins(
      const SpectrogramSettings& settings,
      const float* middleSample,
      float* results);

   void ProcessSpectrumRange(
      SpectrumRange range,
      const SpectrumContext&,
      std::vector<float>& scratch,
      size_t scratchSize);

   void ProcessReassignment(
      SpectrumContext context,
      SpectrumRange range,
      std::vector<float>& scratch);

   // Calculate one column of the spectrum
   bool CalculateOneSpectrum(const int xx, SpectrumRange range, float* __restrict scratch, float* __restrict out, SpectrumContext context) const;

   bool CalculateOneWaveletSpectrum(const int xx, SpectrumRange range, float* __restrict out, SpectrumContext context) const;

   mutable std::optional<AudioSegmentSampleView> mSampleCacheHolder;
};

class SpecPxCache {
public:
   explicit SpecPxCache(size_t cacheLen)
      : len{ cacheLen }
      , values{ len }
   {
      scaleType = 0;
      range = gain = -1;
      minFreq = maxFreq = -1;
   }

   size_t  len;
   Floats values;

   int scaleType;
   int range;
   int gain;
   int minFreq;
   int maxFreq;
};

struct WaveClipSpectrumCache final : WaveClipListener
{
   explicit WaveClipSpectrumCache(size_t nChannels);
   ~WaveClipSpectrumCache() override;

   std::unique_ptr<WaveClipListener> Clone() const override;

   // Cache of values to colour pixels of Spectrogram - used by TrackArtist
   std::vector<std::unique_ptr<SpecPxCache>> mSpecPxCaches;
   std::vector<std::unique_ptr<SpecCache>> mSpecCaches;
   int mDirty { 0 };

   static WaveClipSpectrumCache &Get(const WaveChannelInterval &clip);

   void MarkChanged() noexcept override; // NOFAIL-GUARANTEE
   void Invalidate() override; // NOFAIL-GUARANTEE

   /** Getting high-level data for screen display */
   // PRL:
   // > only the 0th channel of sequence is really used
   // > In the interim, this still works correctly for WideSampleSequence backed
   // > by a right channel track, which always ignores its partner.
   bool GetSpectrogram(const WaveChannelInterval &clip,
      const float *&spectrogram,
      SpectrogramSettings &spectrogramSettings,
      const sampleCount *&where, size_t numPixels,
      double t0 /*absolute time*/, double pixelsPerSecond);

   //helpers for GetSpectroGram
   bool IsCacheValid(
      const SpecCache* cache,
      const WaveChannelInterval& clip,
      double samplesPerPixel,
      const SpectrogramSettings& settings) const;

   struct CacheOverlap {
      int oldX0 = 0;
      double correction = 0.0;
      int copyBegin = 0;
      int copyEnd = 0;
   };

   static CacheOverlap CalculateCacheOverlap(
      const SpecCache& cache,
      size_t numPixels,
      double t0,
      double sampleRate,
      double stretchRatio,
      double samplesPerPixel);


   static void InitializeReassignmentBuffer(
      SpecCache& cache,
      int copyBegin,
      int copyEnd,
      size_t numPixels,
      size_t nBins);

   void MakeStereo(WaveClipListener &&other, bool aligned) override;
   void SwapChannels() override;
   void Erase(size_t index) override;
};

#endif
