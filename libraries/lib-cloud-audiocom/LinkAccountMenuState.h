/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  LinkAccountMenuState.h

  Decides what the "Help > Link audio.com account..." menu entry shows
  and does, depending on whether the account is already linked.

**********************************************************************/
#pragma once

#include "Internat.h"
#include "TranslatableString.h"

namespace audacity::cloud::audiocom
{
enum class LinkAccountAction
{
   ShowLinkDialog,
   Unlink,
};

inline LinkAccountAction GetLinkAccountAction(bool accountLinked)
{
   return accountLinked ? LinkAccountAction::Unlink
                        : LinkAccountAction::ShowLinkDialog;
}

inline TranslatableString GetLinkAccountLabel(bool accountLinked)
{
   return accountLinked ? XXO("&Unlink audio.com account...")
                        : XXO("L&ink audio.com account...");
}
}
