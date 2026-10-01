// ZuxiaSettings.exe -- Zuxia IME settings window.

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <cstdlib>
#include <string>
#include "Settings.h"

namespace {

const wchar_t kClassName[] = L"ZuxiaSettingsWindow";
const wchar_t kTitle[]     = L"\u5e94\u7269\u97f3\u5f62\u8db3\u4e0b\u8f93\u5165\u6cd5 \u00b7 \u8bbe\u7f6e";

enum ControlId : int {
  kFontText = 1001, kFontPick,
  kVertical, kHorizontal,
  kRowHeight, kPadding, kMinWidth, kMaxWidth,
  kPresetSystem, kPresetLight, kPresetDark, kPresetEye, kPresetContrast,
  kBackgroundSystem, kBackgroundColor,
  kTextSystem, kTextColor,
  kDimSystem, kDimColor,
  kHighlightBg, kHighlightFg,
  kTrayChinese, kTrayWestern,
  kShowPartsWindow,
  kPartsVertical, kPartsHorizontal,   // new
  kPreview,
  kRestoreDefaults, kOpenFile, kApply, kConfirm, kCancel,
};

zuxia::Appearance g_look;
zuxia::Appearance g_original;
bool g_applied = false;
int  g_dpi = 96;
HFONT g_ui_font      = nullptr;
HFONT g_section_font = nullptr;
HWND  g_main         = nullptr;

constexpr int kContentWidth  = 520;
constexpr int kContentHeight = 840;  // +40 for direction radio

int g_content_px = 0;
int g_scroll_pos = 0;

int S(int v) { return MulDiv(v, g_dpi, 96); }

HWND Add(HWND p, const wchar_t* cls, const wchar_t* text, DWORD style,
         int x, int y, int w, int h, int id, HFONT font) {
  HWND c = CreateWindowExW(0, cls, text, style | WS_CHILD | WS_VISIBLE,
                           S(x), S(y), S(w), S(h), p,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           reinterpret_cast<HINSTANCE>(
                               GetWindowLongPtrW(p, GWLP_HINSTANCE)), nullptr);
  if (c) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  return c;
}
HWND Label(HWND p, const wchar_t* t, int x, int y, int w, int h) {
  return Add(p, L"STATIC", t, SS_LEFT, x, y, w, h, -1, g_ui_font);
}
void SetInt(HWND p, int id, int v) {
  SetDlgItemTextW(p, id, std::to_wstring(v).c_str());
}
int GetInt(HWND p, int id, int fb, int lo, int hi) {
  wchar_t buf[32] = {}; GetDlgItemTextW(p, id, buf, 32);
  wchar_t* e = nullptr; long v = wcstol(buf, &e, 10);
  if (e == buf) return fb;
  return static_cast<int>(v < lo ? lo : v > hi ? hi : v);
}
std::wstring GetText(HWND p, int id, size_t lim) {
  wchar_t buf[128] = {}; GetDlgItemTextW(p, id, buf, 128);
  std::wstring t(buf); if (t.size() > lim) t.resize(lim); return t;
}
COLORREF Resolve(bool sys, COLORREF c, int idx) {
  return sys ? GetSysColor(idx) : c;
}

void PushLookToControls(HWND hwnd) {
  const std::wstring cap = g_look.font + L"    " +
                           std::to_wstring(g_look.font_size) + L" px";
  SetDlgItemTextW(hwnd, kFontText, cap.c_str());
  CheckDlgButton(hwnd, kVertical,
                 g_look.horizontal ? BST_UNCHECKED : BST_CHECKED);
  CheckDlgButton(hwnd, kHorizontal,
                 g_look.horizontal ? BST_CHECKED : BST_UNCHECKED);
  SetInt(hwnd, kRowHeight, g_look.row_height);
  SetInt(hwnd, kPadding,   g_look.padding);
  SetInt(hwnd, kMinWidth,  g_look.min_width);
  SetInt(hwnd, kMaxWidth,  g_look.max_width);
  CheckDlgButton(hwnd, kBackgroundSystem,
                 g_look.system_background ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, kTextSystem,
                 g_look.system_text ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, kDimSystem,
                 g_look.system_dim ? BST_CHECKED : BST_UNCHECKED);
  EnableWindow(GetDlgItem(hwnd, kBackgroundColor), !g_look.system_background);
  EnableWindow(GetDlgItem(hwnd, kTextColor),       !g_look.system_text);
  EnableWindow(GetDlgItem(hwnd, kDimColor),        !g_look.system_dim);
  SetDlgItemTextW(hwnd, kTrayChinese, g_look.tray_chinese.c_str());
  SetDlgItemTextW(hwnd, kTrayWestern, g_look.tray_western.c_str());
  CheckDlgButton(hwnd, kShowPartsWindow,
                 g_look.show_parts_window ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, kPartsVertical,
                 g_look.parts_vertical ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, kPartsHorizontal,
                 g_look.parts_vertical ? BST_UNCHECKED : BST_CHECKED);
  const int repaint[] = {kBackgroundColor, kTextColor, kDimColor,
                         kHighlightBg, kHighlightFg, kPreview};
  for (int id : repaint) {
    HWND ch = GetDlgItem(hwnd, id);
    if (ch) InvalidateRect(ch, nullptr, TRUE);
  }
}

void PullLookFromControls(HWND hwnd) {
  g_look.horizontal  = IsDlgButtonChecked(hwnd, kHorizontal) == BST_CHECKED;
  g_look.row_height  = GetInt(hwnd, kRowHeight, g_look.row_height, 12, 200);
  g_look.padding     = GetInt(hwnd, kPadding,   g_look.padding,    0,  64);
  g_look.min_width   = GetInt(hwnd, kMinWidth,  g_look.min_width,  80, 2000);
  g_look.max_width   = GetInt(hwnd, kMaxWidth,  g_look.max_width,  80, 4000);
  if (g_look.max_width < g_look.min_width) g_look.max_width = g_look.min_width;
  g_look.system_background =
      IsDlgButtonChecked(hwnd, kBackgroundSystem) == BST_CHECKED;
  g_look.system_text = IsDlgButtonChecked(hwnd, kTextSystem) == BST_CHECKED;
  g_look.system_dim  = IsDlgButtonChecked(hwnd, kDimSystem)  == BST_CHECKED;
  const std::wstring cn = GetText(hwnd, kTrayChinese, 2);
  const std::wstring en = GetText(hwnd, kTrayWestern, 2);
  if (!cn.empty()) g_look.tray_chinese = cn;
  if (!en.empty()) g_look.tray_western = en;
  g_look.show_parts_window =
      IsDlgButtonChecked(hwnd, kShowPartsWindow) == BST_CHECKED;
  g_look.parts_vertical =
      IsDlgButtonChecked(hwnd, kPartsVertical) == BST_CHECKED;
}

void ApplyPreset(int id) {
  switch (id) {
    case kPresetSystem:
      g_look.system_background = g_look.system_text = g_look.system_dim = true;
      g_look.highlight_bg = RGB(35, 104, 190); g_look.highlight_fg = RGB(255,255,255);
      break;
    case kPresetLight:
      g_look.system_background = g_look.system_text = g_look.system_dim = false;
      g_look.background = RGB(255,255,255); g_look.text = RGB(32,32,32);
      g_look.dim = RGB(130,130,130);
      g_look.highlight_bg = RGB(35,104,190); g_look.highlight_fg = RGB(255,255,255);
      break;
    case kPresetDark:
      g_look.system_background = g_look.system_text = g_look.system_dim = false;
      g_look.background = RGB(32,32,32); g_look.text = RGB(235,235,235);
      g_look.dim = RGB(150,150,150);
      g_look.highlight_bg = RGB(0,120,212); g_look.highlight_fg = RGB(255,255,255);
      break;
    case kPresetEye:
      g_look.system_background = g_look.system_text = g_look.system_dim = false;
      g_look.background = RGB(199,237,204); g_look.text = RGB(38,60,42);
      g_look.dim = RGB(96,125,102);
      g_look.highlight_bg = RGB(58,122,74); g_look.highlight_fg = RGB(255,255,255);
      break;
    case kPresetContrast:
      g_look.system_background = g_look.system_text = g_look.system_dim = false;
      g_look.background = RGB(0,0,0); g_look.text = RGB(255,255,255);
      g_look.dim = RGB(255,255,0);
      g_look.highlight_bg = RGB(255,255,0); g_look.highlight_fg = RGB(0,0,0);
      break;
    default: break;
  }
}

bool PickColor(HWND hwnd, COLORREF* color) {
  static COLORREF custom[16] = {};
  CHOOSECOLORW ch = {};
  ch.lStructSize = sizeof(ch); ch.hwndOwner = hwnd;
  ch.rgbResult = *color; ch.lpCustColors = custom;
  ch.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
  if (!ChooseColorW(&ch)) return false;
  *color = ch.rgbResult; return true;
}

bool PickFont(HWND hwnd) {
  LOGFONTW lf = {};
  lf.lfHeight = -S(g_look.font_size); lf.lfCharSet = DEFAULT_CHARSET;
  lstrcpynW(lf.lfFaceName, g_look.font.c_str(), LF_FACESIZE);
  CHOOSEFONTW cf = {};
  cf.lStructSize = sizeof(cf); cf.hwndOwner = hwnd; cf.lpLogFont = &lf;
  cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_NOSCRIPTSEL | CF_NOVERTFONTS;
  if (!ChooseFontW(&cf)) return false;
  g_look.font = lf.lfFaceName;
  int h = lf.lfHeight < 0 ? -lf.lfHeight : lf.lfHeight;
  int sz = MulDiv(h, 96, g_dpi);
  g_look.font_size = sz < 8 ? 8 : sz > 72 ? 72 : sz;
  return true;
}

void DrawPreview(const DRAWITEMSTRUCT* item) {
  HDC dc = item->hDC; RECT box = item->rcItem;
  COLORREF back = Resolve(g_look.system_background, g_look.background, COLOR_WINDOW);
  COLORREF fore = Resolve(g_look.system_text, g_look.text, COLOR_WINDOWTEXT);
  COLORREF dim  = Resolve(g_look.system_dim,  g_look.dim,  COLOR_GRAYTEXT);
  HBRUSH br = CreateSolidBrush(back); FillRect(dc, &box, br); DeleteObject(br);
  FrameRect(dc, &box, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
  HFONT font = CreateFontW(-S(g_look.font_size), 0,0,0, FW_NORMAL, FALSE,FALSE,FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH|FF_DONTCARE, g_look.font.c_str());
  HGDIOBJ old = SelectObject(dc, font); SetBkMode(dc, TRANSPARENT);
  const int pad = S(g_look.padding), row = S(g_look.row_height);
  int y = box.top + pad;
  RECT line = {box.left+pad, y, box.right-pad, y+row};
  SetTextColor(dc, dim);
  DrawTextW(dc, L"qingzs", -1, &line, DT_LEFT|DT_VCENTER|DT_SINGLELINE); y += row;
  static const wchar_t* kI[] = {L"1 \u6e05", L"2 \u60c5", L"3 \u8bf7"};
  if (g_look.horizontal) {
    int x = box.left + pad;
    for (int i = 0; i < 3; ++i) {
      SIZE sz = {}; GetTextExtentPoint32W(dc, kI[i], lstrlenW(kI[i]), &sz);
      RECT cell = {x, y, x+sz.cx+S(16), y+row};
      if (cell.right > box.right-pad) break;
      if (i == 0) { HBRUSH h = CreateSolidBrush(g_look.highlight_bg);
                    FillRect(dc, &cell, h); DeleteObject(h);
                    SetTextColor(dc, g_look.highlight_fg); }
      else SetTextColor(dc, fore);
      DrawTextW(dc, kI[i], -1, &cell, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
      x = cell.right;
    }
  } else {
    for (int i = 0; i < 3; ++i) {
      RECT cell = {box.left+pad, y, box.right-pad, y+row};
      if (cell.bottom > box.bottom-pad) break;
      if (i == 0) { HBRUSH h = CreateSolidBrush(g_look.highlight_bg);
                    FillRect(dc, &cell, h); DeleteObject(h);
                    SetTextColor(dc, g_look.highlight_fg); }
      else SetTextColor(dc, fore);
      RECT t = cell; t.left += S(8);
      DrawTextW(dc, kI[i], -1, &t, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
      y = cell.bottom;
    }
  }
  SelectObject(dc, old); DeleteObject(font);
}

void DrawSwatch(const DRAWITEMSTRUCT* item, COLORREF color, bool enabled) {
  RECT box = item->rcItem;
  HBRUSH br = CreateSolidBrush(color); FillRect(item->hDC, &box, br); DeleteObject(br);
  FrameRect(item->hDC, &box,
            reinterpret_cast<HBRUSH>(GetStockObject(enabled ? BLACK_BRUSH : GRAY_BRUSH)));
  if (item->itemState & ODS_FOCUS) DrawFocusRect(item->hDC, &box);
}

void BuildControls(HWND hwnd) {
  // ---- candidate window ----
  Add(hwnd, L"STATIC", L"\u5019\u9009\u7a97\u5916\u89c2",
      SS_LEFT, 20, 14, 200, 22, -1, g_section_font);
  Label(hwnd, L"\u5b57\u4f53", 20, 50, 60, 22);
  Add(hwnd, L"STATIC", L"", SS_LEFT|SS_CENTERIMAGE|WS_BORDER,
      85, 46, 275, 26, kFontText, g_ui_font);
  Add(hwnd, L"BUTTON", L"\u9009\u62e9\u5b57\u4f53\u2026",
      BS_PUSHBUTTON|WS_TABSTOP, 370, 46, 110, 26, kFontPick, g_ui_font);
  Label(hwnd, L"\u5019\u9009\u6392\u5217", 20, 88, 60, 22);
  Add(hwnd, L"BUTTON", L"\u7ad6\u6392",
      BS_AUTORADIOBUTTON|WS_GROUP|WS_TABSTOP, 85, 86, 70, 24, kVertical, g_ui_font);
  Add(hwnd, L"BUTTON", L"\u6a2a\u6392",
      BS_AUTORADIOBUTTON, 160, 86, 70, 24, kHorizontal, g_ui_font);
  Label(hwnd, L"\u884c\u9ad8", 20, 126, 60, 22);
  Add(hwnd, L"EDIT", L"", ES_NUMBER|ES_LEFT|WS_BORDER|WS_TABSTOP,
      85, 124, 60, 26, kRowHeight, g_ui_font);
  Label(hwnd, L"\u5185\u8fb9\u8ddd", 165, 126, 60, 22);
  Add(hwnd, L"EDIT", L"", ES_NUMBER|ES_LEFT|WS_BORDER|WS_TABSTOP,
      230, 124, 60, 26, kPadding, g_ui_font);
  Label(hwnd, L"\u6700\u5c0f\u5bbd\u5ea6", 20, 164, 60, 22);
  Add(hwnd, L"EDIT", L"", ES_NUMBER|ES_LEFT|WS_BORDER|WS_TABSTOP,
      85, 162, 60, 26, kMinWidth, g_ui_font);
  Label(hwnd, L"\u6700\u5927\u5bbd\u5ea6", 165, 164, 60, 22);
  Add(hwnd, L"EDIT", L"", ES_NUMBER|ES_LEFT|WS_BORDER|WS_TABSTOP,
      230, 162, 60, 26, kMaxWidth, g_ui_font);

  // ---- color ----
  Add(hwnd, L"STATIC", L"\u914d\u8272",
      SS_LEFT, 20, 206, 200, 22, -1, g_section_font);
  Label(hwnd, L"\u9884\u8bbe", 20, 240, 60, 22);
  const wchar_t* presets[] = {L"\u8ddf\u968f\u7cfb\u7edf", L"\u6d45\u8272",
                               L"\u6df1\u8272", L"\u62a4\u773c", L"\u9ad8\u5bf9\u6bd4"};
  for (int i = 0; i < 5; ++i)
    Add(hwnd, L"BUTTON", presets[i], BS_PUSHBUTTON|WS_TABSTOP,
        85+i*82, 238, 78, 26, kPresetSystem+i, g_ui_font);
  struct CR { const wchar_t* label; int ck; int sw; int y; };
  const CR rows[] = {
      {L"\u7a97\u53e3\u80cc\u666f", kBackgroundSystem, kBackgroundColor, 278},
      {L"\u6b63\u6587\u989c\u8272", kTextSystem,       kTextColor,       312},
      {L"\u7f16\u7801\u989c\u8272", kDimSystem,        kDimColor,        346},
  };
  for (const CR& row : rows) {
    Label(hwnd, row.label, 20, row.y+3, 70, 22);
    Add(hwnd, L"BUTTON", L"\u8ddf\u968f\u7cfb\u7edf",
        BS_AUTOCHECKBOX|WS_TABSTOP, 95, row.y+2, 100, 24, row.ck, g_ui_font);
    Add(hwnd, L"BUTTON", L"", BS_OWNERDRAW|WS_TABSTOP,
        205, row.y, 70, 26, row.sw, g_ui_font);
  }
  Label(hwnd, L"\u9009\u4e2d\u5e95\u8272", 20, 383, 70, 22);
  Add(hwnd, L"BUTTON", L"", BS_OWNERDRAW|WS_TABSTOP,
      205, 380, 70, 26, kHighlightBg, g_ui_font);
  Label(hwnd, L"\u9009\u4e2d\u6587\u5b57", 20, 417, 70, 22);
  Add(hwnd, L"BUTTON", L"", BS_OWNERDRAW|WS_TABSTOP,
      205, 414, 70, 26, kHighlightFg, g_ui_font);

  // ---- tray ----
  Add(hwnd, L"STATIC", L"\u4efb\u52a1\u680f\u56fe\u6807",
      SS_LEFT, 20, 456, 200, 22, -1, g_section_font);
  Label(hwnd, L"\u4e2d\u6587", 20, 490, 40, 22);
  Add(hwnd, L"EDIT", L"", ES_LEFT|WS_BORDER|WS_TABSTOP,
      65, 488, 50, 26, kTrayChinese, g_ui_font);
  Label(hwnd, L"\u897f\u6587", 135, 490, 40, 22);
  Add(hwnd, L"EDIT", L"", ES_LEFT|WS_BORDER|WS_TABSTOP,
      180, 488, 50, 26, kTrayWestern, g_ui_font);
  Label(hwnd,
        L"\u4efb\u52a1\u680f\u53f3\u4e0b\u89d2\u90a3\u4e2a\u8f93\u5165"
        L"\u6307\u793a\u5668\u4e0a\u663e\u793a\u7684\u5b57",
        250, 490, 250, 22);

  // ---- learning mode ----
  Add(hwnd, L"STATIC",
      L"\u62c6\u5b57\u7a97\u53e3\uff08\u5b66\u4e60\u6a21\u5f0f\uff09",
      SS_LEFT, 20, 530, 300, 22, -1, g_section_font);
  Add(hwnd, L"BUTTON",
      L"\u5f00\u542f\u2014\u2014\u5019\u9009\u9ad8\u4eae\u65f6"
      L"\u5b9e\u65f6\u663e\u793a\u62c6\u5b57",
      BS_AUTOCHECKBOX|WS_TABSTOP,
      20, 558, 460, 26, kShowPartsWindow, g_ui_font);
  Label(hwnd, L"\u663e\u793a\u65b9\u5f0f", 20, 592, 70, 22);
  Add(hwnd, L"BUTTON", L"\u7ad6\u6392\uff08\u6bcf\u5b57\u4e00\u884c\uff0c\u4fe1\u606f\u5c55\u5f00\uff09",
      BS_AUTORADIOBUTTON|WS_GROUP|WS_TABSTOP,
      95, 590, 200, 24, kPartsVertical, g_ui_font);
  Add(hwnd, L"BUTTON", L"\u6a2a\u6392\uff08\u591a\u5b57\u5e76\u6392\uff09",
      BS_AUTORADIOBUTTON,
      300, 590, 160, 24, kPartsHorizontal, g_ui_font);

  // ---- preview ----
  Add(hwnd, L"STATIC", L"\u9884\u89c8",
      SS_LEFT, 20, 628, 200, 22, -1, g_section_font);
  Add(hwnd, L"STATIC", L"", SS_OWNERDRAW,
      20, 656, 480, 120, kPreview, g_ui_font);

  // ---- buttons ----
  Add(hwnd, L"BUTTON", L"\u6062\u590d\u9ed8\u8ba4",
      BS_PUSHBUTTON|WS_TABSTOP, 20, 796, 100, 30, kRestoreDefaults, g_ui_font);
  Add(hwnd, L"BUTTON", L"\u6253\u5f00\u8bbe\u7f6e\u6587\u4ef6",
      BS_PUSHBUTTON|WS_TABSTOP, 128, 796, 120, 30, kOpenFile, g_ui_font);
  Add(hwnd, L"BUTTON", L"\u5e94\u7528",
      BS_PUSHBUTTON|WS_TABSTOP, 290, 796, 66, 30, kApply, g_ui_font);
  Add(hwnd, L"BUTTON", L"\u786e\u5b9a",
      BS_DEFPUSHBUTTON|WS_TABSTOP, 364, 796, 66, 30, kConfirm, g_ui_font);
  Add(hwnd, L"BUTTON", L"\u53d6\u6d88",
      BS_PUSHBUTTON|WS_TABSTOP, 438, 796, 66, 30, kCancel, g_ui_font);
}

bool Save(HWND hwnd) {
  PullLookFromControls(hwnd);
  if (zuxia::SaveAppearance(g_look)) { g_applied = true; return true; }
  MessageBoxW(hwnd,
    L"\u8bbe\u7f6e\u6ca1\u80fd\u5199\u8fdb\u6587\u4ef6\u3002"
    L"\u53ef\u80fd\u662f\u6740\u6bd2\u8f6f\u4ef6\u62e6\u4e86\uff0c"
    L"\u6216\u8005\u8fd9\u4e2a\u6587\u4ef6\u88ab\u8bbe\u6210\u4e86\u53ea\u8bfb\u3002\n"
    L"\u70b9\u300c\u6253\u5f00\u8bbe\u7f6e\u6587\u4ef6\u300d\u770b\u770b\u80fd\u4e0d\u80fd\u624b\u5de5\u6539\u3002",
    kTitle, MB_OK|MB_ICONWARNING);
  return false;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE: BuildControls(hwnd); PushLookToControls(hwnd); return 0;
    case WM_SIZE: {
      if (!(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VSCROLL)) return 0;
      const int view = HIWORD(lp);
      SCROLLINFO si = {}; si.cbSize = sizeof(si);
      si.fMask = SIF_RANGE|SIF_PAGE|SIF_POS;
      si.nMin = 0; si.nMax = g_content_px > 0 ? g_content_px-1 : 0;
      si.nPage = static_cast<UINT>(view > 0 ? view : 1); si.nPos = g_scroll_pos;
      SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
      const int lim = g_content_px > view ? g_content_px - view : 0;
      if (g_scroll_pos > lim) {
        ScrollWindowEx(hwnd, 0, g_scroll_pos-lim, nullptr, nullptr, nullptr, nullptr,
                       SW_SCROLLCHILDREN|SW_INVALIDATE|SW_ERASE);
        g_scroll_pos = lim; SetScrollPos(hwnd, SB_VERT, g_scroll_pos, TRUE);
      }
      return 0;
    }
    case WM_VSCROLL: {
      if (!(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VSCROLL)) return 0;
      SCROLLINFO si = {}; si.cbSize = sizeof(si); si.fMask = SIF_ALL;
      if (!GetScrollInfo(hwnd, SB_VERT, &si)) return 0;
      const int page = static_cast<int>(si.nPage);
      const int lim  = si.nMax+1 > page ? si.nMax+1-page : 0;
      int want = g_scroll_pos;
      switch (LOWORD(wp)) {
        case SB_TOP: want=0; break; case SB_BOTTOM: want=lim; break;
        case SB_LINEUP: want-=S(28); break; case SB_LINEDOWN: want+=S(28); break;
        case SB_PAGEUP: want-=page; break; case SB_PAGEDOWN: want+=page; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: want=si.nTrackPos; break;
        default: return 0;
      }
      want = want<0?0:want>lim?lim:want;
      if (want == g_scroll_pos) return 0;
      ScrollWindowEx(hwnd, 0, g_scroll_pos-want, nullptr, nullptr, nullptr, nullptr,
                     SW_SCROLLCHILDREN|SW_INVALIDATE|SW_ERASE);
      g_scroll_pos = want; SetScrollPos(hwnd, SB_VERT, g_scroll_pos, TRUE);
      return 0;
    }
    case WM_MOUSEWHEEL: {
      if (!(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VSCROLL)) break;
      const int n = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
      for (int i=0;i<n;++i)  SendMessageW(hwnd, WM_VSCROLL, SB_LINEUP,   0);
      for (int i=0;i>n;--i)  SendMessageW(hwnd, WM_VSCROLL, SB_LINEDOWN, 0);
      return 0;
    }
    case WM_DRAWITEM: {
      const DRAWITEMSTRUCT* di = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
      switch (di->CtlID) {
        case kPreview:         DrawPreview(di); return TRUE;
        case kBackgroundColor:
          DrawSwatch(di, Resolve(g_look.system_background,g_look.background,COLOR_WINDOW),
                     !g_look.system_background); return TRUE;
        case kTextColor:
          DrawSwatch(di, Resolve(g_look.system_text,g_look.text,COLOR_WINDOWTEXT),
                     !g_look.system_text); return TRUE;
        case kDimColor:
          DrawSwatch(di, Resolve(g_look.system_dim,g_look.dim,COLOR_GRAYTEXT),
                     !g_look.system_dim); return TRUE;
        case kHighlightBg: DrawSwatch(di, g_look.highlight_bg, true); return TRUE;
        case kHighlightFg: DrawSwatch(di, g_look.highlight_fg, true); return TRUE;
        default: break;
      }
      break;
    }
    case WM_COMMAND: {
      const int id = LOWORD(wp), code = HIWORD(wp);
      switch (id) {
        case kFontPick: if (PickFont(hwnd)) PushLookToControls(hwnd); return 0;
        case kPresetSystem: case kPresetLight: case kPresetDark:
        case kPresetEye:    case kPresetContrast:
          ApplyPreset(id); PushLookToControls(hwnd); return 0;
        case kBackgroundColor:
          if (PickColor(hwnd,&g_look.background))
          { g_look.system_background=false; PushLookToControls(hwnd); } return 0;
        case kTextColor:
          if (PickColor(hwnd,&g_look.text))
          { g_look.system_text=false; PushLookToControls(hwnd); } return 0;
        case kDimColor:
          if (PickColor(hwnd,&g_look.dim))
          { g_look.system_dim=false; PushLookToControls(hwnd); } return 0;
        case kHighlightBg:
          if (PickColor(hwnd,&g_look.highlight_bg)) PushLookToControls(hwnd); return 0;
        case kHighlightFg:
          if (PickColor(hwnd,&g_look.highlight_fg)) PushLookToControls(hwnd); return 0;
        case kBackgroundSystem: case kTextSystem: case kDimSystem:
        case kVertical: case kHorizontal:
          PullLookFromControls(hwnd); PushLookToControls(hwnd); return 0;
        case kShowPartsWindow:
        case kPartsVertical: case kPartsHorizontal:
          PullLookFromControls(hwnd); return 0;
        case kRowHeight: case kPadding: case kMinWidth: case kMaxWidth:
        case kTrayChinese: case kTrayWestern:
          if (code == EN_CHANGE) {
            PullLookFromControls(hwnd);
            InvalidateRect(GetDlgItem(hwnd, kPreview), nullptr, TRUE);
          }
          return 0;
        case kRestoreDefaults:
          g_look = zuxia::Appearance(); PushLookToControls(hwnd); return 0;
        case kOpenFile: {
          Save(hwnd);
          ShellExecuteW(hwnd, L"open", L"notepad.exe",
                        zuxia::SettingsFilePath().c_str(), nullptr, SW_SHOWNORMAL);
          g_original = g_look; g_applied = false; return 0;
        }
        case kApply:   Save(hwnd); PushLookToControls(hwnd); return 0;
        case kConfirm: if (Save(hwnd)) DestroyWindow(hwnd); return 0;
        case IDCANCEL: case kCancel:
          if (g_applied) zuxia::SaveAppearance(g_original);
          DestroyWindow(hwnd); return 0;
        default: break;
      }
      break;
    }
    case WM_CLOSE:
      if (g_applied) zuxia::SaveAppearance(g_original);
      DestroyWindow(hwnd); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  HANDLE once = CreateMutexW(nullptr, TRUE, L"Local\\ZuxiaSettingsSingleton");
  if (once && GetLastError() == ERROR_ALREADY_EXISTS) {
    HWND ex = FindWindowW(kClassName, nullptr);
    if (ex) { ShowWindow(ex, SW_RESTORE); SetForegroundWindow(ex); }
    return 0;
  }
  SetProcessDPIAware();
  INITCOMMONCONTROLSEX cc = {sizeof(cc), ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&cc);
  HDC scr = GetDC(nullptr);
  if (scr) { g_dpi = GetDeviceCaps(scr, LOGPIXELSX); ReleaseDC(nullptr, scr); }
  if (g_dpi <= 0) g_dpi = 96;
  g_ui_font = CreateFontW(-MulDiv(10,g_dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
                          DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,
                          L"Microsoft YaHei UI");
  g_section_font = CreateFontW(-MulDiv(11,g_dpi,72),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,
                               DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,
                               L"Microsoft YaHei UI");
  g_look = zuxia::LoadAppearance(); g_original = g_look;
  WNDCLASSEXW wc = {};
  wc.cbSize=sizeof(wc); wc.lpfnWndProc=WindowProc; wc.hInstance=instance;
  wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
  wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);
  wc.lpszClassName=kClassName;
  wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1)); wc.hIconSm=wc.hIcon;
  if (!RegisterClassExW(&wc)) return 1;
  g_content_px = S(kContentHeight); g_scroll_pos = 0;
  RECT work={};
  if (!SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0)) {
    work.right=GetSystemMetrics(SM_CXSCREEN); work.bottom=GetSystemMetrics(SM_CYSCREEN);
  }
  const int ww=work.right-work.left, wh=work.bottom-work.top;
  DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU;
  int ch=g_content_px, cw=S(kContentWidth);
  RECT probe={0,0,cw,ch}; AdjustWindowRect(&probe,style,FALSE);
  const int chrome=(probe.bottom-probe.top)-ch;
  if (wh>0 && (probe.bottom-probe.top)>wh) {
    ch=wh-chrome; if(ch<S(280))ch=S(280);
    style|=WS_VSCROLL; cw+=GetSystemMetrics(SM_CXVSCROLL);
  }
  RECT want={0,0,cw,ch}; AdjustWindowRect(&want,style,FALSE);
  const int fw=want.right-want.left, fh=want.bottom-want.top;
  int x=work.left+(ww>fw?(ww-fw)/2:0), y=work.top+(wh>fh?(wh-fh)/2:0);
  g_main=CreateWindowExW(0,kClassName,kTitle,style,x,y,fw,fh,
                         nullptr,nullptr,instance,nullptr);
  if (!g_main) return 1;
  ShowWindow(g_main,show); UpdateWindow(g_main);
  MSG message={};
  while (GetMessageW(&message,nullptr,0,0)>0) {
    if (IsDialogMessageW(g_main,&message)) continue;
    TranslateMessage(&message); DispatchMessageW(&message);
  }
  if (g_ui_font)      DeleteObject(g_ui_font);
  if (g_section_font) DeleteObject(g_section_font);
  if (once) CloseHandle(once);
  return 0;
}
