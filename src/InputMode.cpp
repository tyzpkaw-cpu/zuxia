#include "InputMode.h"

#include <olectl.h>

#include "Diagnostics.h"
#include "TextService.h"

// {3EB28CF1-AB83-4E46-8B78-FFDB28DC129F}
const GUID c_guidModeButton = {
    0x3eb28cf1,
    0xab83,
    0x4e46,
    {0x8b, 0x78, 0xff, 0xdb, 0x28, 0xdc, 0x12, 0x9f}};

namespace {

constexpr wchar_t kChineseText[] = L"中";  // 中
constexpr wchar_t kWesternText[] = L"西";  // 西
constexpr wchar_t kChineseTip[] = TEXTSERVICE_DESC L"：中文（轻敲 Shift 切换）";
constexpr wchar_t kWesternTip[] = TEXTSERVICE_DESC L"：西文（轻敲 Shift 切换）";

}  // namespace

// ------------------------------------------------------------- CModeButton --

CModeButton::CModeButton(CTextService* service) : service_(service) {
  info_.clsidService = c_clsidTextService;
  info_.guidItem = c_guidModeButton;
  info_.dwStyle = TF_LBI_STYLE_BTN_BUTTON;
  info_.ulSort = 0;
  StringCchCopyW(info_.szDescription, ARRAYSIZE(info_.szDescription),
                 TEXTSERVICE_DESC);
  DllAddRef();
}

CModeButton::~CModeButton() {
  if (sink_) sink_->Release();
  DllRelease();
}

void CModeButton::Detach() { service_ = nullptr; }

STDAPI CModeButton::QueryInterface(REFIID riid, void** object) {
  if (!object) return E_INVALIDARG;
  *object = nullptr;
  if (IsEqualIID(riid, IID_IUnknown) ||
      IsEqualIID(riid, IID_ITfLangBarItem) ||
      IsEqualIID(riid, IID_ITfLangBarItemButton)) {
    *object = static_cast<ITfLangBarItemButton*>(this);
  } else if (IsEqualIID(riid, IID_ITfSource)) {
    *object = static_cast<ITfSource*>(this);
  }
  if (!*object) return E_NOINTERFACE;
  AddRef();
  return S_OK;
}

STDAPI_(ULONG) CModeButton::AddRef() { return InterlockedIncrement(&ref_); }

STDAPI_(ULONG) CModeButton::Release() {
  const LONG count = InterlockedDecrement(&ref_);
  if (count == 0) delete this;
  return count;
}

STDAPI CModeButton::GetInfo(TF_LANGBARITEMINFO* info) {
  if (!info) return E_INVALIDARG;
  *info = info_;
  return S_OK;
}

STDAPI CModeButton::GetStatus(DWORD* status) {
  if (!status) return E_INVALIDARG;
  *status = 0;
  return S_OK;
}

STDAPI CModeButton::Show(BOOL show) {
  shown_ = show;
  Refresh();
  return S_OK;
}

STDAPI CModeButton::GetTooltipString(BSTR* tooltip) {
  if (!tooltip) return E_INVALIDARG;
  const bool native = !service_ || service_->_IsNativeMode();
  *tooltip = SysAllocString(native ? kChineseTip : kWesternTip);
  return *tooltip ? S_OK : E_OUTOFMEMORY;
}

STDAPI CModeButton::OnClick(TfLBIClick click, POINT /*point*/,
                            const RECT* /*area*/) {
  if (click == TF_LBI_CLK_LEFT && service_) service_->_ToggleInputMode();
  return S_OK;
}

STDAPI CModeButton::InitMenu(ITfMenu* /*menu*/) { return E_NOTIMPL; }

STDAPI CModeButton::OnMenuSelect(UINT /*id*/) { return E_NOTIMPL; }

STDAPI CModeButton::GetIcon(HICON* icon) {
  if (!icon) return E_INVALIDARG;
  // The button shows text, not an icon; the shell falls back to GetText.
  *icon = nullptr;
  return S_FALSE;
}

