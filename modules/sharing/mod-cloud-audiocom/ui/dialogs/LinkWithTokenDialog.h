/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  LinkWithTokenDialog.h

  Dmitry Vedenko

**********************************************************************/
#pragma once

#include "wxPanelWrapper.h"

class ShuttleGui;
class wxButton;
class wxTextCtrl;
enum class AudiocomTrace;

namespace audacity::cloud::audiocom
{
class LinkWithTokenDialog final : public wxDialogWrapper
{
public:
   explicit LinkWithTokenDialog(AudiocomTrace, wxWindow* parent = nullptr);
   ~LinkWithTokenDialog() override;

private:
   // Construction of the dialog contents, in the order they are laid out
   void BuildUI();
   void BuildHeaderRow(ShuttleGui& s);
   void BuildTokenBox(ShuttleGui& s);
   void BuildButtonRow(ShuttleGui& s);

   void OnContinue();
   void OnTextChanged();

   wxButton* mContinueButton { nullptr };
   wxTextCtrl* mToken { nullptr };
   const AudiocomTrace mAudiocomTrace;
};
} // namespace audacity::cloud::audiocom
