/**********************************************************************

  Audacity: A Digital Audio Editor

  @file SpectrumCache.cpp

  Paul Licameli split from WaveClip.cpp

**********************************************************************/

#include "SpectrumCache.h"

#include "SpectrogramSettings.h"
#include "RealFFTf.h"
#include "Sequence.h"
#include "Spectrum.h"
#include "WaveClipUIUtilities.h"
#include "WaveTrack.h"
#include "WideSampleSequence.h"
#include <cmath>

namespace {

void ComputeSpectrumUsingRealFFTf
   (float * __restrict buffer, const FFTParam *hFFT,
    const float * __restrict window, size_t len, float * __restrict out)
{
   constexpr float kZeroPowerDecibels = -160.0f;
   size_t i;
   len = std::min(len, hFFT->Points * 2);
   len = hFFT->Points * 2;
   for(i = 0; i < len; i++)
      buffer[i] *= window[i];
   for( ; i < (hFFT->Points * 2); i++)
      buffer[i] = 0; // zero pad as needed
   RealFFTf(buffer, hFFT);
   // Handle the (real-only) DC
   float power = buffer[0] * buffer[0];
   if(power <= 0)
      out[0] = kZeroPowerDecibels;
   else
      out[0] = 10.0 * log10f(power);
   for(i = 1; i < hFFT->Points; i++) {
      const int index = hFFT->BitReversed[i];
      const float real = buffer[index];
        const float imaginary = buffer[index + 1];
      power = real * real + imaginary * imaginary;
      if(power <= 0)
         out[i] = kZeroPowerDecibels;
      else
         out[i] = 10.0*log10f(power);
   }
}

void ComputeSpectrogramGainFactors
   (size_t fftLen, double rate, int frequencyGain, std::vector<float> &gainFactors)
{
   if (frequencyGain > 0) {
      // Compute a frequency-dependent gain factor
      // scaled such that 1000 Hz gets a gain of 0dB

      // This is the reciprocal of the bin number of 1000 Hz:
      const double factor = (rate / (double)fftLen) / 1000.0;

      auto half = fftLen / 2;
      gainFactors.reserve(half);
      // Don't take logarithm of zero!  Let bin 0 replicate the gain factor for bin 1.
      gainFactors.push_back(frequencyGain*log10(factor));
      for (decltype(half) x = 1; x < half; x++) {
         gainFactors.push_back(frequencyGain*log10(factor * x));
      }
   }
}

}

bool SpecCache::isWaveletAnalysis(const SpectrogramSettings& settings) 
{
    return settings.algorithm == SpectrogramSettings::algWavelet;
}

bool SpecCache::Matches(
   int dirty_, double samplesPerPixel,
   const SpectrogramSettings& settings) const
{
   // Make a tolerant comparison of the spp values in this wise:
   // accumulated difference of times over the number of pixels is less than
   // a sample period.
   const bool sppMatch = (fabs(samplesPerPixel - spp) * len < 1.0);

   if (isWaveletAnalysis(settings))
   {
       return
          sppMatch &&
          dirty == dirty_ &&
          frequencyGain == settings.frequencyGain &&
          algorithm == settings.algorithm &&
          windowSize == settings.WindowSize();
   }
   return
      sppMatch &&
      dirty == dirty_ &&
      windowType == settings.windowType &&
      windowSize == settings.WindowSize() &&
      zeroPaddingFactor == settings.ZeroPaddingFactor() &&
      frequencyGain == settings.frequencyGain &&
      algorithm == settings.algorithm;
}

static void ApplyWindowAndFFT(float* buffer, const float* window, size_t fftLen, FFTParam* hFFT)
{
   for (size_t ii = 0; ii < fftLen; ++ii)
   {
      buffer[ii] *= window[ii];
   }
   RealFFTf(buffer, hFFT);
}

void SpecCache::AdjustSampleWindowBounds(sampleCount& from, size_t& myLen, sampleCount numSamples, float*& adj,bool& copy)
{
   if (from < 0) {
      // Near the start of the clip, pad left with zeroes as needed.
      // from is at least -windowSize / 2
      for (auto ii = from; ii < 0; ++ii)
         *adj++ = 0;
      myLen += from.as_long_long(); // add a negative
      from = 0;
      copy = true;
   }

   if (from + myLen >= numSamples) {
      // Near the end of the clip, pad right with zeroes as needed.
      // newlen is bounded by myLen:
      auto newlen = (numSamples - from).as_size_t();
      for (decltype(myLen) ii = newlen; ii < myLen; ++ii)
         adj[ii] = 0;
      myLen = newlen;
      copy = true;
   }
}

