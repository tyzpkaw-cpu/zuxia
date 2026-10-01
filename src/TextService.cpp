#include "Globals.h"
#include "Diagnostics.h"
#include "TextService.h"
#include "Settings.h"

#include "CandidateWindow.h"
#include "PartsWindow.h"

#include <new>

HRESULT CTextService::CreateInstance(IUnknown* outer, REFIID riid,
                                     void** object) {
  if (!object) return E_INVALIDARG;
  *object = nullptr;
  if (outer) return CLASS_E_NOAGGREGATION;

  auto* service = new (std::nothrow) CTextService();
  if (!service) return E_OUTOFMEMORY;
  const HRESULT result = service->QueryInterface(riid, object);
  service->Release();
  return result;
}

CTextService::CTextService() { DllAddRef(); }

CTextService::~CTextService() {
  if (candidate_window_) {
    delete candidate_window_;
    candidate_window_ = nullptr;
  }
  if (parts_window_) {
    delete parts_window_;
    parts_window_ = nullptr;
  }
  engine_.Shutdown();
  DllRelease();
}

STDMETHODIMP CTextService::QueryInterface(REFIID riid, void** object) {
  if (!object) return E_INVALIDARG;
  *object = nullptr;

  if (IsEqualIID(riid, IID_IUnknown) ||
      IsEqualIID(riid, IID_ITfTextInputProcessor)) {
    *object = static_cast<ITfTextInputProcessor*>(this);
  } else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink)) {
    *object = static_cast<ITfThreadMgrEventSink*>(this);
  } else if (IsEqualIID(riid, IID_ITfTextEditSink)) {
    *object = static_cast<ITfTextEditSink*>(this);
  } else if (IsEqualIID(riid, IID_ITfKeyEventSink)) {
    *object = static_cast<ITfKeyEventSink*>(this);
  } else if (IsEqualIID(riid, IID_ITfCompartmentEventSink)) {
    *object = static_cast<ITfCompartmentEventSink*>(this);
  } else if (IsEqualIID(riid, IID_ITfCompositionSink)) {
    *object = static_cast<ITfCompositionSink*>(this);
  }

  if (!*object) return E_NOINTERFACE;
  AddRef();
  return S_OK;
}

STDMETHODIMP_(ULONG) CTextService::AddRef() {
  return static_cast<ULONG>(InterlockedIncrement(&_cRef));
}

STDMETHODIMP_(ULONG) CTextService::Release() {
  const LONG count = InterlockedDecrement(&_cRef);
  assert(count >= 0);
  if (count == 0) delete this;
  return static_cast<ULONG>(count);
}

STDMETHODIMP CTextService::Activate(ITfThreadMgr* thread_mgr,
                                    TfClientId client_id) ZUXIA_COM_GUARD_BEGIN
  if (!thread_mgr) return E_INVALIDARG;
  ITfDocumentMgr* focused = nullptr;
  _pThreadMgr = thread_mgr;
  _pThreadMgr->AddRef();
  _tfClientId = client_id;

  if (!_InitThreadMgrEventSink()) goto error;

  if (SUCCEEDED(_pThreadMgr->GetFocus(&focused)) && focused) {
    _InitTextEditSink(focused);
    focused->Release();
  }

  if (!_InitKeyEventSink()) goto error;
  _InitPreservedKey();
  if (!CCandidateWindow::InitWindowClass()) goto error;
  if (!zuxia::CPartsWindow::InitWindowClass()) goto error;

  candidate_window_ = new (std::nothrow) CCandidateWindow();
  if (!candidate_window_ || !candidate_window_->Create()) goto error;

  parts_window_ = new (std::nothrow) zuxia::CPartsWindow();
  if (!parts_window_ || !parts_window_->Create()) goto error;

  if (!engine_.Initialize(g_hInst)) goto error;

  _SetKeyboardOpen(TRUE);
  _InitInputMode();
  _InitLanguageBar();
  zuxia::LogEvent(L"activated");
  return S_OK;

error:
  zuxia::LogEvent(L"activate-failed");
  Deactivate();
  return E_FAIL;
ZUXIA_COM_GUARD_END(L"CTextService::Activate", E_FAIL)

STDMETHODIMP CTextService::Deactivate() ZUXIA_COM_GUARD_BEGIN
  _UninitLanguageBar();
  _UninitInputMode();
  engine_.Clear();
  engine_.Shutdown();
  _HideCandidateWindow();
  if (candidate_window_) {
    delete candidate_window_;
    candidate_window_ = nullptr;
  }
  if (parts_window_) {
    parts_window_->Hide();
    delete parts_window_;
    parts_window_ = nullptr;
  }

  if (_pComposition) {
    _pComposition->Release();
    _pComposition = nullptr;
  }

  _InitTextEditSink(nullptr);
  _UninitPreservedKey();
  _UninitKeyEventSink();
  _UninitThreadMgrEventSink();

  if (_pThreadMgr) {
    _pThreadMgr->Release();
    _pThreadMgr = nullptr;
  }
  _tfClientId = 0;
  return S_OK;
ZUXIA_COM_GUARD_END(L"CTextService::Deactivate", S_OK)

void CTextService::_UpdatePartsWindow(const std::wstring& text) {
  if (!parts_window_) return;
  if (!zuxia::CurrentAppearance().show_parts_window) {
    parts_window_->Hide();
    return;
  }
  // 空串不再意味着「隐藏」：拆字窗留着上一个词，由它自己决定什么时候收。
  if (!text.empty()) parts_window_->ShowWord(text);
}

void CTextService::_HidePartsWindow() {
  if (parts_window_) parts_window_->Hide();
}

void CTextService::_HideCandidateWindow() {
  if (candidate_window_) candidate_window_->Hide();
  // 拆字窗不跟着候选窗收起：上屏之后它留着刚上屏的那个词，可以拖开、放大
  // 了慢慢看（0.4.1 之前一拖就没，就是因为这里连带把它藏了）。这里只结束
  // 「这一次组字」—— 用户在这次组字里点过 × 的，下一次组字它照常出来。
  parts_session_open_ = false;
}

HRESULT CTextService::_ApplyRimeSnapshot(
    TfEditCookie cookie, ITfContext* context,
    const zuxia::EngineSnapshot& snapshot) {
  if (!snapshot.commit.empty()) {
    // 拆字窗换成刚上屏的词。必须在下面 _HideCandidateWindow 之前：用户这次
    // 组字里关掉过它，就不能因为上屏又弹出来。
    _UpdatePartsWindow(snapshot.commit);
    const HRESULT result = _CommitText(cookie, context, snapshot.commit);
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
    const HRESULT result =
        _SetCompositionText(cookie, context, snapshot.preedit);
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

  // 新的一次组字开始：上一次里被 × 掉的拆字窗可以再出来了。
  if (!parts_session_open_) {
    parts_session_open_ = true;
    if (parts_window_) parts_window_->ResetDismissed();
  }
  // 拆字窗实时跟着高亮的候选走。
  if (!snapshot.candidates.empty()) {
    size_t idx = static_cast<size_t>(snapshot.highlighted);
    if (idx >= snapshot.candidates.size()) idx = 0;
    _UpdatePartsWindow(snapshot.candidates[idx].text);
  }

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
        anchor.left   = caret.x;
        anchor.bottom = caret.y + 24;
        positioned    = true;
      }
    }
    view->Release();
  }

  if (!positioned) {
    POINT cursor = {};
    GetCursorPos(&cursor);
    anchor.left   = cursor.x;
    anchor.bottom = cursor.y + 20;
  }
  candidate_window_->Move(anchor.left, anchor.bottom);
  candidate_window_->Show();
}
