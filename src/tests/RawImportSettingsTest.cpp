/**********************************************************************

  Audacity: A Digital Audio Editor

  RawImportSettingsTest.cpp

  Unit tests for import/RawImportSettings.h: settings validation,
  the byte-order table and channel de-interleaving.

**********************************************************************/

#include "RawImportSettings.h"

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

// Default settings match the original static members' initial values
void TestDefaults()
{
   const RawImportSettings s;
   Check("default encoding is raw 16-bit PCM, CPU byte order",
      s.encoding == (SF_FORMAT_RAW | SF_ENDIAN_CPU | SF_FORMAT_PCM_16));
   Check("default channels is 1", s.channels == 1);
   Check("default offset is 0", s.offset == 0);
   Check("default rate is 0 (use project rate)", s.rate == 0);
   Check("default percent is 100", s.percent == 100.0);
}

RawImportSettings With(unsigned channels, long long offset,
   double percent, double rate)
{
   RawImportSettings s;
   s.channels = channels;
   s.offset = offset;
   s.percent = percent;
   s.rate = rate;
   return s;
}

// Clamped() must apply exactly the limits the old OnOK() applied
void TestClamped()
{
   Check("channels 0 -> 1", Clamped(With(0, 0, 100, 44100)).channels == 1);
   Check("channels 1 kept", Clamped(With(1, 0, 100, 44100)).channels == 1);
   Check("channels 16 kept", Clamped(With(16, 0, 100, 44100)).channels == 16);
   Check("channels 17 -> 1", Clamped(With(17, 0, 100, 44100)).channels == 1);

   Check("negative offset -> 0", Clamped(With(1, -5, 100, 44100)).offset == 0);
   Check("large offset kept (above 2^31)",
      Clamped(With(1, 5000000000LL, 100, 44100)).offset == 5000000000LL);

   Check("percent -1 -> 0", Clamped(With(1, 0, -1, 44100)).percent == 0.0);
   Check("percent 50 kept", Clamped(With(1, 0, 50, 44100)).percent == 50.0);
   Check("percent 150 -> 100", Clamped(With(1, 0, 150, 44100)).percent == 100.0);

   Check("rate 50 -> 100", Clamped(With(1, 0, 100, 50)).rate == MinRawRate);
   Check("rate 44100 kept", Clamped(With(1, 0, 100, 44100)).rate == 44100.0);
   Check("rate 500000 -> 384000",
      Clamped(With(1, 0, 100, 500000)).rate == MaxRawRate);

   RawImportSettings s = With(2, 10, 100, 44100);
   s.encoding = SF_FORMAT_PCM_24 | SF_ENDIAN_BIG;
   Check("encoding is not changed", Clamped(s).encoding == s.encoding);
}

// The table must reproduce the old "choice * 0x10000000" arithmetic and
// getEndianChoice() must be its inverse
void TestEndianTable()
{
   for (int choice = 0; choice < 4; ++choice) {
      const auto name = std::to_string(choice);
      Check("choice " + name + " equals old formula",
         kEndianFormats[choice] == choice * 0x10000000);
      Check("choice " + name + " round-trips",
         getEndianChoice(SF_FORMAT_PCM_16 | kEndianFormats[choice]) == choice);
      Check("choice " + name + ": | gives the same as old +",
         (SF_FORMAT_PCM_16 | kEndianFormats[choice]) ==
         (SF_FORMAT_PCM_16 + choice * 0x10000000));
   }
   Check("subtype bits are ignored",
      getEndianChoice(SF_FORMAT_RAW | SF_FORMAT_FLOAT | SF_ENDIAN_BIG) == 2);
}

void TestDeinterleave()
{
   const short stereo[] = { 1, 2, 3, 4, 5, 6 };
   std::vector<short> out(3);
   DeinterleaveChannel(stereo, out.data(), 3, 2, 0);
   Check("stereo short, left channel", out == std::vector<short>{ 1, 3, 5 });
   DeinterleaveChannel(stereo, out.data(), 3, 2, 1);
   Check("stereo short, right channel", out == std::vector<short>{ 2, 4, 6 });

   const float three[] = { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f };
   std::vector<float> outF(2);
   DeinterleaveChannel(three, outF.data(), 2, 3, 2);
   Check("3-channel float, third channel",
      outF == std::vector<float>{ 0.3f, 0.6f });

   const float mono[] = { 1.f, 2.f, 3.f };
   std::vector<float> outM(3);
   DeinterleaveChannel(mono, outM.data(), 3, 1, 0);
   Check("mono is copied unchanged", outM == std::vector<float>{ 1.f, 2.f, 3.f });

   std::vector<short> untouched{ 7 };
   DeinterleaveChannel(stereo, untouched.data(), 0, 2, 0);
   Check("zero frames writes nothing", untouched[0] == 7);
}

} // namespace

int main()
{
   TestDefaults();
   TestClamped();
   TestEndianTable();
   TestDeinterleave();
   std::printf("\n%s\n", gFailures ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
   return gFailures ? 1 : 0;
}