sampleCount SpecCache::GetSamplePosition(
   int xx, double samplesPerPixel) const
{
   if (xx < 0)
      return sampleCount(where[0].as_double() + (xx * samplesPerPixel));
   else if (xx > (int)len)
      return sampleCount(where[len].as_double() + (xx - len) * samplesPerPixel);
   return where[xx];
}
SpecCache::ComplexValue
SpecCache::GetFFTValue(const float* buffer, int index, size_t ii)
{
   return {
      buffer[index],
      ii == 0 ? 0.0F : buffer[index + 1]
   };
}

bool SpecCache::CalculateReassignment(
   const SpectrogramSettings& settings,
   int xx,
   double pixelsPerSecond,
   double sampleRate,
   SpectrumRange range,
   float* scratch,
   float* out)
{
   bool result = false;
   const size_t fftLen =
      settings.WindowSize() * settings.ZeroPaddingFactor();
   const auto nBins = settings.NBins();
   static const double epsilon = 1e-16;
   auto *const hFFT = settings.hFFT.get();

   float* const scratch2 = scratch + fftLen;
   std::copy(scratch, scratch2, scratch2);

   float* const scratch3 = scratch + (2 * fftLen);
   std::copy(scratch, scratch2, scratch3);

   ApplyWindowAndFFT(scratch, settings.window.get(), fftLen, hFFT);

   ApplyWindowAndFFT(scratch2, settings.dWindow.get(), fftLen, hFFT);

   ApplyWindowAndFFT(scratch3, settings.tWindow.get(), fftLen, hFFT);


   for (size_t ii = 0; ii < hFFT->Points; ++ii) {
      const int index = hFFT->BitReversed[ii];
      const auto denom = GetFFTValue(scratch, index, ii);

      const double power = (denom.real * denom.real) + (denom.imag * denom.imag);
      if (power < epsilon)
         // Avoid dividing by near-zero below
         continue;

      const auto freqNum = GetFFTValue(scratch2, index, ii);

      // Find complex quotient --
      // Which means, multiply numerator by conjugate of denominator,
      // then divide by norm squared of denominator --
      // Then just take its imaginary part.
      const double quotIm =
         (-freqNum.real * denom.imag +
            freqNum.imag * denom.real) / power;

      // With appropriate multiplier, that becomes the correction of
      // the frequency bin.
      const double freqCorrection = -(fftLen / (2.0F * M_PI)) * quotIm;

      const int bin = (int)((int)ii + freqCorrection + 0.5F);
      // Must check if correction takes bin out of bounds, above or below!
      // bin is signed!
      if (bin >= 0 && bin < (int)hFFT->Points) {
         const auto timeNum = GetFFTValue(scratch3, index, ii);

         // Find another complex quotient --
         // Then just take its real part.
         // The result has sample interval as unit.
         const double timeCorrection =
            (timeNum.real * denom.real +
               timeNum.imag * denom.imag) / power;

         // PRL: timeCorrection is scaled to the clip's raw sample rate,
         // without the stretching ratio correction for real time. We want
         // to find the correct X coordinate for that.
         int correctedX = floor(
            0.5 + xx + ((timeCorrection * pixelsPerSecond) / sampleRate));
         if (correctedX >= range.lower && correctedX < range.upper)
         {
            result = true;

            // This is non-negative, because bin and correctedX are
            auto ind = ((int)nBins * correctedX) + bin;
#ifdef _OPENMP
            // This assignment can race if index reaches into another thread's bins.
            // The probability of a race very low, so this carries little overhead,
            // about 5% slower vs allowing it to race.
#pragma omp atomic update
#endif
            out[ind] += power;
         }
      }
   }
}

float*  SpecCache::PrepareSampleBuffer(size_t myLen, const WaveChannelInterval& clip, sampleCount from, float* adj, bool copy, std::vector<float>& floats) const
{
   if (myLen > 0) {
      float* useBuffer = 0;
      constexpr auto mayThrow = false; // Don't throw just for display
      mSampleCacheHolder.emplace(
         clip.GetSampleView(from, myLen, mayThrow));
      floats.resize(myLen);
      mSampleCacheHolder->Copy(floats.data(), myLen);
      useBuffer = floats.data();
      if (copy) {
         if (useBuffer)
            memcpy(adj, useBuffer, myLen * sizeof(float));
         else
            memset(adj, 0, myLen * sizeof(float));
      }
      return useBuffer;
   }
   return nullptr;
}

