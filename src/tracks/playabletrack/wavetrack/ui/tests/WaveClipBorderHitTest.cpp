/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  WaveClipBorderHitTest.cpp

  Characterization tests for the border selection rules that used to live
  inside WaveClipAdjustBorderHandle::HitAnywhere.

**********************************************************************/

#include <catch2/catch.hpp>

#include "../WaveClipBorderHit.h"

using namespace WaveClipBorderHit;

namespace {
// Asserts that a border was hit, and which one
void RequireHit(
   const std::optional<Target>& result, std::size_t clipIndex, bool leftBorder)
{
   REQUIRE(result.has_value());
   REQUIRE(result->clipIndex == clipIndex);
   REQUIRE(result->adjustLeftBorder == leftBorder);
}
} // namespace

TEST_CASE("WaveClipBorderHit: no clips")
{
   REQUIRE(!FindAdjustTarget({}, 0).has_value());
   REQUIRE(!FindAdjustTarget({}, 100).has_value());
}

TEST_CASE("WaveClipBorderHit: single clip")
{
   const std::vector<ClipEdges> clips { { 100, 200 } };

   SECTION("left border, inside the single threshold")
   {
      RequireHit(FindAdjustTarget(clips, 100), 0, true);
      RequireHit(FindAdjustTarget(clips, 95), 0, true);
      RequireHit(FindAdjustTarget(clips, 105), 0, true);
   }
   SECTION("right border, inside the single threshold")
   {
      RequireHit(FindAdjustTarget(clips, 200), 0, false);
      RequireHit(FindAdjustTarget(clips, 195), 0, false);
      RequireHit(FindAdjustTarget(clips, 205), 0, false);
   }
   SECTION("between the single and the doubled threshold: no hit")
   {
      REQUIRE(!FindAdjustTarget(clips, 94).has_value());
      REQUIRE(!FindAdjustTarget(clips, 106).has_value());
      REQUIRE(!FindAdjustTarget(clips, 194).has_value());
      REQUIRE(!FindAdjustTarget(clips, 206).has_value());
   }
   SECTION("far from both borders: no hit")
   {
      REQUIRE(!FindAdjustTarget(clips, 150).has_value());
      REQUIRE(!FindAdjustTarget(clips, 0).has_value());
      REQUIRE(!FindAdjustTarget(clips, 500).has_value());
   }
}

TEST_CASE("WaveClipBorderHit: narrow clips are ignored")
{
   // Width is inclusive of both edges: 100..108 is 9 px, 100..109 is 10 px
   REQUIRE(!FindAdjustTarget({ { 100, 108 } }, 100).has_value());
   REQUIRE(!FindAdjustTarget({ { 100, 108 } }, 108).has_value());
   RequireHit(FindAdjustTarget({ { 100, 109 } }, 100), 0, true);
   RequireHit(FindAdjustTarget({ { 100, 109 } }, 109), 0, false);
}

TEST_CASE("WaveClipBorderHit: adjacent clips")
{
   const std::vector<ClipEdges> clips { { 0, 99 }, { 100, 199 } };

   SECTION("pointer inside the left clip: its right border")
   {
      RequireHit(FindAdjustTarget(clips, 98), 0, false);
      RequireHit(FindAdjustTarget(clips, 95), 0, false);
   }
   SECTION("pointer on or after the left clip's last pixel: right clip's left border")
   {
      RequireHit(FindAdjustTarget(clips, 99), 1, true);
      RequireHit(FindAdjustTarget(clips, 100), 1, true);
      RequireHit(FindAdjustTarget(clips, 104), 1, true);
   }
}

TEST_CASE("WaveClipBorderHit: clips separated by a gap")
{
   const std::vector<ClipEdges> clips { { 0, 99 }, { 110, 209 } };

   SECTION("only the left clip is a candidate")
   {
      RequireHit(FindAdjustTarget(clips, 95), 0, false);
      RequireHit(FindAdjustTarget(clips, 99), 0, false);
   }
   SECTION("only the right clip is a candidate")
   {
      RequireHit(FindAdjustTarget(clips, 115), 1, true);
      RequireHit(FindAdjustTarget(clips, 110), 1, true);
   }
   SECTION("both are candidates: the right clip's left border wins")
   {
      // 99 is within 10 px of both borders; the pointer is past the left clip
      RequireHit(FindAdjustTarget(clips, 100), 1, true);
      RequireHit(FindAdjustTarget(clips, 104), 1, true);
      RequireHit(FindAdjustTarget(clips, 102), 1, true);
      RequireHit(FindAdjustTarget(clips, 107), 1, true);
   }
   SECTION("middle of a wide gap: no hit")
   {
      const std::vector<ClipEdges> wide { { 0, 99 }, { 300, 399 } };
      REQUIRE(!FindAdjustTarget(wide, 200).has_value());
   }
}

TEST_CASE("WaveClipBorderHit: overlapping candidates, the last clip wins")
{
   const std::vector<ClipEdges> clips { { 100, 200 }, { 103, 300 } };
   RequireHit(FindAdjustTarget(clips, 101), 1, true);
}
