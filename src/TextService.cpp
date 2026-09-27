#include "Globals.h"
#include "Diagnostics.h"
#include "TextService.h"

#include "CandidateWindow.h"

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
                                    TfClientId client_id) {
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
  // Losing the Shift toggle costs one convenience; it must never cost the
  // whole text service. PreserveKey can legitimately fail -- another input
  // method may already hold the key -- and _InitPreservedKey reports that.
  _InitPreservedKey();
  if (!CCandidateWindow::InitWindowClass()) goto error;

  candidate_window_ = new (std::nothrow) CCandidateWindow();
  if (!candidate_window_ || !candidate_window_->Create()) goto error;
  if (!engine_.Initialize(g_hInst)) goto error;

  // A newly selected TIP should accept input immediately. The mode itself
  // comes from the session-wide compartment, not from this process.
  _SetKeyboardOpen(TRUE);
  _InitInputMode();
  _InitLanguageBar();
  zuxia::LogEvent(L"activated");
  return S_OK;

error:
  zuxia::LogEvent(L"activate-failed");
  Deactivate();
  return E_FAIL;
}

STDMETHODIMP CTextService::Deactivate() {
  _UninitLanguageBar();
  _UninitInputMode();
  engine_.Clear();
  engine_.Shutdown();
  _HideCandidateWindow();
  if (candidate_window_) {
    delete candidate_window_;
    candidate_window_ = nullptr;
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
}