bool SpecCache::CalculateOneSpectrum(
   const int xx, SpectrumRange range, float* __restrict scratch, float* __restrict out, SpectrumContext context) const
{
   bool result = false;

   const bool reassignment =
      (context.settings.algorithm == SpectrogramSettings::algReassignment);
   const size_t windowSizeSetting = context.settings.WindowSize();

   const auto numSamples = context.clip.GetSequence().GetNumSamples();
   const auto sampleRate = context.clip.GetRate();
   const auto stretchRatio = context.clip.GetStretchRatio();
   const auto samplesPerPixel = sampleRate / context.pixelsPerSecond / stretchRatio;
   // xx may be for a column that is out of the visible bounds, but only
   // when we are calculating reassignment contributions that may cross into
   // the visible area.

   sampleCount from = GetSamplePosition(xx, samplesPerPixel);

   const bool autocorrelation =
      context.settings.algorithm == SpectrogramSettings::algPitchEAC;
   const size_t zeroPaddingFactorSetting = context.settings.ZeroPaddingFactor();
   const size_t padding = (windowSizeSetting * (zeroPaddingFactorSetting - 1)) / 2;
   const size_t fftLen = windowSizeSetting * zeroPaddingFactorSetting;
   auto nBins = context.settings.NBins();

   if (from < 0 || from >= numSamples) {
      if (xx >= 0 && xx < (int)len) {
         // Pixel column is out of bounds of the clip!  Should not happen.
         float *const results = &out[nBins * xx];
         std::fill(results, results + nBins, 0.0f);
      }
   }
   else {


      // We can avoid copying memory when ComputeSpectrum is used below
      bool copy = !autocorrelation || (padding > 0) || reassignment;
      float* useBuffer = 0;
      float *adj = scratch + padding;

      {
         auto myLen = windowSizeSetting;
         // Take a window of the track centered at this sample.
         from -= windowSizeSetting >> 1;

         //handles padding, left or right depending on where clip is located.
         AdjustSampleWindowBounds(from, myLen, numSamples, adj, copy);
         std::vector<float> floats;
         useBuffer = PrepareSampleBuffer(myLen, context.clip, from, adj, copy, floats);
      }

      if (copy || !useBuffer)
         useBuffer = scratch;

      if (autocorrelation) {
         // not reassignment, xx is surely within bounds.
         wxASSERT(xx >= 0);
         float *const results = &out[nBins * xx];
         // This function does not mutate useBuffer
         ComputeSpectrum(
            useBuffer, windowSizeSetting, windowSizeSetting, results,
            autocorrelation, context.settings.windowType);
      }
      else if (reassignment) {
         result = CalculateReassignment(context.settings, xx, context.pixelsPerSecond, sampleRate, range, scratch, out);
      }
      else {
         // not reassignment, xx is surely within bounds.
         wxASSERT(xx >= 0);
         float *const results = &out[nBins * xx];

         // Do the FFT.  Note that useBuffer is multiplied by the window,
         // and the window is initialized with leading and trailing zeroes
         // when there is padding.  Therefore we did not need to reinitialize
         // the part of useBuffer in the padding zones.

         // This function mutates useBuffer
         ComputeSpectrumUsingRealFFTf
            (useBuffer, context.settings.hFFT.get(), context.settings.window.get(), fftLen, results);
         if (!context.gainFactors.empty()) {
            // Apply a frequency-dependent gain factor
            for (size_t ii = 0; ii < nBins; ++ii)
               results[ii] += context.gainFactors[ii];
         }
      }
   }

   return result;
}

