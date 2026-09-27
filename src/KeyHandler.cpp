#include "Globals.h"
#include "EditSession.h"
#include "TextService.h"

#include "CandidateWindow.h"

#include <new>

class CKeyHandlerEditSession final : public CEditSessionBase {
 public:
  CKeyHandlerEditSession(CTextService* service, ITfContext* context, WPARAM key)
      : CEditSessionBase(service, context), key_(key) {}

  STDMETHODIMP DoEditSession(TfEditCookie cookie) override {
    const int rime_key = zuxia::RimeEngine::VirtualKeyToRimeKey(key_);
    if (!rime_key) return S_FALSE;

    const zuxia::EngineSnapshot snapshot =
        text_service_->_Engine().ProcessKey(rime_key);
    if (!snapshot.handled && snapshot.commit.empty() &&
        snapshot.preedit.empty() && !snapshot.composing) {
      return S_FALSE;
    }
    return text_service_->_ApplyRimeSnapshot(cookie, context_, snapshot);
  }

 private:
  WPARAM key_;
};

BOOL IsRangeCovered(TfEditCookie cookie, ITfRange* test, ITfRange* cover) {
  if (!test || !cover) return FALSE;
  LONG comparison = 0;
  if (cover->CompareStart(cookie, test, TF_ANCHOR_START, &comparison) != S_OK ||
      comparison > 0) {
    return FALSE;
  }
  if (cover->CompareEnd(cookie, test, TF_ANCHOR_END, &comparison) != S_OK ||
      comparison < 0) {
    return FALSE;
  }
  return TRUE;
}

HRESULT CTextService::_InvokeKeyHandler(ITfContext* context, WPARAM key,
                                        LPARAM /*flags*/) {
  if (!context) return E_INVALIDARG;
  auto* session = new (std::nothrow) CKeyHandlerEditSession(this, context, key);
  if (!session) return E_OUTOFMEMORY;

  HRESULT session_result = E_FAIL;
  const HRESULT request_result = context->RequestEditSession(
      _tfClientId, session, TF_ES_SYNC | TF_ES_READWRITE, &session_result);
  session->Release();
  return SUCCEEDED(request_result) ? session_result : request_result;
}

HRESULT CTextService::_EnsureComposition(TfEditCookie cookie,
                                         ITfContext* context) {
  if (_pComposition) return S_OK;
  if (!context) return E_INVALIDARG;

  ITfInsertAtSelection* insert = nullptr;
  ITfRange* range = nullptr;
  ITfContextComposition* composition_context = nullptr;
  ITfComposition* composition = nullptr;
  TF_SELECTION selection = {};
  HRESULT result = context->QueryInterface(IID_ITfInsertAtSelection,
                                            reinterpret_cast<void**>(&insert));
  if (FAILED(result)) goto done;

  result = insert->InsertTextAtSelection(cookie, TF_IAS_QUERYONLY, nullptr, 0,
                                         &range);
  if (FAILED(result) || !range) goto done;

  result = context->QueryInterface(IID_ITfContextComposition,
                                   reinterpret_cast<void**>(
                                       &composition_context));
  if (FAILED(result)) goto done;

  result = composition_context->StartComposition(cookie, range, this,
                                                  &composition);
  if (FAILED(result) || !composition) goto done;

  _pComposition = composition;  // Own the reference returned by TSF.
  composition = nullptr;

  selection.range = range;
  selection.style.ase = TF_AE_NONE;
  selection.style.fInterimChar = FALSE;
  context->SetSelection(cookie, 1, &selection);
  result = S_OK;

done:
  if (composition) composition->Release();
  if (composition_context) composition_context->Release();
  if (range) range->Release();
  if (insert) insert->Release();
  return result;
}

HRESULT CTextService::_SetCompositionText(TfEditCookie cookie,
                                          ITfContext* context,
                                          const std::wstring& text) {
  HRESULT result = _EnsureComposition(cookie, context);
  if (FAILED(result) || !_pComposition) return FAILED(result) ? result : E_FAIL;

  ITfRange* range = nullptr;
  result = _pComposition->GetRange(&range);
  if (FAILED(result) || !range) return FAILED(result) ? result : E_FAIL;

  result = range->SetText(cookie, 0, text.c_str(),
                          static_cast<LONG>(text.size()));
  if (SUCCEEDED(result)) {
    range->Collapse(cookie, TF_ANCHOR_END);
    TF_SELECTION selection = {};
    selection.range = range;
    selection.style.ase = TF_AE_NONE;
    selection.style.fInterimChar = FALSE;
    context->SetSelection(cookie, 1, &selection);
  }
  range->Release();
  return result;
}

