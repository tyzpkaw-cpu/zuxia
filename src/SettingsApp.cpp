// ZuxiaSettings.exe —— 足下输入法的设置界面。
//
// 输入法本体读的还是那个纯文本文件 %LOCALAPPDATA%\Zuxia\设置.txt；这个程序
// 只是它的一张脸。两条好处：文本文件那条路一个字节都没变（想手改照样手改，
// 远程支持时一句话就能说清），而这个进程崩了也碰不到输入法 —— 它跑在自己
// 的进程里，不在 Word 和浏览器的地址空间里。
//
// 改完点「应用」，输入法最慢半秒自己看到，不用重启、不用重新部署。
//
// 界面全部用代码摆，不走对话框资源：布局要跟着 DPI 缩放，而 .rc 里的对话框
// 单位是按字体格算的，中文字体一换就散架。

#include <windows.h>

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <cstdlib>
#include <string>

#include "Settings.h"

namespace {

const wchar_t kClassName[] = L"ZuxiaSettingsWindow";
const wchar_t kTitle[] = L"应物音形足下输入法 · 设置";

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
  kPreview,
  kRestoreDefaults,
  kOpenFile,
  kApply,
  kConfirm,
  kCancel,
};

zuxia::Appearance g_look;      // 界面上此刻的值
zuxia::Appearance g_original;  // 打开程序时文件里的值，「取消」要退回这里
bool g_applied = false;        // 按过「应用」没有 —— 按过，取消才需要回写
int g_dpi = 96;
HFONT g_ui_font = nullptr;
HFONT g_section_font = nullptr;
HWND g_main = nullptr;

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

// ------------------------------------------------------------------ 界面 --