void SpecCache::CalculateWaveletBins(
   const SpectrogramSettings& settings,
   const float* middleSample,
   float* results)
{
   const size_t nBins = settings.NBins();
   constexpr float kZeroPowerDecibels = -160.0F;

   for (size_t iBin = 0; iBin < nBins; ++iBin) {
      if (settings.waveletSizes[iBin] == 0) {
         results[iBin] = kZeroPowerDecibels;
         continue;
      }

      const float* right = middleSample;
      const float* left = middleSample;

      // Do the inner product
      const float* wRe = &settings.waveletsRe[iBin][0];
      const float* wIm = &settings.waveletsIm[iBin][0];

      double sumRe = *middleSample * *wRe;
      double sumIm = 0.0;

      wRe++, wIm++, right++, left--;

      const auto halfLen = settings.waveletSizes[iBin];

      for (auto ii = 1; ii < halfLen; ii++) {
         sumRe += (*right + *left) * *wRe;
         sumIm += (*left - *right) * *wIm;

         wRe++, wIm++, right++, left--;
      }

      // Calculate magnitude
      float power = (sumRe * sumRe) + (sumIm * sumIm);

      if (power <= 0)
         power = kZeroPowerDecibels;
      else
         power = 10.0 * log10f(power);

      results[iBin] = power;
   }
}

bool SpecCache::CalculateOneWaveletSpectrum(
   const int xx, SpectrumRange range, float* __restrict out, SpectrumContext context) const
{
   bool result = false;
   sampleCount from;

   const auto numSamples = context.clip.GetSequence().GetNumSamples();
   const auto sampleRate = context.clip.GetRate();
   const auto stretchRatio = context.clip.GetStretchRatio();
   const auto samplesPerPixel = sampleRate / context.pixelsPerSecond / stretchRatio;
   const size_t nBins = context.settings.NBins();
   float *const results = &out[nBins * xx];
   std::vector<float> floats;
   // not reassignment, xx is surely within bounds.
   wxASSERT(xx >= 0);
   if (xx > (int)len)
      from = sampleCount(where[len].as_double() + (xx - len) * samplesPerPixel);
   else
      from = where[xx];

   if (from < 0 || from >= numSamples) {
      if (xx >= 0 && xx < (int)len) {
         // Pixel column is out of bounds of the clip!  Should not happen.
         std::fill(results, results + nBins, 0.0f);
      }
   }
   else
   {
      // Iterate frequencies in turn
      // Note that as opposed to Fourier Transform, using wavelets implies a window length that varies with frequency
      // We extract samples corresponding to max wavelet length (the first)
    
      // Locate longest wavelet (first one)
      const auto longestWaveletLen = context.settings.waveletMaxLength;
      wxASSERT(longestWaveletLen);
       
      auto myLen = 2 * longestWaveletLen - 1;
      std::vector<float> scratch(myLen);
      bool copy = true;
      float* adj = scratch.data();

      //Fixes padding on left and right depending on where the clip is
      AdjustSampleWindowBounds(from, myLen, numSamples, adj, copy);
      PrepareSampleBuffer(myLen, context.clip, from, adj, copy, floats);
      
      // Done preparing samples. Now onward with calculations
      const float * middleSample = &scratch[longestWaveletLen - 1];
      CalculateWaveletBins(context.settings, middleSample, results);

       if (!context.gainFactors.empty()) {
          // Apply a frequency-dependent gain factor
          for (size_t ii = 0; ii < nBins; ++ii)
             results[ii] += context.gainFactors[ii];
       }
       result = true;
   }

   return result;
}

void SpecCache::Grow(
   size_t len_, SpectrogramSettings& settings, double samplesPerPixel,
   double start_)
{
   settings.CacheWindows();

   // len columns, and so many rows, column-major.
   // Don't take column literally -- this isn't pixel data yet, it's the
   // raw data to be mapped onto the display.
   freq.resize(len_ * settings.NBins());

   // Sample counts corresponding to the columns, and to one past the end.
   where.resize(len_ + 1);

   len = len_;
   algorithm = settings.algorithm;
   spp = samplesPerPixel;
   start = start_;
   windowType = settings.windowType;
   windowSize = settings.WindowSize();
   zeroPaddingFactor = settings.ZeroPaddingFactor();
   frequencyGain = settings.frequencyGain;
}