HRESULT CTextService::_CommitText(TfEditCookie cookie, ITfContext* context,
                                  const std::wstring& text) {
  if (!context) return E_INVALIDARG;
  if (text.empty()) return S_OK;

  HRESULT result = E_FAIL;
  ITfRange* range = nullptr;
  if (_pComposition) {
    result = _pComposition->GetRange(&range);
    if (SUCCEEDED(result) && range) {
      result = range->SetText(cookie, 0, text.c_str(),
                              static_cast<LONG>(text.size()));
      if (SUCCEEDED(result)) {
        range->Collapse(cookie, TF_ANCHOR_END);
        TF_SELECTION selection = {};
        selection.range = range;
        selection.style.ase = TF_AE_NONE;
        selection.style.fInterimChar = FALSE;
        context->SetSelection(cookie, 1, &selection);
      }
      range->Release();
    }
    _TerminateComposition(cookie, context);
    return result;
  }

  ITfInsertAtSelection* insert = nullptr;
  result = context->QueryInterface(IID_ITfInsertAtSelection,
                                   reinterpret_cast<void**>(&insert));
  if (FAILED(result)) return result;
  result = insert->InsertTextAtSelection(
      cookie, 0, text.c_str(), static_cast<LONG>(text.size()), &range);
  if (SUCCEEDED(result) && range) {
    range->Collapse(cookie, TF_ANCHOR_END);
    TF_SELECTION selection = {};
    selection.range = range;
    selection.style.ase = TF_AE_NONE;
    selection.style.fInterimChar = FALSE;
    context->SetSelection(cookie, 1, &selection);
    range->Release();
  }
  insert->Release();
  return result;
}

void CTextService::_CancelComposition(TfEditCookie cookie,
                                      ITfContext* context) {
  if (!_pComposition) return;
  ITfRange* range = nullptr;
  if (SUCCEEDED(_pComposition->GetRange(&range)) && range) {
    range->SetText(cookie, 0, L"", 0);
    range->Release();
  }
  _TerminateComposition(cookie, context);
}

HRESULT CTextService::_ApplyRimeSnapshot(
    TfEditCookie cookie, ITfContext* context,
    const zuxia::EngineSnapshot& snapshot) {
  if (!snapshot.commit.empty()) {
    const HRESULT result = _CommitText(cookie, context, snapshot.commit);
    _HideCandidateWindow();
    return result;
  }

  if (!snapshot.preedit.empty()) {
    const HRESULT result = _SetCompositionText(cookie, context, snapshot.preedit);
    if (SUCCEEDED(result)) {
      _UpdateCandidateWindow(cookie, context, snapshot);
    }
    return result;
  }

  _CancelComposition(cookie, context);
  _HideCandidateWindow();
  return S_OK;
}

void CTextService::_UpdateCandidateWindow(
    TfEditCookie cookie, ITfContext* context,
    const zuxia::EngineSnapshot& snapshot) {
  if (!candidate_window_ || snapshot.preedit.empty()) {
    _HideCandidateWindow();
    return;
  }

  candidate_window_->Update(snapshot.preedit, snapshot.candidates,
                            snapshot.highlighted);

  RECT anchor = {};
  bool positioned = false;
  ITfContextView* view = nullptr;
  ITfRange* range = nullptr;
  if (context && SUCCEEDED(context->GetActiveView(&view)) && view) {
    if (_pComposition && SUCCEEDED(_pComposition->GetRange(&range)) && range) {
      BOOL clipped = FALSE;
      if (SUCCEEDED(view->GetTextExt(cookie, range, &anchor, &clipped))) {
        positioned = true;
      }
      range->Release();
    }

    if (!positioned) {
      HWND owner = nullptr;
      POINT caret = {};
      if (SUCCEEDED(view->GetWnd(&owner)) && owner && GetCaretPos(&caret) &&
          ClientToScreen(owner, &caret)) {
        anchor.left = caret.x;
        anchor.bottom = caret.y + 24;
        positioned = true;
      }
    }
    view->Release();
  }

  if (!positioned) {
    POINT cursor = {};
    GetCursorPos(&cursor);
    anchor.left = cursor.x;
    anchor.bottom = cursor.y + 20;
  }
  candidate_window_->Move(anchor.left, anchor.bottom);
  candidate_window_->Show();
}

void CTextService::_HideCandidateWindow() {
  if (candidate_window_) candidate_window_->Hide();
}
