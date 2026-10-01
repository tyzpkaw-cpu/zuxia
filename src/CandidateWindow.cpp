#include "Globals.h"
#include "CandidateWindow.h"

#include <algorithm>

ATOM CCandidateWindow::atom_ = 0;
INIT_ONCE CCandidateWindow::init_once_ = INIT_ONCE_STATIC_INIT;

namespace {
constexpr wchar_t kWindowClass[] = L"ZuxiaIMECandidateWindow";
}

CCandidateWindow::CCandidateWindow() = default;

CCandidateWindow::~CCandidateWindow() { Destroy(); }

BOOL CCandidateWindow::InitWindowClass() {
  return InitOnceExecuteOnce(&init_once_, RegisterClassOnce, nullptr, nullptr);
}

BOOL CALLBACK CCandidateWindow::RegisterClassOnce(
    PINIT_ONCE /*init_once*/, PVOID /*parameter*/, PVOID* /*context*/) {
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = g_hInst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kWindowClass;
  atom_ = RegisterClassExW(&wc);
  return atom_ != 0;
}

void CCandidateWindow::UninitWindowClass() {
  // The class is process-local and is released automatically when the DLL
  // unloads. It intentionally remains registered after INIT_ONCE completes.
}

bool CCandidateWindow::Create() {
  if (hwnd_) return true;
  if (!InitWindowClass()) return false;
  look_ = zuxia::CurrentAppearance();
  hwnd_ = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kWindowClass,
      L"应物候选", WS_POPUP | WS_BORDER, 0, 0, width_, height_, nullptr,
      nullptr, g_hInst, this);
  if (!hwnd_) return false;
  ApplySettings();
  return true;
}

void CCandidateWindow::ApplySettings() {
  look_ = zuxia::CurrentAppearance();
  if (font_ && font_in_use_ == look_.font &&
      font_size_in_use_ == look_.font_size) {
    return;
  }
  HFONT created = CreateFontW(
      -Scale(look_.font_size), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, look_.font.c_str());
  if (!created) return;  // 字体名打错了也不能让候选窗变成一片空白
  if (font_) DeleteObject(font_);
  font_ = created;
  font_in_use_ = look_.font;
  font_size_in_use_ = look_.font_size;
}

void CCandidateWindow::Destroy() {
  if (hwnd_) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
  if (font_) {
    DeleteObject(font_);
    font_ = nullptr;
  }
}

void CCandidateWindow::Move(int x, int y) {
  if (!hwnd_) return;
  RECT work = {};
  POINT anchor = {x, y};
  MONITORINFO monitor_info = {};
  monitor_info.cbSize = sizeof(monitor_info);
  const HMONITOR monitor =
      MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
  if (monitor && GetMonitorInfoW(monitor, &monitor_info)) {
    work = monitor_info.rcWork;
  } else {
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  }
  x = std::max(static_cast<int>(work.left), x);
  y = std::max(static_cast<int>(work.top), y);
  if (x + width_ > work.right) {
    x = std::max(static_cast<int>(work.left),
                 static_cast<int>(work.right) - width_);
  }
  if (y + height_ > work.bottom) {
    y = std::max(static_cast<int>(work.top), y - height_ - Scale(24));
  }
  SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width_, height_,
               SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void CCandidateWindow::Show() {
  if (hwnd_) ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
}

void CCandidateWindow::Hide() {
  if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
  session_width_ = 0;  // 下一次组字从头量宽度
}

bool CCandidateWindow::Visible() const {
  return hwnd_ && IsWindowVisible(hwnd_);
}

std::wstring CCandidateWindow::RowText(
    const zuxia::Candidate& candidate) const {
  std::wstring text = candidate.label + L"  " + candidate.text;
  if (!candidate.comment.empty()) text += L"  " + candidate.comment;
  return text;
}

void CCandidateWindow::Update(
    const std::wstring& preedit,
    const std::vector<zuxia::Candidate>& candidates, int highlighted) {
  ApplySettings();
  preedit_ = preedit;
  candidates_ = candidates;
  highlighted_ = std::clamp(highlighted, 0,
                            std::max(0, static_cast<int>(candidates_.size()) - 1));
  RecalculateSize();
  if (hwnd_) {
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, width_, height_,
                 SWP_NOMOVE | SWP_NOACTIVATE);
    InvalidateRect(hwnd_, nullptr, TRUE);
  }
}