void SpecCache::ProcessSpectrumRange(
   SpectrumRange range,
   const SpectrumContext& context,
   std::vector<float>& scratch,
   size_t scratchSize)
{
   // todo(mhodgkinson): I don't find an option to define _OPENMP anywhere. Is this
// still of interest?
#ifdef _OPENMP
      // Storage for mutable per-thread data.
      // private clause ensures one copy per thread
   struct ThreadLocalStorage {
      ThreadLocalStorage() {}
      ~ThreadLocalStorage() {}

      void init(SampleTrackCache& waveTrackCache, size_t scratchSize) {
         if (!cache) {
            cache = std::make_unique<SampleTrackCache>(waveTrackCache.GetTrack());
            scratch.resize(scratchSize);
         }
      }
      std::unique_ptr<SampleTrackCache> cache;
      std::vector<float> scratch;
   } tls;

#pragma omp parallel for private(tls)
#endif
   for (auto xx = range.lower; xx < range.upper; ++xx)
   {
      if (isWaveletAnalysis(context.settings))
      {
         CalculateOneWaveletSpectrum(xx, range, freq.data(), context);
      }
      else
      {
#ifdef _OPENMP
         tls.init(waveTrackCache, scratchSize);
         float* buffer = &tls.scratch[0];
#else
         float* buffer = scratch.data();
#endif
         CalculateOneSpectrum(xx, range,
           buffer, freq.data(), context);
      }
   }
}

void SpecCache::ProcessReassignment(
   SpectrumContext context,
   SpectrumRange range,
   std::vector<float>& scratch)
{
   // Need to look beyond the edges of the range to accumulate more
         // time reassignments.
         // I'm not sure what's a good stopping criterion?
   const auto sampleRate = context.clip.GetRate();
   const auto fftLen =
      context.settings.WindowSize() * context.settings.ZeroPaddingFactor();
   const auto nBins = context.settings.NBins();
   auto xx = range.lower;
   const double pixelsPerSample =
      context.pixelsPerSecond * context.clip.GetStretchRatio() / sampleRate;
   const int limit = std::min((int)(0.5 + fftLen * pixelsPerSample), 100);
   for (int ii = 0; ii < limit; ++ii)
   {
      const bool result = CalculateOneSpectrum(--xx, range, scratch.data(), freq.data(), context);
      if (!result)
         break;
   }

   xx = range.upper;
   for (int ii = 0; ii < limit; ++ii)
   {
      const bool result = CalculateOneSpectrum(xx++, range, scratch.data(), freq.data(), context);
      if (!result)
         break;
   }

   // Now Convert to dB terms.  Do this only after accumulating
   // power values, which may cross columns with the time correction.
#ifdef _OPENMP
#pragma omp parallel for
#endif
   constexpr float kZeroPowerDecibels = -160.0f;
   for (xx = range.lower; xx < range.upper; ++xx) {
      float* const results = &freq[nBins * xx];
      for (size_t ii = 0; ii < nBins; ++ii) {
         float& power = results[ii];
         if (power <= 0)
            power = kZeroPowerDecibels;
         else
            power = 10.0 * log10f(power);
      }
      if (!context.gainFactors.empty()) {
         // Apply a frequency-dependent gain factor
         for (size_t ii = 0; ii < nBins; ++ii)
            results[ii] += context.gainFactors[ii];
      }
   }
}

void SpecCache::Populate(
   const SpectrogramSettings& settings, const WaveChannelInterval& clip,
   int copyBegin, int copyEnd, size_t numPixels, double pixelsPerSecond)
{
   const auto sampleRate = clip.GetRate();
   const int &frequencyGainSetting = settings.frequencyGain;
   const size_t windowSizeSetting = settings.WindowSize();
   const bool autocorrelation =
      settings.algorithm == SpectrogramSettings::algPitchEAC;
   const bool reassignment =
      settings.algorithm == SpectrogramSettings::algReassignment;
   const size_t zeroPaddingFactorSetting = settings.ZeroPaddingFactor();

   // FFT length may be longer than the window of samples that affect results
   // because of zero padding done for increased frequency resolution
   const size_t fftLen = windowSizeSetting * zeroPaddingFactorSetting;
   const auto nBins = settings.NBins();

   const size_t bufferSize = fftLen;
   const size_t scratchSize = reassignment ? 3 * bufferSize : bufferSize;
   std::vector<float> scratch(scratchSize);

   std::vector<float> gainFactors;
   if (!autocorrelation)
      ComputeSpectrogramGainFactors(
         fftLen, sampleRate, frequencyGainSetting, gainFactors);

   //Created shared context for spectrum calculations and reassignment
   const SpectrumContext context{
      settings,
      clip,
      pixelsPerSecond,
      gainFactors
   };

   // Loop over the ranges before and after the copied portion and compute anew.
   // One of the ranges may be empty.
   for (int jj = 0; jj < 2; ++jj) {
      const SpectrumRange range{
      jj == 0 ? 0 : copyEnd,
      jj == 0 ? copyBegin : static_cast<int>(numPixels)
      };

      // Process each pixel column within the specified range,
      // using either wavelet or FFT-based spectrum analysis. 
      ProcessSpectrumRange(range, context, scratch, scratchSize);

      if (reassignment) {
         //handles frequency reassignment contributions near the boundaries of a spectrogram range, then converts the accumulated power values into decibels.
         ProcessReassignment(context, range, scratch);
      }
   }
}

