#include "Globals.h"
#include "EditSession.h"
#include "TextService.h"

#include "CandidateWindow.h"
#include "Diagnostics.h"

#include <new>

namespace {

bool WithinLogBudget(LONG* seen, LONG budget) {
  return InterlockedIncrement(seen) <= budget;
}

}  // namespace

class CKeyHandlerEditSession final : public CEditSessionBase {
 public:
  CKeyHandlerEditSession(CTextService* service, ITfContext* context, WPARAM key)
      : CEditSessionBase(service, context), key_(key) {}

  bool EngineTookKey() const { return engine_took_key_; }

  STDMETHODIMP DoEditSession(TfEditCookie cookie) override
      ZUXIA_COM_GUARD_BEGIN
    const int rime_key = zuxia::RimeEngine::VirtualKeyToRimeKey(key_);
    if (!rime_key) return S_FALSE;

    zuxia::EngineSnapshot snapshot;
    try {
      snapshot = text_service_->_Engine().ProcessKey(rime_key);
    } catch (...) {
      try {
        text_service_->_Engine().Clear();
        text_service_->_HideCandidateWindow();
      } catch (...) {
      }
      static LONG thrown = 0;
      if (WithinLogBudget(&thrown, 8)) {
        zuxia::LogFailure(L"process-key-threw", 0);
      }
      return S_FALSE;
    }
    if (!snapshot.handled && snapshot.commit.empty() &&
        snapshot.preedit.empty() && !snapshot.composing) {
      return S_FALSE;
    }
    engine_took_key_ = true;

    HRESULT applied = E_UNEXPECTED;
    try {
      applied = text_service_->_ApplyRimeSnapshot(cookie, context_, snapshot);
    } catch (...) {
      applied = E_UNEXPECTED;
    }
    if (FAILED(applied)) {
      static LONG seen = 0;
      if (WithinLogBudget(&seen, 8)) {
        zuxia::LogFailure(L"apply-failed", static_cast<unsigned long>(applied));
      }
      try {
        text_service_->_CancelComposition(cookie, context_);
        text_service_->_Engine().Clear();
        text_service_->_HideCandidateWindow();
      } catch (...) {
      }
    }
    return applied;
  ZUXIA_COM_GUARD_END(L"CKeyHandlerEditSession::DoEditSession", E_FAIL)

 private:
  WPARAM key_;
  bool engine_took_key_ = false;
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
  const bool engine_took_key = session->EngineTookKey();
  session->Release();

  if (FAILED(request_result)) {
    static LONG refused = 0;
    if (WithinLogBudget(&refused, 8)) {
      zuxia::LogFailure(L"edit-session-refused",
                        static_cast<unsigned long>(request_result));
    }
    return request_result;
  }
  return engine_took_key ? S_OK : session_result;
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

  _pComposition = composition;
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
    } else if (SUCCEEDED(result)) {
      result = E_FAIL;
    }
    if (FAILED(result)) {
      static LONG commit_failed = 0;
      if (WithinLogBudget(&commit_failed, 8)) {
        zuxia::LogFailure(L"commit-failed", static_cast<unsigned long>(result));
      }
      _CancelComposition(cookie, context);
      return result;
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
