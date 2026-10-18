/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  LinkAccountMenuStateTests.cpp

**********************************************************************/
#include "LinkAccountMenuState.h"
#include "ExportUtils.h"
#include "OAuthService.h"
#include "MockedPrefs.h"

#include <catch2/catch.hpp>

using namespace audacity::cloud::audiocom;

TEST_CASE("LinkAccountMenuState")
{
   SECTION("Not linked: entry offers to link")
   {
      REQUIRE(GetLinkAccountAction(false) == LinkAccountAction::ShowLinkDialog);
      REQUIRE(
         GetLinkAccountLabel(false).MSGID().GET() ==
         "L&ink audio.com account...");
   }

   SECTION("Linked: entry offers to unlink")
   {
      REQUIRE(GetLinkAccountAction(true) == LinkAccountAction::Unlink);
      REQUIRE(
         GetLinkAccountLabel(true).MSGID().GET() ==
         "&Unlink audio.com account...");
   }

   SECTION("Label differs between the two states")
   {
      REQUIRE(
         GetLinkAccountLabel(true).MSGID() != GetLinkAccountLabel(false).MSGID());
   }
}

TEST_CASE("LinkAccountMenuState follows OAuthService")
{
   MockedPrefs mockedPrefs;
   OAuthService service;

   SECTION("A fresh service is not linked")
   {
      REQUIRE_FALSE(service.HasAccessToken());
      REQUIRE(
         GetLinkAccountAction(service.HasAccessToken()) ==
         LinkAccountAction::ShowLinkDialog);
   }

   SECTION("Unlinking notifies subscribers, so the menu label can refresh")
   {
      int notifications = 0;
      bool authorised = true;
      auto subscription =
         service.Subscribe([&](const AuthStateChangedMessage& message) {
            ++notifications;
            authorised = message.authorised;
         });

      service.UnlinkAccount(AudiocomTrace::LinkAudiocomAccountHelpMenu);

      REQUIRE(notifications == 1);
      REQUIRE_FALSE(authorised);
      REQUIRE_FALSE(service.HasAccessToken());
      REQUIRE_FALSE(service.HasRefreshToken());
      REQUIRE(
         GetLinkAccountAction(service.HasAccessToken()) ==
         LinkAccountAction::ShowLinkDialog);
   }
}