STDAPI CModeButton::GetText(BSTR* text) {
  if (!text) return E_INVALIDARG;
  const bool native = !service_ || service_->_IsNativeMode();
  *text = SysAllocString(native ? kChineseText : kWesternText);
  return *text ? S_OK : E_OUTOFMEMORY;
}

STDAPI CModeButton::AdviseSink(REFIID riid, IUnknown* unknown, DWORD* cookie) {
  if (!IsEqualIID(riid, IID_ITfLangBarItemSink)) return CONNECT_E_CANNOTCONNECT;
  if (!unknown || !cookie) return E_INVALIDARG;
  if (sink_) return CONNECT_E_ADVISELIMIT;
  if (FAILED(unknown->QueryInterface(IID_ITfLangBarItemSink,
                                     reinterpret_cast<void**>(&sink_)))) {
    sink_ = nullptr;
    return E_NOINTERFACE;
  }
  sink_cookie_ = 0;
  *cookie = sink_cookie_;
  return S_OK;
}

STDAPI CModeButton::UnadviseSink(DWORD cookie) {
  if (cookie != sink_cookie_ || !sink_) return CONNECT_E_NOCONNECTION;
  sink_->Release();
  sink_ = nullptr;
  sink_cookie_ = TF_INVALID_COOKIE;
  return S_OK;
}

void CModeButton::Refresh() {
  if (sink_) sink_->OnUpdate(TF_LBI_STATUS | TF_LBI_TEXT | TF_LBI_TOOLTIP);
}

// ------------------------------------------------------- mode compartment --

namespace {

ITfCompartment* ModeCompartment(ITfThreadMgr* thread_mgr) {
  if (!thread_mgr) return nullptr;
  ITfCompartmentMgr* manager = nullptr;
  if (FAILED(thread_mgr->QueryInterface(
          IID_ITfCompartmentMgr, reinterpret_cast<void**>(&manager))) ||
      !manager) {
    return nullptr;
  }
  ITfCompartment* compartment = nullptr;
  manager->GetCompartment(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION,
                          &compartment);
  manager->Release();
  return compartment;
}

}  // namespace

bool CTextService::_IsNativeMode() {
  if (!_pModeCompartment) return true;  // Chinese until told otherwise
  VARIANT value;
  VariantInit(&value);
  bool native = true;
  if (_pModeCompartment->GetValue(&value) == S_OK && value.vt == VT_I4) {
    native = (value.lVal & TF_CONVERSIONMODE_NATIVE) != 0;
  }
  VariantClear(&value);
  return native;
}

void CTextService::_SetNativeMode(bool native) {
  if (!_pModeCompartment) return;
  VARIANT current;
  VariantInit(&current);
  LONG flags = TF_CONVERSIONMODE_NATIVE;
  if (_pModeCompartment->GetValue(&current) == S_OK && current.vt == VT_I4) {
    flags = current.lVal;
  }
  VariantClear(&current);

  // Only the native bit is ours; the rest of the conversion flags belong to
  // whatever else the shell tracks.
  flags = native ? (flags | TF_CONVERSIONMODE_NATIVE)
                 : (flags & ~TF_CONVERSIONMODE_NATIVE);

  VARIANT value;
  VariantInit(&value);
  value.vt = VT_I4;
  value.lVal = flags;
  _pModeCompartment->SetValue(_tfClientId, &value);
  VariantClear(&value);
}

void CTextService::_ToggleInputMode() {
  const bool native = _IsNativeMode();

  // Leaving Chinese mode mid-code would strand the pre-edit text in the
  // application. _EndComposition needs the focused context, which a language
  // bar click does not hand us, so look it up.
  if (native && _IsComposing() && _pThreadMgr) {
    ITfDocumentMgr* focused = nullptr;
    if (SUCCEEDED(_pThreadMgr->GetFocus(&focused)) && focused) {
      ITfContext* context = nullptr;
      if (SUCCEEDED(focused->GetTop(&context)) && context) {
        _EndComposition(context);
        context->Release();
      }
      focused->Release();
    }
  }

  _SetNativeMode(!native);
  // _ApplyInputMode runs from the compartment sink, which fires in every
  // process, so all the engines stay in step.
}