bool WaveClipSpectrumCache::IsCacheValid(
   const SpecCache* cache,
   const WaveChannelInterval& clip,
   double samplesPerPixel,
   const SpectrogramSettings& settings) const
{
   return (cache != nullptr) &&
      cache->leftTrim == clip.GetTrimLeft() &&
      cache->rightTrim == clip.GetTrimRight() &&
      cache->len > 0 &&
      cache->Matches(mDirty, samplesPerPixel, settings);
}


WaveClipSpectrumCache::CacheOverlap
WaveClipSpectrumCache::CalculateCacheOverlap(
   const SpecCache& cache,
   size_t numPixels,
   double t0,
   double sampleRate,
   double stretchRatio,
   double samplesPerPixel)
{
   CacheOverlap overlap;
   WaveClipUIUtilities::findCorrection(
      cache.where, cache.len, numPixels, t0, sampleRate,
      stretchRatio, samplesPerPixel, overlap.oldX0, overlap.correction);

   // Determine which pixel columns can be reused from the old cache.
   const int oldLen = cache.len;

   overlap.copyBegin = std::min(oldLen, std::max(0, -overlap.oldX0));
   overlap.copyEnd = std::min(oldLen, std::max(0, oldLen - overlap.oldX0));
   return overlap;
}

void WaveClipSpectrumCache::InitializeReassignmentBuffer(
   SpecCache& cache,
   int copyBegin,
   int copyEnd,
   size_t numPixels,
   size_t nBins)
{
   // The reused portion must be at one edge of the cache.
   wxASSERT(
      (copyBegin >= 0 && copyEnd == (int)numPixels) ||
      (copyBegin == 0 && copyEnd <= (int)numPixels)
   );

   const int zeroBegin =
      copyBegin > 0 ? 0 : copyEnd - copyBegin;

   const int zeroEnd =
      copyBegin > 0 ? copyBegin : numPixels;

   memset(
      &cache.freq[nBins * zeroBegin],
      0,
      nBins * (zeroEnd - zeroBegin) * sizeof(float));
}


bool WaveClipSpectrumCache::GetSpectrogram(
   const WaveChannelInterval &clip,
   const float*& spectrogram, SpectrogramSettings& settings,
   const sampleCount*& where, size_t numPixels, double t0,
   double pixelsPerSecond)

