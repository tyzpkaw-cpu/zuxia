#include "Diagnostics.h"
#include "Globals.h"
#include "TextService.h"

#include <exception>

namespace {

bool HasShortcutModifier() {
  return (GetKeyState(VK_CONTROL) & 0x8000) != 0 ||
         (GetKeyState(VK_MENU) & 0x8000) != 0 ||
         (GetKeyState(VK_LWIN) & 0x8000) != 0 ||
         (GetKeyState(VK_RWIN) & 0x8000) != 0;
}

bool IsCompositionControlKey(WPARAM key) {
  switch (key) {
    case VK_BACK:
    case VK_DELETE:
    case VK_RETURN:
    case VK_ESCAPE:
    case VK_SPACE:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
      return true;
    default:
      return key >= '0' && key <= '9';
  }
}

}  // namespace

// Shift 轻敲切换中/西，这是靠 TF_MOD_ON_KEYUP 注册的预留键实现的。TSF 的
// 文档说它只在「Shift 按下又松开、中间没有别的键」时才触发 —— 真机把这个
// 说法证伪了：中文标点 ：？！（）""《》 每一个都要按住 Shift 才打得出，而
// 这些键被我们自己吃掉了，没有作为普通按键回到 TSF 的账上，于是松开 Shift
// 时预留键照样触发，输入法就被翻到西文。表现就是「打完一个标点忽然变英文」。
//
// 所以按键得我们自己记：Shift 按着的时候只要还按过别的键，这一次就不是
// 轻敲。标志位在 OnPreservedKey 里用一次即清；另外只要看到一次没按 Shift
// 的按键就顺手清掉，免得某次预留键没触发让标志位一直挂着，白吞掉下一次
// 真正的轻敲。
void CTextService::_NoteKeyForShiftTap(WPARAM key) {
  if (key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT) return;
  if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
    _shiftUsedWithKey = true;
  } else {
    _shiftUsedWithKey = false;
  }
}

BOOL CTextService::_IsKeyEaten(ITfContext* /*context*/, WPARAM key) try {
  const bool ready = _EngineReady();
  const bool disabled = _IsKeyboardDisabled() != FALSE;
  const bool open = _IsKeyboardOpen() != FALSE;
  const bool shortcut = HasShortcutModifier();
  const bool ascii = !_IsNativeMode();

  // One line per process, the first time a key is judged. It records why the
  // decision went the way it did -- never which key it was.
  static LONG reported = 0;
  if (InterlockedCompareExchange(&reported, 1, 0) == 0) {
    wchar_t gates[96] = {};
    swprintf_s(gates, L"ready=%d disabled=%d open=%d shortcut=%d ascii=%d",
               ready ? 1 : 0, disabled ? 1 : 0, open ? 1 : 0, shortcut ? 1 : 0,
               ascii ? 1 : 0);
    zuxia::LogEvent(L"first-key", gates);
  }

  if (!ready || disabled || !open || shortcut) return FALSE;

  // Western mode: the text service is transparent and every key belongs to
  // the application.
  if (ascii) return FALSE;

  // Letters always begin or extend a Zuxia code. Shift is intentionally
  // ignored; codes are normalized to lowercase before reaching librime.
  if (key >= 'A' && key <= 'Z') return TRUE;

  // Punctuation goes to the engine so it comes out in its Chinese form --
  // typing a comma while writing Chinese should give ， not , -- and it does
  // so whether or not a code is being composed.
  if (zuxia::RimeEngine::IsPunctuationKey(key)) return TRUE;

  if (!_EngineComposing() && !_IsComposing()) return FALSE;
  return IsCompositionControlKey(key) ? TRUE : FALSE;
} catch (...) {
  // Never let an exception cross back into TSF: it would terminate the host
  // application. Declining the key is always a safe answer.
  zuxia::LogEvent(L"exception", L"_IsKeyEaten");
  return FALSE;
}

STDMETHODIMP CTextService::OnSetFocus(BOOL foreground) ZUXIA_COM_GUARD_BEGIN
  if (!foreground) _HideCandidateWindow();
  // 按住 Shift 的时候切走窗口，那次 Shift 松开可能落到别的线程去，标志位
  // 会一直挂着，白吞掉下一次真正的轻敲。换焦点就当这一轮结束。
  _shiftUsedWithKey = false;
  return S_OK;
ZUXIA_COM_GUARD_END(L"CTextService::OnSetFocus(bool)", S_OK)

STDMETHODIMP CTextService::OnTestKeyDown(ITfContext* context, WPARAM key,
                                         LPARAM /*flags*/,
                                         BOOL* eaten) ZUXIA_COM_GUARD_BEGIN
  if (!eaten) return E_INVALIDARG;
  _NoteKeyForShiftTap(key);
  *eaten = _IsKeyEaten(context, key);
  return S_OK;
ZUXIA_COM_GUARD_END(L"CTextService::OnTestKeyDown", S_OK)

