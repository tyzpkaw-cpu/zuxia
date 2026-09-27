#pragma once

// Chinese / Western input mode.
//
// The mode lives in the TSF compartment GUID_COMPARTMENT_KEYBOARD_INPUTMODE_
// CONVERSION rather than in the RimeEngine, for two reasons:
//
//  * That compartment is global to the user session, so switching mode in one
//    application switches it everywhere. A per-engine flag made the mode
//    differ between windows, which is impossible for a user to reason about.
//  * Windows reads it. The shell shows its own 中/英 indicator for a text
//    service that maintains this compartment, so the mode is visible even
//    where the language bar is not.
//
// The engine's own ascii_mode follows the compartment; nothing else writes it.

#include "Globals.h"

class CTextService;

// The language bar button. Displays 中 or 西 and toggles on click, so the mode
// is both visible and reachable without knowing the Shift shortcut.
class CModeButton : public ITfLangBarItemButton, public ITfSource {
 public:
  explicit CModeButton(CTextService* service);

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, void** object) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;

  // ITfLangBarItem
  STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* info) override;
  STDMETHODIMP GetStatus(DWORD* status) override;
  STDMETHODIMP Show(BOOL show) override;
  STDMETHODIMP GetTooltipString(BSTR* tooltip) override;

  // ITfLangBarItemButton
  STDMETHODIMP OnClick(TfLBIClick click, POINT point,
                       const RECT* area) override;
  STDMETHODIMP InitMenu(ITfMenu* menu) override;
  STDMETHODIMP OnMenuSelect(UINT id) override;
  STDMETHODIMP GetIcon(HICON* icon) override;
  STDMETHODIMP GetText(BSTR* text) override;

  // ITfSource
  STDMETHODIMP AdviseSink(REFIID riid, IUnknown* unknown,
                          DWORD* cookie) override;
  STDMETHODIMP UnadviseSink(DWORD cookie) override;

  // Tells the shell to re-read the button after the mode changed.
  void Refresh();
  // Dropped when the text service goes away; the shell may still hold a
  // reference to this object afterwards.
  void Detach();

 private:
  ~CModeButton();

  CTextService* service_ = nullptr;
  ITfLangBarItemSink* sink_ = nullptr;
  DWORD sink_cookie_ = TF_INVALID_COOKIE;
  TF_LANGBARITEMINFO info_ = {};
  BOOL shown_ = TRUE;
  LONG ref_ = 1;
};

extern const GUID c_guidModeButton;
