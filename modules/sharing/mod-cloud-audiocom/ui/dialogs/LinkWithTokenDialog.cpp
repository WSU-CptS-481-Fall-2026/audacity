/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  LinkAccountDialog.cpp

  Dmitry Vedenko

**********************************************************************/
#include "LinkWithTokenDialog.h"

#include <wx/button.h>
#include <wx/textctrl.h>
#include <wx/statline.h>
#include <wx/weakref.h>

#include "ShuttleGui.h"

#include "BasicUI.h"
#include "CodeConversions.h"
#include "ExportUtils.h"

#include "OAuthService.h"
#include "ServiceConfig.h"

#include "AuthorizationHandler.h"
#include "LinkFailedDialog.h"
#include "LinkSucceededDialog.h"
#include "HyperLink.h"

namespace audacity::cloud::audiocom
{
LinkWithTokenDialog::LinkWithTokenDialog(AudiocomTrace trace, wxWindow* parent)
    : wxDialogWrapper(
         parent, wxID_ANY, XO("Link account"), wxDefaultPosition, { 480, -1 },
         wxDEFAULT_DIALOG_STYLE)
    , mAudiocomTrace(trace)
{
   GetAuthorizationHandler().PushSuppressDialogs();

   ShuttleGui s(this, eIsCreating);

   s.StartVerticalLay();
   {
      s.StartInvisiblePanel(16);
      {
         s.SetBorder(0);
         s.StartHorizontalLay(wxEXPAND, 0);
            s.AddFixedText(XO("Enter token to link your account"));
         
            s.AddSpace(30, 0, 1);
         
            const auto audioComLink = safenew HyperLink(
               s.GetParent(),
               wxID_ANY,
               _("Sign-in and get the token"),
               GetServiceConfig().GetOAuthLoginPage(mAudiocomTrace));
            s.AddWindow(audioComLink);
         
         s.EndHorizontalLay();

         s.AddSpace(0, 4, 0);

         mToken = s.AddTextBox(TranslatableString {}, {}, 60);
         mToken->SetName(XO("Token").Translation());
         mToken->Bind(wxEVT_TEXT, [this](auto) { OnTextChanged(); });

         s.AddSpace(0, 16, 0);

         s.AddWindow(safenew wxStaticLine { s.GetParent() }, wxEXPAND);

         s.AddSpace(0, 10, 0);

         s.StartHorizontalLay(wxEXPAND, 0);
         {
            s.AddSpace(0, 0, 1);

            s.AddButton(XXO("&Cancel"))
               ->Bind(wxEVT_BUTTON, [this](auto) { Close(); });

            mContinueButton = s.AddButton(XXO("C&ontinue"));
            mContinueButton->Disable();
            mContinueButton->Bind(wxEVT_BUTTON, [this](auto) { OnContinue(); });
         }
         s.EndHorizontalLay();
      }
      s.EndInvisiblePanel();
   }
   s.EndVerticalLay();

   Layout();
   Fit();
   Centre();

   mToken->SetFocus();
}

LinkWithTokenDialog::~LinkWithTokenDialog()
{
   GetAuthorizationHandler().PopSuppressDialogs();
}

void LinkWithTokenDialog::OnContinue()
{
   mContinueButton->Disable();

   wxWeakRef<LinkWithTokenDialog> weakDialog(this);

   GetOAuthService().HandleLinkURI(
      audacity::ToUTF8(mToken->GetValue()), mAudiocomTrace,
      [weakDialog, trace = mAudiocomTrace](auto accessToken) {
         BasicUI::CallAfter(
            [weakDialog, token = std::string(accessToken), trace]() {
               if (!token.empty())
               {
                  if (weakDialog)
                  {
                     auto parent = weakDialog->GetParent();
                     weakDialog->Close();

                     LinkSucceededDialog successDialog { parent };
                     successDialog.ShowModal();
                  }

                  return;
               }

               LinkFailedDialog errorDialog(weakDialog, trace);

               if (wxID_RETRY != errorDialog.ShowModal())
               {
                  if (weakDialog)
                     weakDialog->Close();
               }
            });
      });
}

void LinkWithTokenDialog::OnTextChanged()
{
   mContinueButton->Enable(!mToken->GetValue().empty());
}

} // namespace audacity::cloud::audiocom

// Remaining code hooks this add-on into the application
#include "CommandContext.h"
#include "MenuRegistry.h"
#include "ProjectWindows.h"
#include "CommandManager.h"
#include "Project.h"
#include "LinkAccountMenuState.h"

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
