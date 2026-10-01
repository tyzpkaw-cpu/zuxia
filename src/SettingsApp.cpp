// ZuxiaSettings.exe -- Zuxia IME settings window.
//
// The IME core still reads the same plain-text file %LOCALAPPDATA%\Zuxia\设置.txt;
// this program is just a GUI front-end for it. Two benefits: the text-file path
// is unchanged (users can still hand-edit; remote support is one sentence), and
// a crash here cannot affect the IME -- it runs in its own process.
//
// Clicking Apply writes the file; the IME picks up changes within 500 ms.
// All controls are positioned absolutely so layout scales with DPI.

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
  kFontText = 1001,
  kFontPick,
  kVertical,
  kHorizontal,
  kRowHeight,
  kPadding,
  kMinWidth,
  kMaxWidth,
  kPresetSystem,
  kPresetLight,
  kPresetDark,
  kPresetEye,
  kPresetContrast,
  kBackgroundSystem,
  kBackgroundColor,
  kTextSystem,
  kTextColor,
  kDimSystem,
  kDimColor,
  kHighlightBg,
  kHighlightFg,
  kTrayChinese,
  kTrayWestern,
  kShowPartsWindow,   // new: learning mode checkbox
  kPreview,
  kRestoreDefaults,
  kOpenFile,
  kApply,
  kConfirm,
  kCancel,
};

zuxia::Appearance g_look;
zuxia::Appearance g_original;
bool g_applied = false;
int g_dpi = 96;
HFONT g_ui_font = nullptr;
HFONT g_section_font = nullptr;
HWND g_main = nullptr;

// Content height increased by ~60 px to accommodate the new section.
constexpr int kContentWidth  = 520;
constexpr int kContentHeight = 800;

int g_content_px = 0;
int g_scroll_pos = 0;

int S(int value) { return MulDiv(value, g_dpi, 96); }

HWND Add(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style,
         int x, int y, int w, int h, int id, HFONT font) {
  HWND child = CreateWindowExW(0, cls, text, style | WS_CHILD | WS_VISIBLE,
                               S(x), S(y), S(w), S(h), parent,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                               reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(
                                   parent, GWLP_HINSTANCE)),
                               nullptr);
  if (child) SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  return child;
}

HWND Label(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
  return Add(parent, L"STATIC", text, SS_LEFT, x, y, w, h, -1, g_ui_font);
}

void SetInt(HWND parent, int id, int value) {
  SetDlgItemTextW(parent, id, std::to_wstring(value).c_str());
}

int GetInt(HWND parent, int id, int fallback, int low, int high) {
  wchar_t buffer[32] = {};
  GetDlgItemTextW(parent, id, buffer, ARRAYSIZE(buffer));
  wchar_t* end = nullptr;
  const long value = wcstol(buffer, &end, 10);
  if (end == buffer) return fallback;
  if (value < low) return low;
  if (value > high) return high;
  return static_cast<int>(value);
}

std::wstring GetText(HWND parent, int id, size_t limit) {
  wchar_t buffer[128] = {};
  GetDlgItemTextW(parent, id, buffer, ARRAYSIZE(buffer));
  std::wstring text(buffer);
  if (text.size() > limit) text.resize(limit);
  return text;
}

COLORREF Resolve(bool follow_system, COLORREF color, int sys_index) {
  return follow_system ? GetSysColor(sys_index) : color;
}

// ---------------------------------------------------------------- controls --

