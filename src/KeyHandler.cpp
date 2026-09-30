#include "Globals.h"
#include "EditSession.h"
#include "TextService.h"

#include "CandidateWindow.h"
#include "Diagnostics.h"

#include <new>

namespace {

// 按键路径上的失败会一次一键地重复。日志容量有限，所以每种失败每个进程
// 只留头几条 —— 被同一条消息刷满的日志等于没有日志。
bool WithinLogBudget(LONG* seen, LONG budget) {
  return InterlockedIncrement(seen) <= budget;
}

}  // namespace

class CKeyHandlerEditSession final : public CEditSessionBase {
 public:
  CKeyHandlerEditSession(CTextService* service, ITfContext* context, WPARAM key)
      : CEditSessionBase(service, context), key_(key) {}

  // 引擎是否已经吃下这一键。吃下了就绝不能再把键交还给应用，哪怕后面写
  // 文档失败 —— 见 _InvokeKeyHandler。
  bool EngineTookKey() const { return engine_took_key_; }

  STDMETHODIMP DoEditSession(TfEditCookie cookie) override
      ZUXIA_COM_GUARD_BEGIN
    const int rime_key = zuxia::RimeEngine::VirtualKeyToRimeKey(key_);
    if (!rime_key) return S_FALSE;

    const zuxia::EngineSnapshot snapshot =
        text_service_->_Engine().ProcessKey(rime_key);
    if (!snapshot.handled && snapshot.commit.empty() &&
        snapshot.preedit.empty() && !snapshot.composing) {
      // Rime 看过之后什么也没发生，它内部状态没动，这一键还给应用是对的。
      return S_FALSE;
    }
    engine_took_key_ = true;

    const HRESULT applied =
        text_service_->_ApplyRimeSnapshot(cookie, context_, snapshot);
    if (FAILED(applied)) {
      // 引擎已经吃下这一键，文档却没写成（组字范围失效、宿主拒绝写入）。
      // 这时候把键交还给应用，字母会原样落进文档而 Rime 那边还留着它，
      // 两边从此错位，越打越乱。宁可丢掉这一键：把两边都清干净，键仍然
      // 算我们吃掉的。这条路径以前完全没有日志。
      static LONG seen = 0;
      if (WithinLogBudget(&seen, 8)) {
        zuxia::LogFailure(L"apply-failed", static_cast<unsigned long>(applied));
      }
      text_service_->_CancelComposition(cookie, context_);
      text_service_->_Engine().Clear();
      text_service_->_HideCandidateWindow();
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
  // Release 之后对象可能就没了，标志位得先读出来。TF_ES_SYNC 保证
  // DoEditSession 已经在 RequestEditSession 里跑完。
  const bool engine_took_key = session->EngineTookKey();
  session->Release();

  if (FAILED(request_result)) {
    // 同步写锁没拿到，DoEditSession 根本没跑，引擎也就没碰过这一键 ——
    // 交还给应用是安全的。以前这条路一声不吭，现在留个记号。
    static LONG refused = 0;
    if (WithinLogBudget(&refused, 8)) {
      zuxia::LogFailure(L"edit-session-refused",
                        static_cast<unsigned long>(request_result));
    }
    return request_result;
  }
  // 引擎吃下了就必须报 S_OK，否则调用方会把 *eaten 改回 FALSE，这一键就
  // 两边都生效了一次。
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
    } else if (SUCCEEDED(result)) {
      result = E_FAIL;  // GetRange 报成功却没给出范围
    }
    if (FAILED(result)) {
      // 落字没写进去。这时候还无条件结束组字，留在文档里的就是组字范围里
      // 原来那串码（preedit），用户选的词彻底没了，屏幕上反而多出一串字母。
      // 所以失败就把组字范围清空再收 —— 宁可这一下什么都没打出来，也不能
      // 把一串拉丁字母留在人家文档里。
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

HRESULT CTextService::_ApplyRimeSnapshot(
    TfEditCookie cookie, ITfContext* context,
    const zuxia::EngineSnapshot& snapshot) {
  if (!snapshot.commit.empty()) {
    const HRESULT result = _CommitText(cookie, context, snapshot.commit);
    // 一次按键可以同时「落下前一段」和「还剩一段在组字」：选了只覆盖一半
    // 输入的候选、或者选了解码器的兜底候选，都是这样。原先这里落完字就
    // return，剩下那段 preedit 连同它的候选一起消失 —— 用户按过的键凭空
    // 不见了。两样都要处理。
    if (SUCCEEDED(result) && !snapshot.preedit.empty()) {
      const HRESULT kept =
          _SetCompositionText(cookie, context, snapshot.preedit);
      if (SUCCEEDED(kept)) {
        _UpdateCandidateWindow(cookie, context, snapshot);
      } else {
        _HideCandidateWindow();
      }
      return kept;
    }
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