void CCandidateWindow::RecalculateSize() {
  const int count = static_cast<int>(candidates_.size());
  const int header = preedit_.empty() ? 0 : 1;
  item_widths_.clear();

  if (look_.horizontal) {
    height_ = Scale(look_.padding * 2 + look_.row_height * (1 + header));
  } else {
    height_ = Scale(look_.padding * 2 +
                    look_.row_height * (std::max(1, count) + header));
  }
  width_ = Scale(look_.min_width);
  if (!hwnd_) return;

  HDC dc = GetDC(hwnd_);
  HFONT old =
      font_ ? reinterpret_cast<HFONT>(SelectObject(dc, font_)) : nullptr;
  auto measure = [&](const std::wstring& text) -> int {
    SIZE size = {};
    if (text.empty()) return 0;
    if (!GetTextExtentPoint32W(dc, text.c_str(),
                               static_cast<int>(text.size()), &size)) {
      return 0;
    }
    return static_cast<int>(size.cx);
  };

  if (look_.horizontal) {
    int total = Scale(look_.padding) * 2;
    for (const auto& candidate : candidates_) {
      const int item =
          measure(RowText(candidate)) + Scale(look_.padding) * 2;
      item_widths_.push_back(item);
      total += item;
    }
    // 编码行单独占一行，它也可能比候选那一行还长。
    total = std::max(total, measure(preedit_) + Scale(look_.padding) * 4);
    width_ = std::max(width_, total);
  } else {
    auto widen = [&](const std::wstring& text) {
      const int measured = measure(text);
      if (measured > 0) {
        width_ = std::max(width_,
                          measured + Scale(look_.padding * 4 + 44));
      }
    };
    widen(preedit_);
    for (const auto& candidate : candidates_) widen(RowText(candidate));
  }

  width_ = std::min(width_, Scale(look_.max_width));
  // 只长不缩，直到这次组字结束。
  width_ = std::max(width_, session_width_);
  session_width_ = width_;
  if (old) SelectObject(dc, old);
  ReleaseDC(hwnd_, dc);
}

COLORREF CCandidateWindow::BackgroundColor() const {
  return look_.system_background ? GetSysColor(COLOR_WINDOW) : look_.background;
}

COLORREF CCandidateWindow::TextColor() const {
  return look_.system_text ? GetSysColor(COLOR_WINDOWTEXT) : look_.text;
}

COLORREF CCandidateWindow::DimColor() const {
  return look_.system_dim ? GetSysColor(COLOR_GRAYTEXT) : look_.dim;
}

void CCandidateWindow::Paint() {
  PAINTSTRUCT ps = {};
  HDC dc = BeginPaint(hwnd_, &ps);
  RECT client = {};
  GetClientRect(hwnd_, &client);
  HBRUSH background = CreateSolidBrush(BackgroundColor());
  if (background) {
    FillRect(dc, &client, background);
    DeleteObject(background);
  }
  SetBkMode(dc, TRANSPARENT);
  HFONT old =
      font_ ? reinterpret_cast<HFONT>(SelectObject(dc, font_)) : nullptr;

  int y = Scale(look_.padding);
  if (!preedit_.empty()) {
    RECT row = {Scale(look_.padding), y, client.right - Scale(look_.padding),
                y + Scale(look_.row_height)};
    SetTextColor(dc, DimColor());
    DrawTextW(dc, preedit_.c_str(), -1, &row,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    y += Scale(look_.row_height);
  }

  if (look_.horizontal) {
    PaintHorizontal(dc, client, y);
  } else {
    PaintVertical(dc, client, y);
  }

  if (old) SelectObject(dc, old);
  EndPaint(hwnd_, &ps);
}

void CCandidateWindow::PaintVertical(HDC dc, const RECT& client, int y) {
  for (int i = 0; i < static_cast<int>(candidates_.size()); ++i) {
    RECT row = {Scale(look_.padding / 2), y,
                client.right - Scale(look_.padding / 2),
                y + Scale(look_.row_height)};
    if (i == highlighted_) {
      HBRUSH brush = CreateSolidBrush(look_.highlight_bg);
      if (brush) {
        FillRect(dc, &row, brush);
        DeleteObject(brush);
      }
      SetTextColor(dc, look_.highlight_fg);
    } else {
      SetTextColor(dc, TextColor());
    }
    RECT text_rect = row;
    text_rect.left += Scale(look_.padding);
    text_rect.right -= Scale(look_.padding);
    const std::wstring text = RowText(candidates_[i]);
    DrawTextW(dc, text.c_str(), -1, &text_rect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    y += Scale(look_.row_height);
  }
}

void CCandidateWindow::PaintHorizontal(HDC dc, const RECT& client, int y) {
  int x = Scale(look_.padding);
  for (int i = 0; i < static_cast<int>(candidates_.size()); ++i) {
    const int item = (i < static_cast<int>(item_widths_.size()))
                         ? item_widths_[i]
                         : Scale(look_.padding) * 2;
    if (x >= client.right) break;  // 放不下的就不画，别画到窗外去
    RECT row = {x, y, std::min(x + item, static_cast<int>(client.right)),
                y + Scale(look_.row_height)};
    if (i == highlighted_) {
      HBRUSH brush = CreateSolidBrush(look_.highlight_bg);
      if (brush) {
        FillRect(dc, &row, brush);
        DeleteObject(brush);
      }
      SetTextColor(dc, look_.highlight_fg);
    } else {
      SetTextColor(dc, TextColor());
    }
    const std::wstring text = RowText(candidates_[i]);
    DrawTextW(dc, text.c_str(), -1, &row,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    x += item;
  }
}

int CCandidateWindow::Scale(int value) const {
  UINT dpi = 96;
  if (hwnd_) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    const auto fn = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (fn) dpi = fn(hwnd_);
  }
  return MulDiv(value, dpi, 96);
}

LRESULT CALLBACK CCandidateWindow::WindowProc(HWND hwnd, UINT message,
                                               WPARAM w_param,
                                               LPARAM l_param) {
  CCandidateWindow* self = reinterpret_cast<CCandidateWindow*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
    self = static_cast<CCandidateWindow*>(create->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(self));
  }
  if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
  if (message == WM_PAINT && self) {
    self->Paint();
    return 0;
  }
  if (message == WM_ERASEBKGND) return 1;
  return DefWindowProcW(hwnd, message, w_param, l_param);
}
