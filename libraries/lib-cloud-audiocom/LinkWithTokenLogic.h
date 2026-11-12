/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  LinkWithTokenLogic.h

  UI-free decision rules of the "Link account" token dialog.

  The dialog (mod-cloud-audiocom) needs a wx event loop and several
  global services, so it cannot be unit tested directly. The rules that
  decide what it does are kept here, free of wx and of global state.

  NOTE: these functions currently encode the dialog's behavior exactly
  as it was before refactoring (characterization), so that tests can
  pin it down before it is changed.

**********************************************************************/
#pragma once

#include <string_view>

namespace audacity::cloud::audiocom
{
//! Whether the "Continue" button should be enabled for the given text
inline bool CanSubmitLinkToken(std::string_view tokenText)
{
   return !tokenText.empty();
}

//! What the dialog does once the service answered a link attempt
struct LinkAttemptOutcome
{
   bool closeDialog { false };
   bool showSuccessDialog { false };
   bool showFailureDialog { false };
};

//! @param tokenReceived  the service returned a non-empty access token
//! @param dialogAlive    the token dialog still exists
inline LinkAttemptOutcome
GetLinkAttemptOutcome(bool tokenReceived, bool dialogAlive)
{
   if (tokenReceived)
      return { dialogAlive, dialogAlive, false };

   return { false, false, true };
}

//! Whether the token dialog closes after the failure dialog was dismissed
inline bool ShouldCloseAfterFailure(bool retryChosen, bool dialogAlive)
{
   return !retryChosen && dialogAlive;
}
} // namespace audacity::cloud::audiocom