STDMETHODIMP CTextService::OnKeyDown(ITfContext* context, WPARAM key,
                                     LPARAM flags, BOOL* eaten) try {
  if (!eaten) return E_INVALIDARG;
  _NoteKeyForShiftTap(key);
  *eaten = _IsKeyEaten(context, key);
  if (*eaten) {
    // S_OK 之外的每一种返回都表示「引擎没吃下这一键」—— _InvokeKeyHandler
    // 现在保证了这一点。引擎吃下了却写不进文档时它返回 S_OK 并自己收拾，
    // 因为把键交还给应用会让字母原样落进文档而 Rime 那边还留着它。
    const HRESULT result = _InvokeKeyHandler(context, key, flags);
    if (result != S_OK) *eaten = FALSE;
  }
  return S_OK;
} catch (...) {
  zuxia::LogEvent(L"exception", L"OnKeyDown");
  if (eaten) *eaten = FALSE;
  return S_OK;
}

STDMETHODIMP CTextService::OnTestKeyUp(ITfContext* /*context*/, WPARAM /*key*/,
                                       LPARAM /*flags*/, BOOL* eaten) {
  if (!eaten) return E_INVALIDARG;
  *eaten = FALSE;
  return S_OK;
}

STDMETHODIMP CTextService::OnKeyUp(ITfContext* /*context*/, WPARAM /*key*/,
                                   LPARAM /*flags*/, BOOL* eaten) {
  if (!eaten) return E_INVALIDARG;
  *eaten = FALSE;
  return S_OK;
}

STDMETHODIMP CTextService::OnPreservedKey(ITfContext* context, REFGUID guid,
                                          BOOL* eaten) try {
  if (!eaten) return E_INVALIDARG;
  *eaten = FALSE;
  if (!IsEqualGUID(guid, c_guidToggleAsciiKey)) return S_OK;
  if (!_EngineReady() || _IsKeyboardDisabled()) return S_OK;

  // Shift＋标点不是轻敲。标志位用一次就清，这样下一次真的轻敲能切换。
  if (_shiftUsedWithKey) {
    _shiftUsedWithKey = false;
    return S_OK;
  }

  // Leaving Chinese mode mid-code would otherwise strand the pre-edit text in
  // the application.
  if (_IsComposing()) _EndComposition(context);
  _HideCandidateWindow();
  // Writes the session-wide compartment; the resulting OnChange applies the
  // mode to every process, this one included.
  _ToggleInputMode();
  *eaten = TRUE;
  return S_OK;
} catch (...) {
  zuxia::LogEvent(L"exception", L"OnPreservedKey");
  if (eaten) *eaten = FALSE;
  return S_OK;
}

BOOL CTextService::_InitKeyEventSink() {
  if (!_pThreadMgr) return FALSE;
  ITfKeystrokeMgr* manager = nullptr;
  if (FAILED(_pThreadMgr->QueryInterface(
          IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&manager)))) {
    return FALSE;
  }
  const HRESULT result = manager->AdviseKeyEventSink(
      _tfClientId, static_cast<ITfKeyEventSink*>(this), TRUE);
  manager->Release();
  return SUCCEEDED(result);
}

void CTextService::_UninitKeyEventSink() {
  if (!_pThreadMgr) return;
  ITfKeystrokeMgr* manager = nullptr;
  if (SUCCEEDED(_pThreadMgr->QueryInterface(
          IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&manager)))) {
    manager->UnadviseKeyEventSink(_tfClientId);
    manager->Release();
  }
}

// A tapped Shift switches between Chinese and Western input, delivered through
// a TF_MOD_ON_KEYUP preserved key.
//
// 注意：TSF 的文档说这个只在「Shift 按下又松开、中间没有别的键」时触发。
// 真机把这个说法证伪了 —— 被我们自己吃掉的键不会作为普通按键回到 TSF 的
// 账上，于是 Shift＋标点之后它照样触发。别删 _NoteKeyForShiftTap，那个标志
// 位就是补这个的；删了就回到「打一个中文标点忽然变英文」。
BOOL CTextService::_InitPreservedKey() {
  if (!_pThreadMgr) return FALSE;
  ITfKeystrokeMgr* manager = nullptr;
  if (FAILED(_pThreadMgr->QueryInterface(
          IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&manager)))) {
    return FALSE;
  }
  TF_PRESERVEDKEY key = {};
  key.uVKey = VK_SHIFT;
  key.uModifiers = TF_MOD_ON_KEYUP;
  const HRESULT result = manager->PreserveKey(
      _tfClientId, c_guidToggleAsciiKey, &key, TEXTSERVICE_ASCII_KEY_DESC,
      static_cast<ULONG>(wcslen(TEXTSERVICE_ASCII_KEY_DESC)));
  manager->Release();
  // A failure here costs the toggle, not the text service.
  if (FAILED(result)) {
    // Commonly TF_E_ALREADY_EXISTS when another input method already holds
    // the key. The caller treats this as non-fatal.
    zuxia::LogFailure(L"preserved-key-failed",
                       static_cast<unsigned long>(result));
  } else {
    zuxia::LogEvent(L"preserved-key", L"Shift registered");
  }
  return SUCCEEDED(result);
}

void CTextService::_UninitPreservedKey() {
  if (!_pThreadMgr) return;
  ITfKeystrokeMgr* manager = nullptr;
  if (SUCCEEDED(_pThreadMgr->QueryInterface(
          IID_ITfKeystrokeMgr, reinterpret_cast<void**>(&manager)))) {
    TF_PRESERVEDKEY key = {};
    key.uVKey = VK_SHIFT;
    key.uModifiers = TF_MOD_ON_KEYUP;
    manager->UnpreserveKey(c_guidToggleAsciiKey, &key);
    manager->Release();
  }
}
