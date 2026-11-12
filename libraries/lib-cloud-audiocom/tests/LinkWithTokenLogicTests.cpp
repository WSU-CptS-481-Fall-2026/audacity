/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  LinkWithTokenLogicTests.cpp

  Characterization tests: they pin the behavior of the token dialog as it
  was before refactoring. Tests tagged [current-behavior] describe
  behavior that is questionable and may be deliberately changed later.

**********************************************************************/
#include "LinkWithTokenLogic.h"

#include <catch2/catch.hpp>

using namespace audacity::cloud::audiocom;

TEST_CASE("LinkWithTokenLogic: Continue button enablement")
{
   SECTION("Empty text keeps Continue disabled")
   {
      REQUIRE_FALSE(CanSubmitLinkToken(""));
   }

   SECTION("Any non-empty text enables Continue")
   {
      REQUIRE(CanSubmitLinkToken("abc123"));
      REQUIRE(CanSubmitLinkToken("a"));
   }
}

TEST_CASE(
   "LinkWithTokenLogic: whitespace is not trimmed", "[current-behavior]")
{
   // Known weakness: a whitespace-only token enables Continue.
   REQUIRE(CanSubmitLinkToken(" "));
   REQUIRE(CanSubmitLinkToken("\n"));
   REQUIRE(CanSubmitLinkToken("   \t  "));
}

TEST_CASE("LinkWithTokenLogic: outcome of a link attempt")
{
   SECTION("Success with the dialog alive: close it and show success")
   {
      const auto outcome = GetLinkAttemptOutcome(true, true);
      REQUIRE(outcome.closeDialog);
      REQUIRE(outcome.showSuccessDialog);
      REQUIRE_FALSE(outcome.showFailureDialog);
   }

   SECTION("Success after the dialog was destroyed: do nothing")
   {
      const auto outcome = GetLinkAttemptOutcome(true, false);
      REQUIRE_FALSE(outcome.closeDialog);
      REQUIRE_FALSE(outcome.showSuccessDialog);
      REQUIRE_FALSE(outcome.showFailureDialog);
   }

   SECTION("Failure always shows the failure dialog, alive or not")
   {
      for (const bool alive : { true, false })
      {
         const auto outcome = GetLinkAttemptOutcome(false, alive);
         REQUIRE_FALSE(outcome.closeDialog);
         REQUIRE_FALSE(outcome.showSuccessDialog);
         REQUIRE(outcome.showFailureDialog);
      }
   }
}

TEST_CASE("LinkWithTokenLogic: closing after the failure dialog")
{
   SECTION("Retry keeps the token dialog open")
   {
      REQUIRE_FALSE(ShouldCloseAfterFailure(true, true));
      REQUIRE_FALSE(ShouldCloseAfterFailure(true, false));
   }

   SECTION("Anything else closes it, if it still exists")
   {
      REQUIRE(ShouldCloseAfterFailure(false, true));
      REQUIRE_FALSE(ShouldCloseAfterFailure(false, false));
   }
}
