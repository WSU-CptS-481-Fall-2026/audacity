/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  LinkWithTokenFlow.h

  Pure (wx-free) decisions made by the "Link account" token dialog:
  when the Continue button is available, what happens when the link
  request completes, and what happens after the failure dialog is closed.

  Extracted from LinkWithTokenDialog so these rules can be unit tested
  without creating any windows or contacting audio.com.

**********************************************************************/
#pragma once

#include <string_view>

namespace audacity::cloud::audiocom
{
//! Whether the text typed into the token box can be submitted.
/*!
 Any non-empty text qualifies, including text made only of white space.
 */
inline bool IsTokenSubmittable(std::string_view token)
{
   return !token.empty();
}

//! What the dialog does when the link request completes
enum class LinkOutcome
{
   ShowSuccess, //!< close the dialog and show the "linked" dialog
   Ignore, //!< the link succeeded, but the dialog is gone: nothing to show
   ShowFailure, //!< show the "link failed" dialog
};

//! Decide what to do when the link request completes
/*!
 @param tokenReceived the request produced a non-empty access token
 @param dialogAlive the token dialog still exists
 */
inline LinkOutcome DecideLinkOutcome(bool tokenReceived, bool dialogAlive)
{
   if (!tokenReceived)
      return LinkOutcome::ShowFailure;
   return dialogAlive ? LinkOutcome::ShowSuccess : LinkOutcome::Ignore;
}

//! Whether the token dialog is closed after the "link failed" dialog ends
/*!
 @param retryChosen the user chose to retry
 @param dialogAlive the token dialog still exists
 */
inline bool ShouldCloseAfterFailure(bool retryChosen, bool dialogAlive)
{
   return !retryChosen && dialogAlive;
}
} // namespace audacity::cloud::audiocom
