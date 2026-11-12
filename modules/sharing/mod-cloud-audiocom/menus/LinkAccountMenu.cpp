/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  LinkAccountMenu.cpp

  Dmitry Vedenko

  Hooks the "Help > Link audio.com account..." menu entry into the
  application. The entry links the account through LinkWithTokenDialog,
  or unlinks it when the account is already linked.

**********************************************************************/
#include <wx/frame.h>

#include "CommandContext.h"
#include "CommandFlag.h"
#include "CommandManager.h"
#include "ExportUtils.h"
#include "LinkAccountMenuState.h"
#include "MenuRegistry.h"
#include "OAuthService.h"
#include "Project.h"
#include "ProjectWindows.h"

#include "ui/dialogs/LinkWithTokenDialog.h"

namespace {
// Define our extra menu item
void OnLinkAccount(const CommandContext& context)
{
   auto& oauthService = audacity::cloud::audiocom::GetOAuthService();

   if (
      audacity::cloud::audiocom::GetLinkAccountAction(
         oauthService.HasAccessToken())
      == audacity::cloud::audiocom::LinkAccountAction::Unlink)
   {
      // Account is already linked, unlink it
      oauthService.UnlinkAccount(AudiocomTrace::LinkAudiocomAccountHelpMenu);
   }
   else
   {
      // Account is not linked, show the link dialog
      auto frame = FindProjectFrame(&context.project);
      audacity::cloud::audiocom::LinkWithTokenDialog dialog {
         AudiocomTrace::LinkAudiocomAccountHelpMenu,
         frame
      };
      dialog.ShowModal();
   }
}

TranslatableString GetLinkAccountLabel()
{
   return audacity::cloud::audiocom::GetLinkAccountLabel(audacity::cloud::audiocom::GetOAuthService().HasAccessToken());
}

void UpdateLinkAccountLabels()
{
   for (auto pProject : AllProjects {})
      CommandManager::Get(*pProject)
         .Modify(wxT("LinkAccount"), GetLinkAccountLabel());
}

// Keep the menu label in sync with the account state
Observer::Subscription sAuthStateSubscription =
   audacity::cloud::audiocom::GetOAuthService().Subscribe(
      [](const auto&) { UpdateLinkAccountLabels(); });

using namespace MenuRegistry;
AttachedItem sAttachment{
   Command(
      wxT("LinkAccount"), XXO("L&ink audio.com account..."),
      OnLinkAccount, AlwaysEnabledFlag),
   Placement{ wxT("Help/Extra"), { OrderingHint::Begin } }
};
}
