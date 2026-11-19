/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  @file DtmfToneEquivalenceTests.cpp
  @brief Proves the table-driven DtmfTone.h is bit-for-bit equivalent to the
  original switch-based implementation (frozen in DtmfToneLegacyReference.h),
  and checks the lookup table at compile time.

**********************************************************************/
#include "DtmfTone.h"
#include "DtmfToneLegacyReference.h"

#include <catch2/catch.hpp>

#include <cstring>
#include <vector>

// --- Compile-time checks of the lookup table (independent of the oracle) ----
static_assert(DtmfTone::FrequenciesFor(U'1').low == 697.f);
static_assert(DtmfTone::FrequenciesFor(U'1').high == 1209.f);
static_assert(DtmfTone::FrequenciesFor(U'0').low == 941.f);
static_assert(DtmfTone::FrequenciesFor(U'0').high == 1336.f);
static_assert(DtmfTone::FrequenciesFor(U'#').high == 1477.f);
static_assert(DtmfTone::FrequenciesFor(U'D').low == 941.f);
static_assert(DtmfTone::FrequenciesFor(U'D').high == 1633.f);
// 'a' is on the 2 key, 'z' on the 9 key; uppercase letters other than A-D
// are not symbols at all
static_assert(DtmfTone::FrequenciesFor(U'a').low == 697.f);
static_assert(DtmfTone::FrequenciesFor(U'a').high == 1336.f);
static_assert(DtmfTone::FrequenciesFor(U'z').low == 852.f);
static_assert(DtmfTone::FrequenciesFor(U'z').high == 1477.f);
static_assert(!DtmfTone::FrequenciesFor(U'E').IsValid());
static_assert(!DtmfTone::FrequenciesFor(U' ').IsValid());
static_assert(!DtmfTone::FrequenciesFor(char32_t(0x10FFFF)).IsValid());

namespace
{
// All symbols the effect accepts (same list as DtmfBase::kSymbols)
constexpr char kSymbols[] = "0123456789*#ABCDabcdefghijklmnopqrstuvwxyz";

struct Block {
   size_t len;
   long long last;
   long long total;
};

bool SameBits(const std::vector<float>& a, const std::vector<float>& b)
{
   return a.size() == b.size() &&
          std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

std::vector<float> RenderOriginal(
   wchar_t tone, float fs, const Block& b, float amplitude)
{
   std::vector<float> out(b.len, 0.f);
   DtmfLegacy::MakeDtmfTone(
      out.data(), b.len, fs, tone, sampleCount{ b.last },
      sampleCount{ b.total }, amplitude);
   return out;
}

std::vector<float> RenderNew(
   wchar_t tone, float fs, const Block& b, float amplitude)
{
   std::vector<float> out(b.len, 0.f);
   DtmfTone::Generate(
      out.data(), b.len, fs, static_cast<char32_t>(tone), b.last, b.total,
      amplitude);
   return out;
}
} // namespace

TEST_CASE("DtmfTone: every character a wxChar can hold behaves exactly as "
          "before", "[dtmf][equivalence]")
{
   // Covers supported symbols, unsupported ASCII, Latin-1, the rest of the
   // Basic Multilingual Plane, and the maximum wchar_t value.
   const Block block { 16, 3, 100 };
   size_t mismatches = 0;
   for (unsigned c = 0; c <= 0xFFFF; ++c) {
      const auto tone = static_cast<wchar_t>(c);
      if (!SameBits(
             RenderOriginal(tone, 44100.f, block, 0.8f),
             RenderNew(tone, 44100.f, block, 0.8f)))
         ++mismatches;
   }
   CHECK(mismatches == 0);
}

TEST_CASE("DtmfTone: every symbol, rate, amplitude and block position is "
          "bit-identical to the original", "[dtmf][equivalence]")
{
   const float rates[] = { 8000.f, 11025.f, 22050.f, 44100.f, 48000.f,
                           96000.f, 192000.f };
   const float amplitudes[] = { 0.001f, 0.5f, 0.8f, 1.0f };
   const Block blocks[] = {
      // whole tone in one block, around the fade length (fs / 250 samples)
      { 1, 0, 1 }, { 2, 0, 2 }, { 31, 0, 31 }, { 32, 0, 32 }, { 33, 0, 33 },
      { 100, 0, 100 }, { 4096, 0, 4096 },
      // first, middle and last blocks of a longer tone
      { 1000, 0, 4000 }, { 1000, 1000, 4000 }, { 1500, 2500, 4000 },
      { 5, 3995, 4000 }, { 1, 3999, 4000 }, { 50, 3950, 4000 },
      // block exactly at, and one past, the end (the effect can over-run a
      // tone by one sample when it redistributes leftover samples)
      { 101, 0, 100 }, { 2, 99, 100 },
      // far into a very long tone: precision of the phase argument
      { 512, 10'000'000'000LL, 10'000'000'512LL },  // last block, fades out
      { 512, 10'000'000'000LL, 20'000'000'000LL },  // middle block, no fade
      { 512, 1LL << 40, (1LL << 40) + 512 },
   };

   size_t renders = 0, mismatches = 0;
   for (const char* s = kSymbols; *s; ++s)
      for (float fs : rates)
         for (float amplitude : amplitudes)
            for (const auto& block : blocks) {
               const auto tone = static_cast<wchar_t>(*s);
               ++renders;
               if (!SameBits(
                      RenderOriginal(tone, fs, block, amplitude),
                      RenderNew(tone, fs, block, amplitude)))
                  ++mismatches;
            }
   CHECK(renders > 20000);
   CHECK(mismatches == 0);
}

TEST_CASE("DtmfTone: symbols outside the keypad yield silence and are "
          "reported invalid", "[dtmf][equivalence]")
{
   for (char32_t c : { U' ', U'-', U'E', U'G', U'Z', U'@', char32_t(0xE9),
                       char32_t(0x1F600), char32_t(0x10FFFF) }) {
      CHECK(!DtmfTone::FrequenciesFor(c).IsValid());
      std::vector<float> out(64, 1.f);
      DtmfTone::Generate(out.data(), out.size(), 8000.f, c, 0, 64, 1.f);
      bool silent = true;
      for (float v : out)
         silent = silent && v == 0.f;
      CHECK(silent);
   }
}

TEST_CASE("DtmfTone: lookup agrees with the 16-key grid and the telephone "
          "letters", "[dtmf][equivalence]")
{
   // The 16 keypad symbols: each (row, column) pair occurs exactly once
   const char grid[] = "123A456B789C*0#D";
   const float rows[] = { 697.f, 770.f, 852.f, 941.f };
   const float cols[] = { 1209.f, 1336.f, 1477.f, 1633.f };
   for (int i = 0; i < 16; ++i) {
      const auto f = DtmfTone::FrequenciesFor(static_cast<char32_t>(grid[i]));
      CHECK(f.low == rows[i / 4]);
      CHECK(f.high == cols[i % 4]);
   }
   // Letters share the frequencies of the digit on their phone key
   const char* digitOf = "22233344455566677778889999";
   for (int i = 0; i < 26; ++i) {
      const auto letter = DtmfTone::FrequenciesFor(static_cast<char32_t>('a' + i));
      const auto digit = DtmfTone::FrequenciesFor(static_cast<char32_t>(digitOf[i]));
      CHECK(letter.low == digit.low);
      CHECK(letter.high == digit.high);
   }
}