void PushLookToControls(HWND hwnd) {
  const std::wstring caption = g_look.font + L"    " +
                               std::to_wstring(g_look.font_size) + L" px";
  SetDlgItemTextW(hwnd, kFontText, caption.c_str());
  CheckDlgButton(hwnd, kVertical, g_look.horizontal ? BST_UNCHECKED : BST_CHECKED);
  CheckDlgButton(hwnd, kHorizontal, g_look.horizontal ? BST_CHECKED : BST_UNCHECKED);
  SetInt(hwnd, kRowHeight, g_look.row_height);
  SetInt(hwnd, kPadding, g_look.padding);
  SetInt(hwnd, kMinWidth, g_look.min_width);
  SetInt(hwnd, kMaxWidth, g_look.max_width);
  CheckDlgButton(hwnd, kBackgroundSystem,
                 g_look.system_background ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, kTextSystem,
                 g_look.system_text ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(hwnd, kDimSystem,
                 g_look.system_dim ? BST_CHECKED : BST_UNCHECKED);
  EnableWindow(GetDlgItem(hwnd, kBackgroundColor), !g_look.system_background);
  EnableWindow(GetDlgItem(hwnd, kTextColor), !g_look.system_text);
  EnableWindow(GetDlgItem(hwnd, kDimColor), !g_look.system_dim);
  SetDlgItemTextW(hwnd, kTrayChinese, g_look.tray_chinese.c_str());
  SetDlgItemTextW(hwnd, kTrayWestern, g_look.tray_western.c_str());
  // 自绘的那几个是子窗口，刷父窗口刷不到它们，得逐个点名。
  const int repaint[] = {kBackgroundColor, kTextColor,   kDimColor,
                         kHighlightBg,     kHighlightFg, kPreview};
  for (int id : repaint) {
    HWND child = GetDlgItem(hwnd, id);
    if (child) InvalidateRect(child, nullptr, TRUE);
  }
}

void PullLookFromControls(HWND hwnd) {
  g_look.horizontal = IsDlgButtonChecked(hwnd, kHorizontal) == BST_CHECKED;
  g_look.row_height = GetInt(hwnd, kRowHeight, g_look.row_height, 12, 200);
  g_look.padding = GetInt(hwnd, kPadding, g_look.padding, 0, 64);
  g_look.min_width = GetInt(hwnd, kMinWidth, g_look.min_width, 80, 2000);
  g_look.max_width = GetInt(hwnd, kMaxWidth, g_look.max_width, 80, 4000);
  if (g_look.max_width < g_look.min_width) g_look.max_width = g_look.min_width;
  g_look.system_background =
      IsDlgButtonChecked(hwnd, kBackgroundSystem) == BST_CHECKED;
  g_look.system_text = IsDlgButtonChecked(hwnd, kTextSystem) == BST_CHECKED;
  g_look.system_dim = IsDlgButtonChecked(hwnd, kDimSystem) == BST_CHECKED;
  // 图标位留空就等于没写，输入法那边会退回默认；这里也一样，别把空串存进去。
  const std::wstring cn = GetText(hwnd, kTrayChinese, 2);
  const std::wstring en = GetText(hwnd, kTrayWestern, 2);
  if (!cn.empty()) g_look.tray_chinese = cn;
  if (!en.empty()) g_look.tray_western = en;
}

void ApplyPreset(int id) {
  switch (id) {
    case kPresetSystem:
      g_look.system_background = true;
      g_look.system_text = true;
      g_look.system_dim = true;
      g_look.highlight_bg = RGB(35, 104, 190);
      g_look.highlight_fg = RGB(255, 255, 255);
      break;
    case kPresetLight:
      g_look.system_background = g_look.system_text = g_look.system_dim = false;
      g_look.background = RGB(255, 255, 255);
      g_look.text = RGB(32, 32, 32);
      g_look.dim = RGB(130, 130, 130);
      g_look.highlight_bg = RGB(35, 104, 190);
      g_look.highlight_fg = RGB(255, 255, 255);
      break;
    case kPresetDark:
      g_look.system_background = g_look.system_text = g_look.system_dim = false;
      g_look.background = RGB(32, 32, 32);
      g_look.text = RGB(235, 235, 235);
      g_look.dim = RGB(150, 150, 150);
      g_look.highlight_bg = RGB(0, 120, 212);
      g_look.highlight_fg = RGB(255, 255, 255);
      break;
    case kPresetEye:
      g_look.system_background = g_look.system_text = g_look.system_dim = false;
      g_look.background = RGB(199, 237, 204);
      g_look.text = RGB(38, 60, 42);
      g_look.dim = RGB(96, 125, 102);
      g_look.highlight_bg = RGB(58, 122, 74);
      g_look.highlight_fg = RGB(255, 255, 255);
      break;
    case kPresetContrast:
      g_look.system_background = g_look.system_text = g_look.system_dim = false;
      g_look.background = RGB(0, 0, 0);
      g_look.text = RGB(255, 255, 255);
      g_look.dim = RGB(255, 255, 0);
      g_look.highlight_bg = RGB(255, 255, 0);
      g_look.highlight_fg = RGB(0, 0, 0);
      break;
    default:
      break;
  }
}

bool PickColor(HWND hwnd, COLORREF* color) {
  static COLORREF custom[16] = {};
  CHOOSECOLORW choose = {};
  choose.lStructSize = sizeof(choose);
  choose.hwndOwner = hwnd;
  choose.rgbResult = *color;
  choose.lpCustColors = custom;
  choose.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
  if (!ChooseColorW(&choose)) return false;
  *color = choose.rgbResult;
  return true;
}

bool PickFont(HWND hwnd) {
  LOGFONTW lf = {};
  lf.lfHeight = -S(g_look.font_size);
  lf.lfCharSet = DEFAULT_CHARSET;
  lstrcpynW(lf.lfFaceName, g_look.font.c_str(), LF_FACESIZE);
  CHOOSEFONTW choose = {};
  choose.lStructSize = sizeof(choose);
  choose.hwndOwner = hwnd;
  choose.lpLogFont = &lf;
  choose.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_NOSCRIPTSEL |
                 CF_NOVERTFONTS;
  if (!ChooseFontW(&choose)) return false;
  g_look.font = lf.lfFaceName;
  const int height = lf.lfHeight < 0 ? -lf.lfHeight : lf.lfHeight;
  int size = MulDiv(height, 96, g_dpi);
  if (size < 8) size = 8;
  if (size > 72) size = 72;
  g_look.font_size = size;
  return true;
}

// 预览：照候选窗的画法摆一遍，颜色、字体、行高、横竖排都按当前设置走。
// 不求像素级一致 —— 求的是「改了这个数，屏幕上会变成什么样」看得见。
void DrawPreview(const DRAWITEMSTRUCT* item) {
  HDC dc = item->hDC;
  RECT box = item->rcItem;
  const COLORREF back =
      Resolve(g_look.system_background, g_look.background, COLOR_WINDOW);
  const COLORREF fore = Resolve(g_look.system_text, g_look.text, COLOR_WINDOWTEXT);
  const COLORREF dim = Resolve(g_look.system_dim, g_look.dim, COLOR_GRAYTEXT);

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

  // 编码行
  RECT line = {box.left + pad, y, box.right - pad, y + row};
  SetTextColor(dc, dim);
  DrawTextW(dc, L"qingzs", -1, &line, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  y += row;

  static const wchar_t* kItems[] = {L"1 清", L"2 情", L"3 请"};
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
      DrawTextW(dc, kItems[i], -1, &cell, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
      DrawTextW(dc, kItems[i], -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
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

// ------------------------------------------------------------- 窗口过程 --

void BuildControls(HWND hwnd) {
  Add(hwnd, L"STATIC", L"候选窗外观", SS_LEFT, 20, 14, 200, 22, -1,
      g_section_font);

  Label(hwnd, L"字体", 20, 50, 60, 22);
  Add(hwnd, L"STATIC", L"", SS_LEFT | SS_CENTERIMAGE | WS_BORDER, 85, 46, 275,
      26, kFontText, g_ui_font);
  Add(hwnd, L"BUTTON", L"选择字体…", BS_PUSHBUTTON | WS_TABSTOP, 370, 46, 110,
      26, kFontPick, g_ui_font);

  Label(hwnd, L"候选排列", 20, 88, 60, 22);
  Add(hwnd, L"BUTTON", L"竖排", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 85,
      86, 70, 24, kVertical, g_ui_font);
  Add(hwnd, L"BUTTON", L"横排", BS_AUTORADIOBUTTON, 160, 86, 70, 24,
      kHorizontal, g_ui_font);

  Label(hwnd, L"行高", 20, 126, 60, 22);
  Add(hwnd, L"EDIT", L"", ES_NUMBER | ES_LEFT | WS_BORDER | WS_TABSTOP, 85, 124,
      60, 26, kRowHeight, g_ui_font);
  Label(hwnd, L"内边距", 165, 126, 60, 22);
  Add(hwnd, L"EDIT", L"", ES_NUMBER | ES_LEFT | WS_BORDER | WS_TABSTOP, 230, 124,
      60, 26, kPadding, g_ui_font);

  Label(hwnd, L"最小宽度", 20, 164, 60, 22);
  Add(hwnd, L"EDIT", L"", ES_NUMBER | ES_LEFT | WS_BORDER | WS_TABSTOP, 85, 162,
      60, 26, kMinWidth, g_ui_font);
  Label(hwnd, L"最大宽度", 165, 164, 60, 22);
  Add(hwnd, L"EDIT", L"", ES_NUMBER | ES_LEFT | WS_BORDER | WS_TABSTOP, 230, 162,
      60, 26, kMaxWidth, g_ui_font);

  Add(hwnd, L"STATIC", L"配色", SS_LEFT, 20, 206, 200, 22, -1, g_section_font);
  Label(hwnd, L"预设", 20, 240, 60, 22);
  const wchar_t* presets[] = {L"跟随系统", L"浅色", L"深色", L"护眼", L"高对比"};
  for (int i = 0; i < 5; ++i) {
    Add(hwnd, L"BUTTON", presets[i], BS_PUSHBUTTON | WS_TABSTOP,
        85 + i * 82, 238, 78, 26, kPresetSystem + i, g_ui_font);
  }

  struct ColorRow {
    const wchar_t* label;
    int check_id;
    int swatch_id;
    int y;
  };
  const ColorRow rows[] = {
      {L"窗口背景", kBackgroundSystem, kBackgroundColor, 278},
      {L"正文颜色", kTextSystem, kTextColor, 312},
      {L"编码颜色", kDimSystem, kDimColor, 346},
  };
  for (const ColorRow& row : rows) {
    Label(hwnd, row.label, 20, row.y + 3, 70, 22);
    Add(hwnd, L"BUTTON", L"跟随系统", BS_AUTOCHECKBOX | WS_TABSTOP, 95, row.y + 2,
        100, 24, row.check_id, g_ui_font);
    Add(hwnd, L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, 205, row.y, 70, 26,
        row.swatch_id, g_ui_font);
  }
  Label(hwnd, L"选中底色", 20, 383, 70, 22);
  Add(hwnd, L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, 205, 380, 70, 26,
      kHighlightBg, g_ui_font);
  Label(hwnd, L"选中文字", 20, 417, 70, 22);
  Add(hwnd, L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, 205, 414, 70, 26,
      kHighlightFg, g_ui_font);

  Add(hwnd, L"STATIC", L"任务栏图标", SS_LEFT, 20, 456, 200, 22, -1,
      g_section_font);
  Label(hwnd, L"中文", 20, 490, 40, 22);
  Add(hwnd, L"EDIT", L"", ES_LEFT | WS_BORDER | WS_TABSTOP, 65, 488, 50, 26,
      kTrayChinese, g_ui_font);
  Label(hwnd, L"西文", 135, 490, 40, 22);
  Add(hwnd, L"EDIT", L"", ES_LEFT | WS_BORDER | WS_TABSTOP, 180, 488, 50, 26,
      kTrayWestern, g_ui_font);
  Label(hwnd, L"任务栏右下角那个输入指示器上显示的字", 250, 490, 250, 22);

  Add(hwnd, L"STATIC", L"预览", SS_LEFT, 20, 528, 200, 22, -1, g_section_font);
  Add(hwnd, L"STATIC", L"", SS_OWNERDRAW, 20, 556, 480, 120, kPreview,
      g_ui_font);

  Add(hwnd, L"BUTTON", L"恢复默认", BS_PUSHBUTTON | WS_TABSTOP, 20, 692, 100, 30,
      kRestoreDefaults, g_ui_font);
  Add(hwnd, L"BUTTON", L"打开设置文件", BS_PUSHBUTTON | WS_TABSTOP, 128, 692, 120,
      30, kOpenFile, g_ui_font);
  Add(hwnd, L"BUTTON", L"应用", BS_PUSHBUTTON | WS_TABSTOP, 290, 692, 66, 30,
      kApply, g_ui_font);
  Add(hwnd, L"BUTTON", L"确定", BS_DEFPUSHBUTTON | WS_TABSTOP, 364, 692, 66, 30,
      kConfirm, g_ui_font);
  Add(hwnd, L"BUTTON", L"取消", BS_PUSHBUTTON | WS_TABSTOP, 438, 692, 66, 30,
      kCancel, g_ui_font);
}

bool Save(HWND hwnd) {
  PullLookFromControls(hwnd);
  if (zuxia::SaveAppearance(g_look)) {
    g_applied = true;
    return true;
  }
  MessageBoxW(hwnd,
              L"设置没能写进文件。可能是杀毒软件拦了，或者这个文件被设成了只读。\n"
              L"点「打开设置文件」看看能不能手工改。",
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

    case WM_DRAWITEM: {
      const DRAWITEMSTRUCT* item =
          reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
      switch (item->CtlID) {
        case kPreview:
          DrawPreview(item);
          return TRUE;
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
          DrawSwatch(item, g_look.highlight_bg, true);
          return TRUE;
        case kHighlightFg:
          DrawSwatch(item, g_look.highlight_fg, true);
          return TRUE;
        default:
          break;
      }
      break;
    }

    case WM_COMMAND: {
      const int id = LOWORD(wparam);
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
          // 先落盘再打开，否则记事本里看到的是改之前的内容。
          Save(hwnd);
          const std::wstring path = zuxia::SettingsFilePath();
          ShellExecuteW(hwnd, L"open", L"notepad.exe", path.c_str(), nullptr,
                        SW_SHOWNORMAL);
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
          // 按过「应用」就已经写进文件了，取消得把原样写回去 —— 不然
          // 「取消」等于「保留」，那是骗人。
          if (g_applied) zuxia::SaveAppearance(g_original);
          DestroyWindow(hwnd);
          return 0;
        default:
          break;
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

    default:
      break;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  // 已经开着一个就把它拎到前面来，不要开第二个 —— 两个窗口各写各的文件，
  // 后保存的那个会把前一个的改动盖掉。
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

  g_ui_font = CreateFontW(-MulDiv(10, g_dpi, 72), 0, 0, 0, FW_NORMAL, FALSE,
                          FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
  g_section_font = CreateFontW(-MulDiv(11, g_dpi, 72), 0, 0, 0, FW_SEMIBOLD,
                               FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                               L"Microsoft YaHei UI");

  g_look = zuxia::LoadAppearance();
  g_original = g_look;

  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  wc.lpszClassName = kClassName;
  wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
  wc.hIconSm = wc.hIcon;
  if (!RegisterClassExW(&wc)) return 1;

  RECT want = {0, 0, S(520), S(736)};
  AdjustWindowRect(&want, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
  g_main = CreateWindowExW(
      0, kClassName, kTitle, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
      CW_USEDEFAULT, CW_USEDEFAULT, want.right - want.left,
      want.bottom - want.top, nullptr, nullptr, instance, nullptr);
  if (!g_main) return 1;
  ShowWindow(g_main, show);
  UpdateWindow(g_main);

  MSG message = {};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    if (IsDialogMessageW(g_main, &message)) continue;
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  if (g_ui_font) DeleteObject(g_ui_font);
  if (g_section_font) DeleteObject(g_section_font);
  if (once) CloseHandle(once);
  return 0;
}
