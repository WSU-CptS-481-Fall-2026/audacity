/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  @file DtmfToneTest.cpp
  @brief Standalone tests for DtmfTone.h: the keypad table against the
  DTMF standard, letter mapping, fades, amplitude, phase continuity and
  the frequencies actually present in the generated tone.

**********************************************************************/

#include "DtmfTone.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int gFailures = 0;

void Check(const std::string& name, bool ok)
{
   if (!ok)
      ++gFailures;
   std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name.c_str());
}

std::string Name(char32_t c)
{
   if (c >= 0x20 && c < 0x7F)
      return std::string("'") + static_cast<char>(c) + "'";
   return "U+" + std::to_string(static_cast<unsigned long>(c));
}

constexpr float kRate = 44100.f;
constexpr float kAmplitude = 0.8f;

std::vector<float> Generate(char32_t symbol, std::size_t len,
   long long last, long long total, float fs = kRate)
{
   std::vector<float> buffer(len);
   DtmfTone::Generate(buffer.data(), len, fs, symbol, last, total, kAmplitude);
   return buffer;
}

// Power of one frequency in a signal (Goertzel algorithm)
double PowerAt(const std::vector<float>& x, double hz, double fs)
{
   const double coeff = 2 * std::cos(2 * 3.14159265358979323846 * hz / fs);
   double s1 = 0, s2 = 0;
   for (float v : x) {
      const double s = v + coeff * s1 - s2;
      s2 = s1;
      s1 = s;
   }
   return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

// Part 1: every keypad symbol maps to the standard DTMF frequency pair
void TestKeypadTable()
{
   const struct { char32_t symbol; float low; float high; } expected[] = {
      { U'1', 697, 1209 }, { U'2', 697, 1336 }, { U'3', 697, 1477 }, { U'A', 697, 1633 },
      { U'4', 770, 1209 }, { U'5', 770, 1336 }, { U'6', 770, 1477 }, { U'B', 770, 1633 },
      { U'7', 852, 1209 }, { U'8', 852, 1336 }, { U'9', 852, 1477 }, { U'C', 852, 1633 },
      { U'*', 941, 1209 }, { U'0', 941, 1336 }, { U'#', 941, 1477 }, { U'D', 941, 1633 },
   };
   for (const auto& e : expected) {
      const auto f = DtmfTone::FrequenciesFor(e.symbol);
      Check("keypad " + Name(e.symbol) + " -> " +
               std::to_string(static_cast<int>(e.low)) + " + " +
               std::to_string(static_cast<int>(e.high)) + " Hz",
         f.IsValid() && f.low == e.low && f.high == e.high);
   }
}

// Part 2: lowercase letters use the digit printed on their phone key
void TestLetters()
{
   const char* keys[] = { "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz" };
   for (int k = 0; k < 8; ++k) {
      const char32_t digit = U'2' + k;
      bool ok = true;
      for (const char* p = keys[k]; *p; ++p) {
         const auto f = DtmfTone::FrequenciesFor(static_cast<char32_t>(*p));
         const auto d = DtmfTone::FrequenciesFor(digit);
         ok = ok && f.low == d.low && f.high == d.high;
      }
      Check(std::string("letters ") + keys[k] + " use key " + Name(digit), ok);
   }

   const auto upperA = DtmfTone::FrequenciesFor(U'A');
   Check("uppercase 'A' is the extra tone, not key 2",
      upperA.high == 1633.f);
}

// Part 3: anything else is not a DTMF symbol
void TestInvalidSymbols()
{
   for (char32_t c : { U' ', U'!', U'E', U'Z', U'+', U'\0',
      char32_t{ 0x00E9 } /* é */, char32_t{ 0x4E2D } /* 中 */ })
      Check(Name(c) + " is not a DTMF symbol",
         !DtmfTone::FrequenciesFor(c).IsValid());

   const auto silent = Generate(U'!', 2000, 0, 2000);
   bool allZero = true;
   for (float v : silent)
      allZero = allZero && v == 0.f;
   Check("invalid symbol generates silence", allZero);
}

// Part 4: the tone stays within the requested amplitude
void TestAmplitude()
{
   const auto tone = Generate(U'5', 44100, 0, 44100);
   float peak = 0;
   for (float v : tone)
      peak = std::max(peak, std::fabs(v));
   Check("peak does not exceed the amplitude", peak <= kAmplitude);
   Check("peak reaches most of the amplitude", peak > 0.9f * kAmplitude);
}

// Part 5: 4 ms fades at both ends, and nothing in between is changed
void TestFades()
{
   const std::size_t len = 44100;
   const auto tone = Generate(U'1', len, 0, len);

   std::vector<float> unfaded(len);
   DtmfTone::Detail::RenderSines(unfaded.data(), len, kRate,
      DtmfTone::FrequenciesFor(U'1'), 0, kAmplitude);

   const auto fade = static_cast<std::size_t>(kRate / DtmfTone::kFadesPerSecond);
   Check("fade length is 4 ms (176 samples at 44.1 kHz)", fade == 176);

   Check("first sample is silent", tone[0] == 0.f);
   Check("last sample is nearly silent",
      std::fabs(tone[len - 1]) <= 0.01f * kAmplitude);

   bool fadeInScaled = true;
   for (std::size_t i = 1; i <= fade; ++i)
      fadeInScaled = fadeInScaled && std::fabs(tone[i]) <= std::fabs(unfaded[i]);
   Check("fade-in only reduces the level", fadeInScaled);

   bool middleUnchanged = true;
   for (std::size_t i = fade + 1; i + fade + 1 < len; ++i)
      middleUnchanged = middleUnchanged && tone[i] == unfaded[i];
   Check("middle of the tone is not changed by the fades", middleUnchanged);

   const auto shortBlock = Generate(U'1', 10, 0, 1000);
   Check("fade never runs past a short block", shortBlock[0] == 0.f);
}

// Part 6: a tone generated in blocks equals the same tone generated at once
void TestBlocks()
{
   const std::size_t total = 30000, block = 4096;
   const auto whole = Generate(U'9', total, 0, total);

   std::vector<float> pieces;
   for (std::size_t start = 0; start < total; start += block) {
      const auto len = std::min(block, total - start);
      const auto part = Generate(U'9', len, static_cast<long long>(start), total);
      pieces.insert(pieces.end(), part.begin(), part.end());
   }

   const auto fade = static_cast<std::size_t>(kRate / DtmfTone::kFadesPerSecond);
   bool same = pieces.size() == whole.size();
   for (std::size_t i = fade + 1; same && i + block < total; ++i)
      same = pieces[i] == whole[i];
   Check("phase is continuous across blocks", same);
}

// Part 7: the generated sound contains exactly the two expected frequencies
void TestFrequencyContent()
{
   const auto tone = Generate(U'5', 44100, 1000, 1000000); // no fades
   const double low = PowerAt(tone, 770, kRate);
   const double high = PowerAt(tone, 1336, kRate);
   double others = 0;
   for (double hz : { 697.0, 852.0, 941.0, 1209.0, 1477.0, 1633.0 })
      others = std::max(others, PowerAt(tone, hz, kRate));
   Check("key '5' contains 770 Hz", low > 1000 * others);
   Check("key '5' contains 1336 Hz", high > 1000 * others);

   const auto at8k = Generate(U'#', 8000, 1000, 1000000, 8000.f);
   Check("key '#' at 8000 Hz contains 941 + 1477 Hz",
      PowerAt(at8k, 941, 8000) > 1000 * PowerAt(at8k, 852, 8000) &&
      PowerAt(at8k, 1477, 8000) > 1000 * PowerAt(at8k, 1336, 8000));
}

} // namespace

int main()
{
   TestKeypadTable();
   TestLetters();
   TestInvalidSymbols();
   TestAmplitude();
   TestFades();
   TestBlocks();
   TestFrequencyContent();
   std::printf("\n%s\n", gFailures ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
   return gFailures ? 1 : 0;
}