void PushLookToControls(HWND hwnd) {
  const std::wstring caption = g_look.font + L"    " +
                               std::to_wstring(g_look.font_size) + L" px";
  SetDlgItemTextW(hwnd, kFontText, caption.c_str());
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
  const int repaint[] = {kBackgroundColor, kTextColor,   kDimColor,
                         kHighlightBg,     kHighlightFg, kPreview};
  for (int id : repaint) {
    HWND child = GetDlgItem(hwnd, id);
    if (child) InvalidateRect(child, nullptr, TRUE);
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
}

void ApplyPreset(int id) {
  switch (id) {
    case kPresetSystem:
      g_look.system_background = true;
      g_look.system_text       = true;
      g_look.system_dim        = true;
      g_look.highlight_bg = RGB(35, 104, 190);
      g_look.highlight_fg = RGB(255, 255, 255);
      break;
    case kPresetLight:
      g_look.system_background = g_look.system_text = g_look.system_dim = false;
      g_look.background = RGB(255, 255, 255);
      g_look.text       = RGB(32, 32, 32);
      g_look.dim        = RGB(130, 130, 130);
      g_look.highlight_bg = RGB(35, 104, 190);
      g_look.highlight_fg = RGB(255, 255, 255);
      break;
    case kPresetDark:
      g_look.system_background = g_look.system_text = g_look.system_dim = false;
      g_look.background = RGB(32, 32, 32);
      g_look.text       = RGB(235, 235, 235);
      g_look.dim        = RGB(150, 150, 150);
      g_look.highlight_bg = RGB(0, 120, 212);
      g_look.highlight_fg = RGB(255, 255, 255);
      break;
    case kPresetEye:
      g_look.system_background = g_look.system_text = g_look.system_dim = false;
      g_look.background = RGB(199, 237, 204);
      g_look.text       = RGB(38, 60, 42);
      g_look.dim        = RGB(96, 125, 102);
      g_look.highlight_bg = RGB(58, 122, 74);
      g_look.highlight_fg = RGB(255, 255, 255);
      break;
    case kPresetContrast:
      g_look.system_background = g_look.system_text = g_look.system_dim = false;
      g_look.background = RGB(0, 0, 0);
      g_look.text       = RGB(255, 255, 255);
      g_look.dim        = RGB(255, 255, 0);
      g_look.highlight_bg = RGB(255, 255, 0);
      g_look.highlight_fg = RGB(0, 0, 0);
      break;
    default: break;
  }
}

bool PickColor(HWND hwnd, COLORREF* color) {
  static COLORREF custom[16] = {};
  CHOOSECOLORW choose = {};
  choose.lStructSize = sizeof(choose);
  choose.hwndOwner   = hwnd;
  choose.rgbResult   = *color;
  choose.lpCustColors = custom;
  choose.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
  if (!ChooseColorW(&choose)) return false;
  *color = choose.rgbResult;
  return true;
}

bool PickFont(HWND hwnd) {
  LOGFONTW lf = {};
  lf.lfHeight   = -S(g_look.font_size);
  lf.lfCharSet  = DEFAULT_CHARSET;
  lstrcpynW(lf.lfFaceName, g_look.font.c_str(), LF_FACESIZE);
  CHOOSEFONTW choose = {};
  choose.lStructSize = sizeof(choose);
  choose.hwndOwner   = hwnd;
  choose.lpLogFont   = &lf;
  choose.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_NOSCRIPTSEL |
                 CF_NOVERTFONTS;
  if (!ChooseFontW(&choose)) return false;
  g_look.font = lf.lfFaceName;
  const int height = lf.lfHeight < 0 ? -lf.lfHeight : lf.lfHeight;
  int size = MulDiv(height, 96, g_dpi);
  if (size < 8)  size = 8;
  if (size > 72) size = 72;
  g_look.font_size = size;
  return true;
}

void DrawPreview(const DRAWITEMSTRUCT* item) {
  HDC dc   = item->hDC;
  RECT box = item->rcItem;
  const COLORREF back =
      Resolve(g_look.system_background, g_look.background, COLOR_WINDOW);
  const COLORREF fore =
      Resolve(g_look.system_text, g_look.text, COLOR_WINDOWTEXT);
  const COLORREF dim =
      Resolve(g_look.system_dim, g_look.dim, COLOR_GRAYTEXT);

  HBRUSH brush = CreateSolidBrush(back);
  FillRect(dc, &box, brush);
  DeleteObject(brush);
  FrameRect(dc, &box, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));

  HFONT font = CreateFontW(-S(g_look.font_size), 0, 0, 0, FW_NORMAL, FALSE,
                           FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, g_look.font.c_str());
  HGDIOBJ old_font = SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT);

  const int pad = S(g_look.padding);
  const int row = S(g_look.row_height);
  int y = box.top + pad;

  RECT line = {box.left + pad, y, box.right - pad, y + row};
  SetTextColor(dc, dim);
  DrawTextW(dc, L"qingzs", -1, &line, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  y += row;

  static const wchar_t* kItems[] = {L"1 \u6e05", L"2 \u60c5", L"3 \u8bf7"};
  if (g_look.horizontal) {
    int x = box.left + pad;
    for (int i = 0; i < 3; ++i) {
      SIZE size = {};
      GetTextExtentPoint32W(dc, kItems[i], lstrlenW(kItems[i]), &size);
      RECT cell = {x, y, x + size.cx + S(16), y + row};
      if (cell.right > box.right - pad) break;
      if (i == 0) {
        HBRUSH hit = CreateSolidBrush(g_look.highlight_bg);
        FillRect(dc, &cell, hit);
        DeleteObject(hit);
        SetTextColor(dc, g_look.highlight_fg);
      } else {
        SetTextColor(dc, fore);
      }
      DrawTextW(dc, kItems[i], -1, &cell,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      x = cell.right;
    }
  } else {
    for (int i = 0; i < 3; ++i) {
      RECT cell = {box.left + pad, y, box.right - pad, y + row};
      if (cell.bottom > box.bottom - pad) break;
      if (i == 0) {
        HBRUSH hit = CreateSolidBrush(g_look.highlight_bg);
        FillRect(dc, &cell, hit);
        DeleteObject(hit);
        SetTextColor(dc, g_look.highlight_fg);
      } else {
        SetTextColor(dc, fore);
      }
      RECT text = cell;
      text.left += S(8);
      DrawTextW(dc, kItems[i], -1, &text,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
      y = cell.bottom;
    }
  }

  SelectObject(dc, old_font);
  DeleteObject(font);
}

void DrawSwatch(const DRAWITEMSTRUCT* item, COLORREF color, bool enabled) {
  RECT box = item->rcItem;
  HBRUSH brush = CreateSolidBrush(color);
  FillRect(item->hDC, &box, brush);
  DeleteObject(brush);
  FrameRect(item->hDC, &box,
            reinterpret_cast<HBRUSH>(GetStockObject(
                enabled ? BLACK_BRUSH : GRAY_BRUSH)));
  if (item->itemState & ODS_FOCUS) DrawFocusRect(item->hDC, &box);
}

// --------------------------------------------------------------- layout --

void BuildControls(HWND hwnd) {
  // ---- candidate window appearance ----
  Add(hwnd, L"STATIC", L"\u5019\u9009\u7a97\u5916\u89c2", SS_LEFT,
      20, 14, 200, 22, -1, g_section_font);

  Label(hwnd, L"\u5b57\u4f53", 20, 50, 60, 22);
  Add(hwnd, L"STATIC", L"", SS_LEFT | SS_CENTERIMAGE | WS_BORDER,
      85, 46, 275, 26, kFontText, g_ui_font);
  Add(hwnd, L"BUTTON", L"\u9009\u62e9\u5b57\u4f53\u2026",
      BS_PUSHBUTTON | WS_TABSTOP, 370, 46, 110, 26, kFontPick, g_ui_font);

  Label(hwnd, L"\u5019\u9009\u6392\u5217", 20, 88, 60, 22);
  Add(hwnd, L"BUTTON", L"\u7ad6\u6392",
      BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP,
      85, 86, 70, 24, kVertical, g_ui_font);
  Add(hwnd, L"BUTTON", L"\u6a2a\u6392", BS_AUTORADIOBUTTON,
      160, 86, 70, 24, kHorizontal, g_ui_font);

  Label(hwnd, L"\u884c\u9ad8", 20, 126, 60, 22);
  Add(hwnd, L"EDIT", L"", ES_NUMBER | ES_LEFT | WS_BORDER | WS_TABSTOP,
      85, 124, 60, 26, kRowHeight, g_ui_font);
  Label(hwnd, L"\u5185\u8fb9\u8ddd", 165, 126, 60, 22);
  Add(hwnd, L"EDIT", L"", ES_NUMBER | ES_LEFT | WS_BORDER | WS_TABSTOP,
      230, 124, 60, 26, kPadding, g_ui_font);

  Label(hwnd, L"\u6700\u5c0f\u5bbd\u5ea6", 20, 164, 60, 22);
  Add(hwnd, L"EDIT", L"", ES_NUMBER | ES_LEFT | WS_BORDER | WS_TABSTOP,
      85, 162, 60, 26, kMinWidth, g_ui_font);
  Label(hwnd, L"\u6700\u5927\u5bbd\u5ea6", 165, 164, 60, 22);
  Add(hwnd, L"EDIT", L"", ES_NUMBER | ES_LEFT | WS_BORDER | WS_TABSTOP,
      230, 162, 60, 26, kMaxWidth, g_ui_font);

  // ---- color ----
  Add(hwnd, L"STATIC", L"\u914d\u8272", SS_LEFT,
      20, 206, 200, 22, -1, g_section_font);
  Label(hwnd, L"\u9884\u8bbe", 20, 240, 60, 22);
  const wchar_t* presets[] = {
      L"\u8ddf\u968f\u7cfb\u7edf", L"\u6d45\u8272",
      L"\u6df1\u8272",             L"\u62a4\u773c",
      L"\u9ad8\u5bf9\u6bd4"};
  for (int i = 0; i < 5; ++i) {
    Add(hwnd, L"BUTTON", presets[i], BS_PUSHBUTTON | WS_TABSTOP,
        85 + i * 82, 238, 78, 26, kPresetSystem + i, g_ui_font);
  }

  struct ColorRow { const wchar_t* label; int check_id; int swatch_id; int y; };
  const ColorRow rows[] = {
      {L"\u7a97\u53e3\u80cc\u666f", kBackgroundSystem, kBackgroundColor, 278},
      {L"\u6b63\u6587\u989c\u8272", kTextSystem,       kTextColor,       312},
      {L"\u7f16\u7801\u989c\u8272", kDimSystem,        kDimColor,        346},
  };
  for (const ColorRow& row : rows) {
    Label(hwnd, row.label, 20, row.y + 3, 70, 22);
    Add(hwnd, L"BUTTON", L"\u8ddf\u968f\u7cfb\u7edf",
        BS_AUTOCHECKBOX | WS_TABSTOP,
        95, row.y + 2, 100, 24, row.check_id, g_ui_font);
    Add(hwnd, L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP,
        205, row.y, 70, 26, row.swatch_id, g_ui_font);
  }
  Label(hwnd, L"\u9009\u4e2d\u5e95\u8272", 20, 383, 70, 22);
  Add(hwnd, L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP,
      205, 380, 70, 26, kHighlightBg, g_ui_font);
  Label(hwnd, L"\u9009\u4e2d\u6587\u5b57", 20, 417, 70, 22);
  Add(hwnd, L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP,
      205, 414, 70, 26, kHighlightFg, g_ui_font);

  // ---- tray icon ----
  Add(hwnd, L"STATIC", L"\u4efb\u52a1\u680f\u56fe\u6807", SS_LEFT,
      20, 456, 200, 22, -1, g_section_font);
  Label(hwnd, L"\u4e2d\u6587", 20, 490, 40, 22);
  Add(hwnd, L"EDIT", L"", ES_LEFT | WS_BORDER | WS_TABSTOP,
      65, 488, 50, 26, kTrayChinese, g_ui_font);
  Label(hwnd, L"\u897f\u6587", 135, 490, 40, 22);
  Add(hwnd, L"EDIT", L"", ES_LEFT | WS_BORDER | WS_TABSTOP,
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
      L"\u5f00\u542f\u2014\u2014\u5019\u9009\u9ad8\u4eae\u65f6\u5b9e\u65f6"
      L"\u663e\u793a\u62c6\u5b57\uff08\u591a\u5b57\u8bcd\u6bcf\u5b57\u4e00\u683c\uff09",
      BS_AUTOCHECKBOX | WS_TABSTOP,
      20, 558, 460, 26, kShowPartsWindow, g_ui_font);

  // ---- preview ----
  Add(hwnd, L"STATIC", L"\u9884\u89c8", SS_LEFT,
      20, 598, 200, 22, -1, g_section_font);
  Add(hwnd, L"STATIC", L"", SS_OWNERDRAW,
      20, 626, 480, 120, kPreview, g_ui_font);

  // ---- buttons ----
  Add(hwnd, L"BUTTON", L"\u6062\u590d\u9ed8\u8ba4",
      BS_PUSHBUTTON | WS_TABSTOP, 20, 762, 100, 30, kRestoreDefaults, g_ui_font);
  Add(hwnd, L"BUTTON", L"\u6253\u5f00\u8bbe\u7f6e\u6587\u4ef6",
      BS_PUSHBUTTON | WS_TABSTOP, 128, 762, 120, 30, kOpenFile, g_ui_font);
  Add(hwnd, L"BUTTON", L"\u5e94\u7528",
      BS_PUSHBUTTON | WS_TABSTOP, 290, 762, 66, 30, kApply, g_ui_font);
  Add(hwnd, L"BUTTON", L"\u786e\u5b9a",
      BS_DEFPUSHBUTTON | WS_TABSTOP, 364, 762, 66, 30, kConfirm, g_ui_font);
  Add(hwnd, L"BUTTON", L"\u53d6\u6d88",
      BS_PUSHBUTTON | WS_TABSTOP, 438, 762, 66, 30, kCancel, g_ui_font);
}

bool Save(HWND hwnd) {
  PullLookFromControls(hwnd);
  if (zuxia::SaveAppearance(g_look)) {
    g_applied = true;
    return true;
  }
  MessageBoxW(hwnd,
              L"\u8bbe\u7f6e\u6ca1\u80fd\u5199\u8fdb\u6587\u4ef6\u3002"
              L"\u53ef\u80fd\u662f\u6740\u6bd2\u8f6f\u4ef6\u62e6\u4e86"
              L"\uff0c\u6216\u8005\u8fd9\u4e2a\u6587\u4ef6\u88ab\u8bbe\u6210"
              L"\u4e86\u53ea\u8bfb\u3002\n"
              L"\u70b9\u300c\u6253\u5f00\u8bbe\u7f6e\u6587\u4ef6\u300d"
              L"\u770b\u770b\u80fd\u4e0d\u80fd\u624b\u5de5\u6539\u3002",
              kTitle, MB_OK | MB_ICONWARNING);
  return false;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam,
                            LPARAM lparam) {
  switch (message) {
    case WM_CREATE:
      BuildControls(hwnd);
      PushLookToControls(hwnd);
      return 0;

    case WM_SIZE: {
      if (!(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VSCROLL)) return 0;
      const int view = HIWORD(lparam);
      SCROLLINFO info = {};
      info.cbSize = sizeof(info);
      info.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
      info.nMin   = 0;
      info.nMax   = g_content_px > 0 ? g_content_px - 1 : 0;
      info.nPage  = static_cast<UINT>(view > 0 ? view : 1);
      info.nPos   = g_scroll_pos;
      SetScrollInfo(hwnd, SB_VERT, &info, TRUE);
      const int limit = g_content_px > view ? g_content_px - view : 0;
      if (g_scroll_pos > limit) {
        ScrollWindowEx(hwnd, 0, g_scroll_pos - limit, nullptr, nullptr,
                       nullptr, nullptr,
                       SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
        g_scroll_pos = limit;
        SetScrollPos(hwnd, SB_VERT, g_scroll_pos, TRUE);
      }
      return 0;
    }

    case WM_VSCROLL: {
      if (!(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VSCROLL)) return 0;
      SCROLLINFO info = {};
      info.cbSize = sizeof(info);
      info.fMask  = SIF_ALL;
      if (!GetScrollInfo(hwnd, SB_VERT, &info)) return 0;
      const int page  = static_cast<int>(info.nPage);
      const int limit = info.nMax + 1 > page ? info.nMax + 1 - page : 0;
      int want = g_scroll_pos;
      switch (LOWORD(wparam)) {
        case SB_TOP:      want = 0;     break;
        case SB_BOTTOM:   want = limit; break;
        case SB_LINEUP:   want -= S(28); break;
        case SB_LINEDOWN: want += S(28); break;
        case SB_PAGEUP:   want -= page;  break;
        case SB_PAGEDOWN: want += page;  break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: want = info.nTrackPos; break;
        default: return 0;
      }
      if (want < 0)     want = 0;
      if (want > limit) want = limit;
      if (want == g_scroll_pos) return 0;
      ScrollWindowEx(hwnd, 0, g_scroll_pos - want, nullptr, nullptr,
                     nullptr, nullptr,
                     SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
      g_scroll_pos = want;
      SetScrollPos(hwnd, SB_VERT, g_scroll_pos, TRUE);
      return 0;
    }

    case WM_MOUSEWHEEL: {
      if (!(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VSCROLL)) break;
      const int notches = GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
      for (int i = 0; i < notches;  ++i)
        SendMessageW(hwnd, WM_VSCROLL, SB_LINEUP,   0);
      for (int i = 0; i > notches;  --i)
        SendMessageW(hwnd, WM_VSCROLL, SB_LINEDOWN,  0);
      return 0;
    }

    case WM_DRAWITEM: {
      const DRAWITEMSTRUCT* item =
          reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
      switch (item->CtlID) {
        case kPreview:
          DrawPreview(item); return TRUE;
        case kBackgroundColor:
          DrawSwatch(item,
                     Resolve(g_look.system_background, g_look.background,
                             COLOR_WINDOW),
                     !g_look.system_background);
          return TRUE;
        case kTextColor:
          DrawSwatch(item,
                     Resolve(g_look.system_text, g_look.text, COLOR_WINDOWTEXT),
                     !g_look.system_text);
          return TRUE;
        case kDimColor:
          DrawSwatch(item,
                     Resolve(g_look.system_dim, g_look.dim, COLOR_GRAYTEXT),
                     !g_look.system_dim);
          return TRUE;
        case kHighlightBg:
          DrawSwatch(item, g_look.highlight_bg, true); return TRUE;
        case kHighlightFg:
          DrawSwatch(item, g_look.highlight_fg, true); return TRUE;
        default: break;
      }
      break;
    }

    case WM_COMMAND: {
      const int id   = LOWORD(wparam);
      const int code = HIWORD(wparam);
      switch (id) {
        case kFontPick:
          if (PickFont(hwnd)) PushLookToControls(hwnd);
          return 0;
        case kPresetSystem:
        case kPresetLight:
        case kPresetDark:
        case kPresetEye:
        case kPresetContrast:
          ApplyPreset(id);
          PushLookToControls(hwnd);
          return 0;
        case kBackgroundColor:
          if (PickColor(hwnd, &g_look.background)) {
            g_look.system_background = false;
            PushLookToControls(hwnd);
          }
          return 0;
        case kTextColor:
          if (PickColor(hwnd, &g_look.text)) {
            g_look.system_text = false;
            PushLookToControls(hwnd);
          }
          return 0;
        case kDimColor:
          if (PickColor(hwnd, &g_look.dim)) {
            g_look.system_dim = false;
            PushLookToControls(hwnd);
          }
          return 0;
        case kHighlightBg:
          if (PickColor(hwnd, &g_look.highlight_bg)) PushLookToControls(hwnd);
          return 0;
        case kHighlightFg:
          if (PickColor(hwnd, &g_look.highlight_fg)) PushLookToControls(hwnd);
          return 0;
        case kBackgroundSystem:
        case kTextSystem:
        case kDimSystem:
        case kVertical:
        case kHorizontal:
          PullLookFromControls(hwnd);
          PushLookToControls(hwnd);
          return 0;
        case kShowPartsWindow:
          PullLookFromControls(hwnd);
          return 0;
        case kRowHeight:
        case kPadding:
        case kMinWidth:
        case kMaxWidth:
        case kTrayChinese:
        case kTrayWestern:
          if (code == EN_CHANGE) {
            PullLookFromControls(hwnd);
            InvalidateRect(GetDlgItem(hwnd, kPreview), nullptr, TRUE);
          }
          return 0;
        case kRestoreDefaults:
          g_look = zuxia::Appearance();
          PushLookToControls(hwnd);
          return 0;
        case kOpenFile: {
          Save(hwnd);
          const std::wstring path = zuxia::SettingsFilePath();
          ShellExecuteW(hwnd, L"open", L"notepad.exe", path.c_str(),
                        nullptr, SW_SHOWNORMAL);
          g_original = g_look;
          g_applied  = false;
          return 0;
        }
        case kApply:
          Save(hwnd);
          PushLookToControls(hwnd);
          return 0;
        case kConfirm:
          if (Save(hwnd)) DestroyWindow(hwnd);
          return 0;
        case IDCANCEL:
        case kCancel:
          if (g_applied) zuxia::SaveAppearance(g_original);
          DestroyWindow(hwnd);
          return 0;
        default: break;
      }
      break;
    }

    case WM_CLOSE:
      if (g_applied) zuxia::SaveAppearance(g_original);
      DestroyWindow(hwnd);
      return 0;

    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;

    default: break;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);

  HANDLE once = CreateMutexW(nullptr, TRUE, L"Local\\ZuxiaSettingsSingleton");
  if (once && GetLastError() == ERROR_ALREADY_EXISTS) {
    HWND existing = FindWindowW(kClassName, nullptr);
    if (existing) {
      ShowWindow(existing, SW_RESTORE);
      SetForegroundWindow(existing);
    }
    return 0;
  }

  SetProcessDPIAware();
  INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&controls);

  HDC screen = GetDC(nullptr);
  if (screen) {
    g_dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(nullptr, screen);
  }
  if (g_dpi <= 0) g_dpi = 96;

  g_ui_font = CreateFontW(
      -MulDiv(10, g_dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
      L"Microsoft YaHei UI");
  g_section_font = CreateFontW(
      -MulDiv(11, g_dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
      L"Microsoft YaHei UI");

  g_look     = zuxia::LoadAppearance();
  g_original = g_look;

  WNDCLASSEXW wc = {};
  wc.cbSize        = sizeof(wc);
  wc.lpfnWndProc   = WindowProc;
  wc.hInstance     = instance;
  wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  wc.lpszClassName = kClassName;
  wc.hIcon         = LoadIconW(instance, MAKEINTRESOURCEW(1));
  wc.hIconSm       = wc.hIcon;
  if (!RegisterClassExW(&wc)) return 1;

  g_content_px = S(kContentHeight);
  g_scroll_pos = 0;

  RECT work = {0, 0, 0, 0};
  if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
    work.left = 0; work.top = 0;
    work.right  = GetSystemMetrics(SM_CXSCREEN);
    work.bottom = GetSystemMetrics(SM_CYSCREEN);
  }
  const int work_w = work.right  - work.left;
  const int work_h = work.bottom - work.top;

  DWORD style    = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
  int client_h   = g_content_px;
  int client_w   = S(kContentWidth);

  RECT probe = {0, 0, client_w, client_h};
  AdjustWindowRect(&probe, style, FALSE);
  const int chrome_h = (probe.bottom - probe.top) - client_h;
  if (work_h > 0 && (probe.bottom - probe.top) > work_h) {
    client_h = work_h - chrome_h;
    const int floor_h = S(280);
    if (client_h < floor_h) client_h = floor_h;
    style  |= WS_VSCROLL;
    client_w += GetSystemMetrics(SM_CXVSCROLL);
  }

  RECT want = {0, 0, client_w, client_h};
  AdjustWindowRect(&want, style, FALSE);
  const int window_w = want.right  - want.left;
  const int window_h = want.bottom - want.top;
  int x = work.left + (work_w > window_w ? (work_w - window_w) / 2 : 0);
  int y = work.top  + (work_h > window_h ? (work_h - window_h) / 2 : 0);
  g_main = CreateWindowExW(0, kClassName, kTitle, style,
                           x, y, window_w, window_h,
                           nullptr, nullptr, instance, nullptr);
  if (!g_main) return 1;
  ShowWindow(g_main, show);
  UpdateWindow(g_main);

  MSG message = {};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    if (IsDialogMessageW(g_main, &message)) continue;
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  if (g_ui_font)     DeleteObject(g_ui_font);
  if (g_section_font) DeleteObject(g_section_font);
  if (once) CloseHandle(once);
  return 0;
}
