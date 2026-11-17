/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  LinkWithTokenFlowTests.cpp

  Characterization tests for the decisions that used to live inside
  LinkWithTokenDialog (OnTextChanged and the OnContinue callback).

**********************************************************************/
#include "LinkWithTokenFlow.h"

#include <catch2/catch.hpp>

using namespace audacity::cloud::audiocom;

TEST_CASE("LinkWithTokenFlow: token box")
{
   SECTION("Empty text can not be submitted")
   {
      REQUIRE_FALSE(IsTokenSubmittable(""));
      REQUIRE_FALSE(IsTokenSubmittable(std::string_view {}));
   }

   SECTION("Any text can be submitted")
   {
      REQUIRE(IsTokenSubmittable("abc"));
      REQUIRE(IsTokenSubmittable("audacity://link?authorization_code=abc"));
   }

   SECTION("White space only is currently accepted (existing behaviour)")
   {
      REQUIRE(IsTokenSubmittable(" "));
      REQUIRE(IsTokenSubmittable("   "));
      REQUIRE(IsTokenSubmittable("\t\n"));
   }
}

TEST_CASE("LinkWithTokenFlow: link request completes")
{
   SECTION("Token received and dialog still open: show success")
   {
      REQUIRE(DecideLinkOutcome(true, true) == LinkOutcome::ShowSuccess);
   }

   SECTION("Token received but dialog already gone: show nothing")
   {
      REQUIRE(DecideLinkOutcome(true, false) == LinkOutcome::Ignore);
   }

   SECTION("No token: show the failure dialog, whether or not the dialog is open")
   {
      REQUIRE(DecideLinkOutcome(false, true) == LinkOutcome::ShowFailure);
      REQUIRE(DecideLinkOutcome(false, false) == LinkOutcome::ShowFailure);
   }
}

TEST_CASE("LinkWithTokenFlow: after the failure dialog")
{
   SECTION("Retry keeps the token dialog open")
   {
      REQUIRE_FALSE(ShouldCloseAfterFailure(true, true));
      REQUIRE_FALSE(ShouldCloseAfterFailure(true, false));
   }

   SECTION("Anything but retry closes the token dialog if it still exists")
   {
      REQUIRE(ShouldCloseAfterFailure(false, true));
      REQUIRE_FALSE(ShouldCloseAfterFailure(false, false));
   }
}
