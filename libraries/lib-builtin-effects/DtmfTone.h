/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  @file DtmfTone.h
  @brief Dual-tone multi-frequency (DTMF) tone synthesis, free of GUI and
  effect-framework dependencies so that it can be unit tested in isolation.

  Replaces the two 42-label `switch` statements of the former
  DtmfBase::MakeDtmfTone with a data table. Results are bit-identical to the
  previous implementation (see tests/DtmfToneEquivalenceTests.cpp).

**********************************************************************/
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace DtmfTone
{
//! A DTMF symbol is the sum of one tone from each frequency group
struct Frequencies
{
   float low {};  //!< Hz, from the row group; 0 if not a DTMF symbol
   float high {}; //!< Hz, from the column group; 0 if not a DTMF symbol

   constexpr bool IsValid() const noexcept
   {
      return low != 0.f && high != 0.f;
   }
};

//! Fades at both ends last 1 / kFadesPerSecond seconds (4 ms), which removes
//! the clicks that an abrupt start or stop of a tone would produce.
inline constexpr double kFadesPerSecond = 250.0;

namespace Detail
{
inline constexpr double kTwoPi = 2 * 3.14159265358979323846;

/*
                1209 Hz 1336 Hz 1477 Hz 1633 Hz

                        ABC     DEF
   697 Hz          1       2       3       A

                GHI     JKL     MNO
   770 Hz          4       5       6       B

                PQRS     TUV     WXYZ
   852 Hz          7       8       9       C

                        oper
   941 Hz          *       0       #       D
*/
inline constexpr std::array<float, 4> kRowHz { 697.f, 770.f, 852.f, 941.f };
inline constexpr std::array<float, 4> kColumnHz { 1209.f, 1336.f, 1477.f,
                                                  1633.f };
inline constexpr std::array<std::array<char32_t, 4>, 4> kKeypad { {
   { U'1', U'2', U'3', U'A' },
   { U'4', U'5', U'6', U'B' },
   { U'7', U'8', U'9', U'C' },
   { U'*', U'0', U'#', U'D' },
} };

//! Digit printed on the telephone key carrying each lowercase letter a..z:
//! abc=2 def=3 ghi=4 jkl=5 mno=6 pqrs=7 tuv=8 wxyz=9. Uppercase A-D are the
//! "military" extra tones and are never treated as letters.
inline constexpr char kLetterToDigit[] = "22233344455566677778889999";
static_assert(sizeof(kLetterToDigit) - 1 == 26);

constexpr char32_t ToKeypadSymbol(char32_t symbol) noexcept
{
   return (symbol >= U'a' && symbol <= U'z') ?
             static_cast<char32_t>(kLetterToDigit[symbol - U'a']) :
             symbol;
}
} // namespace Detail

//! Look up the frequency pair of a symbol; {0, 0} if it is not supported
constexpr Frequencies FrequenciesFor(char32_t symbol) noexcept
{
   const auto key = Detail::ToKeypadSymbol(symbol);
   for (std::size_t row = 0; row < Detail::kKeypad.size(); ++row)
      for (std::size_t column = 0; column < Detail::kKeypad[row].size();
           ++column)
         if (Detail::kKeypad[row][column] == key)
            return { Detail::kRowHz[row], Detail::kColumnHz[column] };
   return {};
}

namespace Detail
{
//! Number of samples of each fade; never longer than the block itself
inline double FadeLength(std::size_t len, float fs)
{
   return std::min(static_cast<double>(len), fs / kFadesPerSecond);
}

inline void RenderSines(
   float* buffer, std::size_t len, float fs, Frequencies frequencies,
   long long last, float amplitude)
{
   // Precalculate the phase advance per sample of each sine
   const double lowStep = kTwoPi / fs * frequencies.low;
   const double highStep = kTwoPi / fs * frequencies.high;

   // `last` keeps the phase continuous from one block to the next
   for (std::size_t i = 0; i < len; ++i) {
      const double n = static_cast<double>(static_cast<long long>(i) + last);
      buffer[i] = static_cast<float>(
         amplitude * 0.5 * (std::sin(lowStep * n) + std::sin(highStep * n)));
   }
}

inline void ApplyFadeIn(float* buffer, std::size_t len, float fs)
{
   const double fade = FadeLength(len, fs);
   for (std::size_t i = 0; i < fade; ++i)
      buffer[i] = static_cast<float>(buffer[i] * (i / fade));
}

inline void ApplyFadeOut(float* buffer, std::size_t len, float fs)
{
   // We are in the last block: back up by the fade length from its end
   const double fade = FadeLength(len, fs);
   const auto offset = static_cast<std::size_t>(len - fade);
   for (std::size_t i = 0; i < fade; ++i)
      buffer[i + offset] = static_cast<float>(buffer[i + offset] * (1 - (i / fade)));
}
} // namespace Detail

/*!
 Write `len` samples of one tone to `buffer`, as one block of a tone that is
 `total` samples long, whose first sample of this block is sample `last`.

 @param buffer destination for `len` samples
 @param fs sample rate
 @param frequencies from FrequenciesFor(); {0, 0} yields silence
 @param last index within the tone of buffer[0]; 0 also fades the tone in
 @param total length of the whole tone; the block that reaches it fades out
 */
inline void Generate(
   float* buffer, std::size_t len, float fs, Frequencies frequencies,
   long long last, long long total, float amplitude)
{
   Detail::RenderSines(buffer, len, fs, frequencies, last, amplitude);
   if (last == 0)
      Detail::ApplyFadeIn(buffer, len, fs);
   if (last >= total - static_cast<long long>(len))
      Detail::ApplyFadeOut(buffer, len, fs);
}

//! Convenience overload that looks the symbol up first
inline void Generate(
   float* buffer, std::size_t len, float fs, char32_t symbol, long long last,
   long long total, float amplitude)
{
   Generate(buffer, len, fs, FrequenciesFor(symbol), last, total, amplitude);
}
} // namespace DtmfTone
