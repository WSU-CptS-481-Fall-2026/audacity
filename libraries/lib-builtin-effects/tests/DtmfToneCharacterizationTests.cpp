/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  @file DtmfToneCharacterizationTests.cpp
  @brief Characterization tests pinning the observable behaviour of the
  original DtmfBase::MakeDtmfTone (via the frozen oracle) before refactoring.

  The expectations are written from the DTMF keypad standard and measured
  with a Goertzel detector, so they do not depend on how any implementation
  chooses its frequencies internally.

**********************************************************************/
#include "DtmfToneLegacyReference.h"

#include <catch2/catch.hpp>

#include <cmath>
#include <vector>

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr float kFs = 8000.f;
constexpr size_t kBlock = 2000;      // samples analysed
constexpr long long kTotal = 4000;   // pretend tone length
constexpr long long kMiddle = 1000;  // block start that has neither fade

constexpr double kRows[4] = { 697, 770, 852, 941 };
constexpr double kCols[4] = { 1209, 1336, 1477, 1633 };

//! Amplitude of the sinusoid at `freq` present in `x` (Goertzel algorithm)
double AmplitudeAt(const std::vector<float>& x, double fs, double freq)
{
   const double w = 2 * kPi * freq / fs;
   const double coeff = 2 * std::cos(w);
   double s1 = 0, s2 = 0;
   for (float v : x) {
      const double s0 = v + coeff * s1 - s2;
      s2 = s1;
      s1 = s0;
   }
   const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
   return 2 * std::sqrt(power < 0 ? 0 : power) / double(x.size());
}

std::vector<float> RenderMiddle(wchar_t tone, float amplitude = 1.f)
{
   std::vector<float> buf(kBlock, 0.f);
   DtmfLegacy::MakeDtmfTone(
      buf.data(), buf.size(), kFs, tone, sampleCount{ kMiddle },
      sampleCount{ kTotal }, amplitude);
   return buf;
}

struct Key {
   wchar_t symbol;
   int row;
   int col;
};

// Keypad layout from the DTMF standard (rows = low group, cols = high group)
const Key kKeypad[] = {
   { L'1', 0, 0 }, { L'2', 0, 1 }, { L'3', 0, 2 }, { L'A', 0, 3 },
   { L'4', 1, 0 }, { L'5', 1, 1 }, { L'6', 1, 2 }, { L'B', 1, 3 },
   { L'7', 2, 0 }, { L'8', 2, 1 }, { L'9', 2, 2 }, { L'C', 2, 3 },
   { L'*', 3, 0 }, { L'0', 3, 1 }, { L'#', 3, 2 }, { L'D', 3, 3 },
};

// Letters follow the telephone keypad: abc=2 def=3 ghi=4 jkl=5 mno=6
// pqrs=7 tuv=8 wxyz=9 (lowercase only; uppercase A-D are the extra tones)
wchar_t DigitForLetter(wchar_t c)
{
   const char* map = "22233344455566677778889999";
   return static_cast<wchar_t>(map[c - L'a']);
}

//! Check that exactly the expected row and column frequency are present
void ExpectDtmfPair(wchar_t tone, int row, int col, int& failures)
{
   const auto x = RenderMiddle(tone);
   for (int r = 0; r < 4; ++r) {
      const double a = AmplitudeAt(x, kFs, kRows[r]);
      const bool expected = (r == row);
      // each of the two summed sinusoids has amplitude 0.5 * amplitude
      if (expected ? !(std::abs(a - 0.5) < 0.03) : !(a < 0.03))
         ++failures;
   }
   for (int c = 0; c < 4; ++c) {
      const double a = AmplitudeAt(x, kFs, kCols[c]);
      const bool expected = (c == col);
      if (expected ? !(std::abs(a - 0.5) < 0.03) : !(a < 0.03))
         ++failures;
   }
}
} // namespace

TEST_CASE("Original MakeDtmfTone: the 16 keypad symbols produce the standard "
          "frequency pairs", "[dtmf][characterization]")
{
   int failures = 0;
   for (const auto& k : kKeypad)
      ExpectDtmfPair(k.symbol, k.row, k.col, failures);
   CHECK(failures == 0);
}