void CTextService::_ApplyInputMode() {
  const bool native = _IsNativeMode();
  if (_EngineReady()) _Engine().SetAsciiMode(!native);
  if (!native) _HideCandidateWindow();
  if (lang_bar_) lang_bar_->Refresh();
  zuxia::LogEvent(L"mode", native ? L"chinese" : L"western");
}

STDMETHODIMP CTextService::OnChange(REFGUID guid) {
  if (IsEqualGUID(guid, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION)) {
    _ApplyInputMode();
  }
  return S_OK;
}

BOOL CTextService::_InitInputMode() {
  _pModeCompartment = ModeCompartment(_pThreadMgr);
  if (!_pModeCompartment) {
    zuxia::LogEvent(L"mode-compartment-failed");
    return FALSE;
  }

  ITfSource* source = nullptr;
  if (SUCCEEDED(_pModeCompartment->QueryInterface(
          IID_ITfSource, reinterpret_cast<void**>(&source))) &&
      source) {
    source->AdviseSink(IID_ITfCompartmentEventSink,
                       static_cast<ITfCompartmentEventSink*>(this),
                       &_dwModeSinkCookie);
    source->Release();
  }

  // A compartment that nobody has written yet reads as VT_EMPTY, which would
  // leave the shell without a mode to display. Claim Chinese explicitly.
  VARIANT value;
  VariantInit(&value);
  const bool unset = !(_pModeCompartment->GetValue(&value) == S_OK &&
                       value.vt == VT_I4);
  VariantClear(&value);
  if (unset) _SetNativeMode(true);

  _ApplyInputMode();
  return TRUE;
}

void CTextService::_UninitInputMode() {
  if (_pModeCompartment) {
    if (_dwModeSinkCookie != TF_INVALID_COOKIE) {
      ITfSource* source = nullptr;
      if (SUCCEEDED(_pModeCompartment->QueryInterface(
              IID_ITfSource, reinterpret_cast<void**>(&source))) &&
          source) {
        source->UnadviseSink(_dwModeSinkCookie);
        source->Release();
      }
      _dwModeSinkCookie = TF_INVALID_COOKIE;
    }
    _pModeCompartment->Release();
    _pModeCompartment = nullptr;
  }
}

// --------------------------------------------------------- language bar --

BOOL CTextService::_InitLanguageBar() {
  ITfLangBarItemMgr* manager = nullptr;
  if (FAILED(_pThreadMgr->QueryInterface(
          IID_ITfLangBarItemMgr, reinterpret_cast<void**>(&manager))) ||
      !manager) {
    return FALSE;
  }
  lang_bar_ = new (std::nothrow) CModeButton(this);
  if (!lang_bar_) {
    manager->Release();
    return FALSE;
  }
  const HRESULT result = manager->AddItem(lang_bar_);
  manager->Release();
  if (FAILED(result)) {
    zuxia::LogFailure(L"langbar-failed", static_cast<unsigned long>(result));
    lang_bar_->Detach();
    lang_bar_->Release();
    lang_bar_ = nullptr;
    return FALSE;
  }
  return TRUE;
}

void CTextService::_UninitLanguageBar() {
  if (!lang_bar_) return;
  ITfLangBarItemMgr* manager = nullptr;
  if (SUCCEEDED(_pThreadMgr->QueryInterface(
          IID_ITfLangBarItemMgr, reinterpret_cast<void**>(&manager))) &&
      manager) {
    manager->RemoveItem(lang_bar_);
    manager->Release();
  }
  lang_bar_->Detach();
  lang_bar_->Release();
  lang_bar_ = nullptr;
}