{
   auto &mSpecCache = mSpecCaches[clip.GetChannelIndex()];

   const auto sampleRate = clip.GetRate();
   const auto stretchRatio = clip.GetStretchRatio();
   const auto samplesPerPixel = sampleRate / pixelsPerSecond / stretchRatio;

   //Trim offset comparison failure forces spectrogram cache rebuild
   //and skip copying "unchanged" data after clip border was trimmed.
   bool match = IsCacheValid(mSpecCache.get(), clip, samplesPerPixel, settings);

   if (match && mSpecCache->start == t0 && mSpecCache->len >= numPixels)
   {
      spectrogram = mSpecCache->freq.data();
      where = mSpecCache->where.data();

      return false;  //hit cache completely
   }

   // Caching is not implemented for reassignment, unless for
   // a complete hit, because of the complications of time reassignment
   if (settings.algorithm == SpectrogramSettings::algReassignment)
      match = false;

   // Free the cache when it won't cause a major stutter.
   // If the window size changed, we know there is nothing to be copied
   // If we zoomed out, or resized, we can give up memory. But not too much -
   // up to 2x extra is needed at the end of the clip to prevent stutter.
   constexpr double kMaxCacheCapacityRatio = 2.1;
   if (mSpecCache->freq.capacity() > kMaxCacheCapacityRatio * mSpecCache->freq.size() ||
       mSpecCache->windowSize*mSpecCache->zeroPaddingFactor <
       settings.WindowSize()*settings.ZeroPaddingFactor())
   {
      match = false;
      mSpecCache = std::make_unique<SpecCache>();
   }

  
   struct CacheOverlap overlap;
   overlap.oldX0 = 0;
   overlap.correction = 0.0;
   overlap.copyBegin = 0;
   overlap.copyBegin = 0;

   if (match) {
      overlap = CalculateCacheOverlap(
         *mSpecCache, numPixels, t0,
         sampleRate, stretchRatio, samplesPerPixel);
   }

   mSpecCache->leftTrim = clip.GetTrimLeft();
   mSpecCache->rightTrim = clip.GetTrimRight();
   auto nBins = settings.NBins();

   // Optimization: if the old cache is good and overlaps
   // with the current one, re-use as much of the cache as
   // possible
   if (overlap.copyEnd > overlap.copyBegin)
   {
      // memmove is required since dst/src overlap
      memmove(&mSpecCache->freq[nBins * overlap.copyBegin],
               &mSpecCache->freq[nBins * (overlap.copyBegin + overlap.oldX0)],
               nBins * (overlap.copyEnd - overlap.copyBegin) * sizeof(float));
   }

   // Resize the cache, keep the contents unchanged.
   mSpecCache->Grow(numPixels, settings, samplesPerPixel, t0);

   // Reassignment accumulates, so it needs a zeroed buffer
   if (settings.algorithm == SpectrogramSettings::algReassignment)
   {
      InitializeReassignmentBuffer(
         *mSpecCache, overlap.copyBegin, overlap.copyEnd, numPixels, nBins);
   }

   // purposely offset the display 1/2 sample to the left (as compared
   // to waveform display) to properly center response of the FFT
   constexpr auto addBias = true;
   WaveClipUIUtilities::fillWhere(
      mSpecCache->where, numPixels, addBias, overlap.correction, t0, sampleRate,
      stretchRatio, samplesPerPixel);

   mSpecCache->Populate(settings, clip, overlap.copyBegin, overlap.copyEnd, numPixels, pixelsPerSecond);
   mSpecCache->dirty = mDirty;
   spectrogram = mSpecCache->freq.data();
   where = mSpecCache->where.data();

   return true;
}

WaveClipSpectrumCache::WaveClipSpectrumCache(size_t nChannels)
   : mSpecCaches(nChannels)
   , mSpecPxCaches(nChannels)
{
   for (auto &pCache : mSpecCaches)
      pCache = std::make_unique<SpecCache>();
}

WaveClipSpectrumCache::~WaveClipSpectrumCache()
{
}

std::unique_ptr<WaveClipListener> WaveClipSpectrumCache::Clone() const
{
   // Don't need to copy contents
   return std::make_unique<WaveClipSpectrumCache>(mSpecCaches.size());
}

static WaveClip::Attachments::RegisteredFactory sKeyS{ [](WaveClip &clip){
   return std::make_unique<WaveClipSpectrumCache>(clip.NChannels());
} };

WaveClipSpectrumCache &
WaveClipSpectrumCache::Get(const WaveChannelInterval &clip)
{
   return const_cast<WaveClip&>(clip.GetClip()) // Consider it mutable data
      .Attachments::Get< WaveClipSpectrumCache >( sKeyS );
}

void WaveClipSpectrumCache::MarkChanged() noexcept
{
   ++mDirty;
}

void WaveClipSpectrumCache::Invalidate()
{
   // Invalidate the spectrum display cache
   for (auto &pCache : mSpecCaches)
      pCache = std::make_unique<SpecCache>();
}

void WaveClipSpectrumCache::MakeStereo(WaveClipListener &&other, bool /*aligned*/)
{
   auto* pOther = dynamic_cast<WaveClipSpectrumCache *>(&other);
   assert(pOther); // precondition
   mSpecCaches.push_back(move(pOther->mSpecCaches[0]));
   mSpecPxCaches.push_back(move(pOther->mSpecPxCaches[0]));
}

void WaveClipSpectrumCache::SwapChannels()
{
   mSpecCaches.resize(2);
   std::swap(mSpecCaches[0], mSpecCaches[1]);
   mSpecPxCaches.resize(2);
   std::swap(mSpecPxCaches[0], mSpecPxCaches[1]);
}

void WaveClipSpectrumCache::Erase(size_t index)
{
   if (index < mSpecCaches.size())
      mSpecCaches.erase(mSpecCaches.begin() + index);
   if (index < mSpecPxCaches.size())
      mSpecPxCaches.erase(mSpecPxCaches.begin() + index);
}