TEST_CASE("Original MakeDtmfTone: lowercase letters behave like their "
          "telephone keypad digit", "[dtmf][characterization]")
{
   int failures = 0;
   for (wchar_t c = L'a'; c <= L'z'; ++c) {
      const wchar_t digit = DigitForLetter(c);
      for (const auto& k : kKeypad)
         if (k.symbol == digit)
            ExpectDtmfPair(c, k.row, k.col, failures);
   }
   CHECK(failures == 0);
}

TEST_CASE("Original MakeDtmfTone: unsupported symbols are silent",
          "[dtmf][characterization]")
{
   const wchar_t unsupported[] = { L' ', L'-', L'E', L'G', L'Z', L'@',
                                   0x00E9, 0x0100, 0xFFFF, 0 };
   for (wchar_t c : unsupported) {
      const auto x = RenderMiddle(c);
      bool silent = true;
      for (float v : x)
         silent = silent && (v == 0.f);
      CHECK(silent);
   }
}

TEST_CASE("Original MakeDtmfTone: output scales linearly with amplitude",
          "[dtmf][characterization]")
{
   const auto full = RenderMiddle(L'5', 1.f);
   const auto half = RenderMiddle(L'5', 0.5f);
   double worst = 0;
   for (size_t i = 0; i < full.size(); ++i)
      worst = std::max(worst, std::abs(double(full[i]) * 0.5 - half[i]));
   CHECK(worst < 1e-6);
}

TEST_CASE("Original MakeDtmfTone: fades in at the start and out at the end",
          "[dtmf][characterization]")
{
   constexpr size_t kLen = 500;
   constexpr size_t kFade = 32; // fs / 250 = 8000 / 250
   std::vector<float> start(kLen, 0.f), end(kLen, 0.f);
   DtmfLegacy::MakeDtmfTone(start.data(), start.size(), kFs, L'5',
      sampleCount{ 0 }, sampleCount{ kTotal }, 1.f);
   DtmfLegacy::MakeDtmfTone(end.data(), end.size(), kFs, L'5',
      sampleCount{ kTotal - (long long) kLen }, sampleCount{ kTotal }, 1.f);

   // Independent model: sum of two sines (770 Hz + 1336 Hz) times a linear
   // 32-sample ramp at the start and at the end of the whole tone.
   const auto raw = [](long long n) {
      return 0.5 * (std::sin(2 * kPi * 770 * n / kFs) +
                    std::sin(2 * kPi * 1336 * n / kFs));
   };
   double worstStart = 0, worstEnd = 0;
   for (size_t i = 0; i < kLen; ++i) {
      const double gain = i < kFade ? double(i) / kFade : 1.0;
      worstStart = std::max(worstStart, std::abs(start[i] - gain * raw(i)));
   }
   for (size_t i = 0; i < kLen; ++i) {
      const long long n = kTotal - (long long) kLen + (long long) i;
      const size_t fromEnd = kLen - 1 - i; // 0 for the final sample
      const double gain = fromEnd < kFade ? 1.0 - double(kFade - 1 - fromEnd) / kFade : 1.0;
      worstEnd = std::max(worstEnd, std::abs(end[i] - gain * raw(n)));
   }
   CHECK(worstStart < 1e-5);
   CHECK(worstEnd < 1e-5);
}

TEST_CASE("Original MakeDtmfTone: rendering in blocks equals rendering at "
          "once", "[dtmf][characterization]")
{
   // 'last' exists so that block boundaries do not introduce phase errors.
   const size_t total = 4000;
   std::vector<float> whole(total, 0.f), parts(total, 0.f);
   DtmfLegacy::MakeDtmfTone(whole.data(), total, 44100.f, L'q',
      sampleCount{ 0 }, sampleCount{ (long long) total }, 0.8f);

   const size_t cuts[] = { 0, 1000, 2500, total };
   for (int i = 0; i < 3; ++i)
      DtmfLegacy::MakeDtmfTone(parts.data() + cuts[i], cuts[i + 1] - cuts[i],
         44100.f, L'q', sampleCount{ (long long) cuts[i] },
         sampleCount{ (long long) total }, 0.8f);

   bool identical = true;
   for (size_t i = 0; i < total; ++i)
      identical = identical && (whole[i] == parts[i]);
   CHECK(identical);
}
