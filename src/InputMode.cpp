#include "InputMode.h"

#include <olectl.h>
#include <shellapi.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "Diagnostics.h"
#include "Globals.h"
#include "Settings.h"
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

// Windows 11 的任务栏输入指示器画的是 GetIcon 交出来的图标，GetText 那一行字
// 它根本不看。不给图标，它就退回去显示语言缩写 —— 那就是任务栏上那个「简体」
// 的来历，跟这个输入法叫什么、处于什么模式都没关系。
//
// 图标在运行时画，不预先烤两张 .ico 塞进资源：尺寸要跟着 DPI 走，颜色要跟着
// 系统深浅色走，字还要让用户能在设置文件里改。
bool SystemUsesLightTheme() {
  HKEY key = nullptr;
  DWORD value = 1;  // 读不到就当浅色，配深色字 —— 浅底上至少看得见
  DWORD size = sizeof(value);
  DWORD type = REG_DWORD;
  if (RegOpenKeyExW(
          HKEY_CURRENT_USER,
          L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
          0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
    RegQueryValueExW(key, L"SystemUsesLightTheme", nullptr, &type,
                     reinterpret_cast<BYTE*>(&value), &size);
    RegCloseKey(key);
  }
  return value != 0;
}

HICON RenderGlyphIcon(const std::wstring& glyph) {
  if (glyph.empty()) return nullptr;
  int size = GetSystemMetrics(SM_CXSMICON);
  if (size <= 0) size = 16;

  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = size;
  info.bmiHeader.biHeight = -size;  // 自上而下，省得翻转
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;

  void* bits = nullptr;
  HBITMAP color =
      CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!color || !bits) {
    if (color) DeleteObject(color);
    return nullptr;
  }
  memset(bits, 0, static_cast<size_t>(size) * static_cast<size_t>(size) * 4);

  HDC dc = CreateCompatibleDC(nullptr);
  if (!dc) {
    DeleteObject(color);
    return nullptr;
  }
  HGDIOBJ old_bitmap = SelectObject(dc, color);
  // ANTIALIASED 而不是 CLEARTYPE：ClearType 的彩色边缘会让下面那步「拿亮度当
  // 透明度」算出带颜色的毛边。
  HFONT font = CreateFontW(-MulDiv(size, 7, 8), 0, 0, 0, FW_SEMIBOLD, FALSE,
                           FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
  HGDIOBJ old_font = font ? SelectObject(dc, font) : nullptr;
  SetBkMode(dc, TRANSPARENT);
  // 先一律画白字：白色的亮度正好就是抗锯齿的覆盖率，拿来当 alpha。
  SetTextColor(dc, RGB(255, 255, 255));
  RECT box = {0, 0, size, size};
  DrawTextW(dc, glyph.c_str(), static_cast<int>(glyph.size()), &box,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
  if (old_font) SelectObject(dc, old_font);
  if (font) DeleteObject(font);
  SelectObject(dc, old_bitmap);
  DeleteDC(dc);

  const bool dark_glyph = SystemUsesLightTheme();
  auto* pixels = static_cast<unsigned char*>(bits);
  for (int i = 0; i < size * size; ++i) {
    unsigned char* p = pixels + static_cast<size_t>(i) * 4;
    const unsigned char coverage = (std::max)({p[0], p[1], p[2]});
    const unsigned char value = dark_glyph ? 0 : coverage;
    p[0] = value;  // 预乘 alpha：深色任务栏上白字，浅色任务栏上黑字
    p[1] = value;
    p[2] = value;
    p[3] = coverage;
  }

  // 单色掩码全 0 = 处处不透明，真正的透明交给上面的 alpha 通道。
  // 单色位图每行按 WORD 对齐，缓冲区大小得按这个算。
  const size_t stride = static_cast<size_t>((size + 15) / 16) * 2;
  std::vector<unsigned char> mask_bits(stride * static_cast<size_t>(size), 0);
  HBITMAP mask = CreateBitmap(size, size, 1, 1, mask_bits.data());
  if (!mask) {
    DeleteObject(color);
    return nullptr;
  }
  ICONINFO icon_info = {};
  icon_info.fIcon = TRUE;
  icon_info.hbmColor = color;
  icon_info.hbmMask = mask;
  HICON icon = CreateIconIndirect(&icon_info);
  DeleteObject(color);
  DeleteObject(mask);
  return icon;
}

// 设置程序和这个 DLL 放在同一个目录里（x64\ 或 x86\），谁被载入就起谁，
// 位数天然对得上。起成独立进程是刻意的：这段代码跑在 Word、浏览器的进程
// 里，绝不能在这里开窗口 —— 设置界面卡一下，宿主就跟着卡。
void LaunchSettings() {
  wchar_t path[MAX_PATH] = {};
  const DWORD length = GetModuleFileNameW(g_hInst, path, ARRAYSIZE(path));
  if (length == 0 || length >= ARRAYSIZE(path)) return;
  std::wstring exe(path, length);
  const size_t slash = exe.find_last_of(L'\\');
  if (slash == std::wstring::npos) return;
  exe.resize(slash + 1);
  exe += L"ZuxiaSettings.exe";
  // 起不来就算了：设置文件手改照样有效，不值得为此弹个错误框打断打字。
  ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace

// ------------------------------------------------------------- CModeButton --

CModeButton::CModeButton(CTextService* service) : service_(service) {
  info_.clsidService = c_clsidTextService;
  info_.guidItem = c_guidModeButton;
  // TF_LBI_STYLE_SHOWNINTRAY 是任务栏那个输入指示器愿意显示这个按钮图标的
  // 条件。少了它，这一项只存在于早就默认隐藏的旧版语言栏里，指示器拿不到
  // 图标就退回去显示语言缩写 —— 真机上看到的「简体」而不是「足」，来源就
  // 在这里。微软自己的 SampleIME 也是 BTN_BUTTON | SHOWNINTRAY 两个一起给。
  info_.dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;
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

STDAPI CModeButton::GetTooltipString(BSTR* tooltip) ZUXIA_COM_GUARD_BEGIN
  if (!tooltip) return E_INVALIDARG;
  const bool native = !service_ || service_->_IsNativeMode();
  *tooltip = SysAllocString(native ? kChineseTip : kWesternTip);
  return *tooltip ? S_OK : E_OUTOFMEMORY;
ZUXIA_COM_GUARD_END(L"CModeButton::GetTooltipString", E_FAIL)

STDAPI CModeButton::OnClick(TfLBIClick click, POINT /*point*/,
                            const RECT* /*area*/) ZUXIA_COM_GUARD_BEGIN
  if (click == TF_LBI_CLK_LEFT && service_) service_->_ToggleInputMode();
  // 右键 = 设置。语言栏上这个按钮是输入法在系统里唯一固定的抓手，
  // 开始菜单那个快捷方式被用户删了，还能从这里进去。
  if (click == TF_LBI_CLK_RIGHT) LaunchSettings();
  return S_OK;
ZUXIA_COM_GUARD_END(L"CModeButton::OnClick", E_FAIL)

STDAPI CModeButton::InitMenu(ITfMenu* /*menu*/) { return E_NOTIMPL; }

STDAPI CModeButton::OnMenuSelect(UINT /*id*/) { return E_NOTIMPL; }

STDAPI CModeButton::GetIcon(HICON* icon) ZUXIA_COM_GUARD_BEGIN
  if (!icon) return E_INVALIDARG;
  *icon = nullptr;
  const bool native = !service_ || service_->_IsNativeMode();
  const zuxia::Appearance look = zuxia::CurrentAppearance();
  *icon = RenderGlyphIcon(native ? look.tray_chinese : look.tray_western);
  // 图标的所有权交给调用方，由它 DestroyIcon —— 这是 ITfLangBarItemButton
  // 的约定，所以每次都画一张新的，不能缓存着重复交出去。
  return *icon ? S_OK : S_FALSE;
ZUXIA_COM_GUARD_END(L"CModeButton::GetIcon", E_FAIL)

STDAPI CModeButton::GetText(BSTR* text) ZUXIA_COM_GUARD_BEGIN
  if (!text) return E_INVALIDARG;
  const bool native = !service_ || service_->_IsNativeMode();
  *text = SysAllocString(native ? kChineseText : kWesternText);
  return *text ? S_OK : E_OUTOFMEMORY;
ZUXIA_COM_GUARD_END(L"CModeButton::GetText", E_FAIL)

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
  if (sink_) {
    sink_->OnUpdate(TF_LBI_STATUS | TF_LBI_TEXT | TF_LBI_TOOLTIP |
                    TF_LBI_ICON);
  }
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
  // 切到西文时光清 librime 不够：TSF 那边的组字范围还留着一串码，它会变成
  // 文档里清不掉的死文本；解码器的候选覆盖层也还挂着，之后的数字键会被它
  // 截下去选一个早就不在屏幕上的候选。
  //
  // 这个函数是 compartment 的回调，别的进程（或同进程的别的线程）翻模式时
  // 本进程也会走到这里 —— 而 _ToggleInputMode 里那段收尾只在按 Shift 的那
  // 个线程上跑。所以收尾必须在这里做一次。
  if (!native && (_IsComposing() || _EngineComposing()) && _pThreadMgr) {
    ITfDocumentMgr* focused = nullptr;
    if (SUCCEEDED(_pThreadMgr->GetFocus(&focused)) && focused) {
      ITfContext* context = nullptr;
      if (SUCCEEDED(focused->GetTop(&context)) && context) {
        _EndComposition(context);  // 里面会 engine_.Clear() 并隐藏候选窗
        context->Release();
      }
      focused->Release();
    }
  }
  if (_EngineReady()) _Engine().SetAsciiMode(!native);
  if (!native) _HideCandidateWindow();
  if (lang_bar_) lang_bar_->Refresh();
  zuxia::LogEvent(L"mode", native ? L"chinese" : L"western");
}

STDMETHODIMP CTextService::OnChange(REFGUID guid) ZUXIA_COM_GUARD_BEGIN
  if (IsEqualGUID(guid, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION)) {
    _ApplyInputMode();
  }
  return S_OK;
ZUXIA_COM_GUARD_END(L"CTextService::OnChange", S_OK)

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
