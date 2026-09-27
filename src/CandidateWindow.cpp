#include "Globals.h"
#include "CandidateWindow.h"

#include <algorithm>

ATOM CCandidateWindow::atom_ = 0;
INIT_ONCE CCandidateWindow::init_once_ = INIT_ONCE_STATIC_INIT;

namespace {
constexpr wchar_t kWindowClass[] = L"ZuxiaIMECandidateWindow";
constexpr int kPadding = 8;
constexpr int kRowHeight = 30;
constexpr int kMinWidth = 220;
constexpr int kMaxWidth = 720;
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
  hwnd_ = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kWindowClass,
      L"应物候选", WS_POPUP | WS_BORDER, 0, 0, width_, height_, nullptr,
      nullptr, g_hInst, this);
  if (!hwnd_) return false;
  font_ = CreateFontW(-Scale(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                      L"Microsoft YaHei UI");
  return true;
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
}

bool CCandidateWindow::Visible() const {
  return hwnd_ && IsWindowVisible(hwnd_);
}

void CCandidateWindow::Update(
    const std::wstring& preedit,
    const std::vector<zuxia::Candidate>& candidates, int highlighted) {
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
  const int rows = std::max(1, static_cast<int>(candidates_.size()));
  height_ = Scale(kPadding * 2 + kRowHeight * rows +
                  (preedit_.empty() ? 0 : kRowHeight));
  width_ = Scale(kMinWidth);
  if (!hwnd_) return;
  HDC dc = GetDC(hwnd_);
  HFONT old =
      font_ ? reinterpret_cast<HFONT>(SelectObject(dc, font_)) : nullptr;
  SIZE size = {};
  auto measure = [&](const std::wstring& text) {
    if (!text.empty() && GetTextExtentPoint32W(dc, text.c_str(),
                                               static_cast<int>(text.size()),
                                               &size)) {
      width_ = std::max(
          width_, static_cast<int>(size.cx) + Scale(kPadding * 4 + 44));
    }
  };
  measure(preedit_);
  for (const auto& candidate : candidates_) {
    measure(candidate.label + L"  " + candidate.text + L"  " +
            candidate.comment);
  }
  width_ = std::min(width_, Scale(kMaxWidth));
  if (old) SelectObject(dc, old);
  ReleaseDC(hwnd_, dc);
}

void CCandidateWindow::Paint() {
  PAINTSTRUCT ps = {};
  HDC dc = BeginPaint(hwnd_, &ps);
  RECT client = {};
  GetClientRect(hwnd_, &client);
  FillRect(dc, &client, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
  SetBkMode(dc, TRANSPARENT);
  HFONT old =
      font_ ? reinterpret_cast<HFONT>(SelectObject(dc, font_)) : nullptr;

  int y = Scale(kPadding);
  if (!preedit_.empty()) {
    RECT row = {Scale(kPadding), y, client.right - Scale(kPadding),
                y + Scale(kRowHeight)};
    SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
    DrawTextW(dc, preedit_.c_str(), -1, &row,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    y += Scale(kRowHeight);
  }

  for (int i = 0; i < static_cast<int>(candidates_.size()); ++i) {
    RECT row = {Scale(kPadding / 2), y, client.right - Scale(kPadding / 2),
                y + Scale(kRowHeight)};
    if (i == highlighted_) {
      HBRUSH brush = CreateSolidBrush(RGB(35, 104, 190));
      FillRect(dc, &row, brush);
      DeleteObject(brush);
      SetTextColor(dc, RGB(255, 255, 255));
    } else {
      SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    }
    RECT text_rect = row;
    text_rect.left += Scale(kPadding);
    text_rect.right -= Scale(kPadding);
    const auto& item = candidates_[i];
    const std::wstring text = item.label + L"  " + item.text +
                              (item.comment.empty() ? L"" : L"  " + item.comment);
    DrawTextW(dc, text.c_str(), -1, &text_rect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    y += Scale(kRowHeight);
  }

  if (old) SelectObject(dc, old);
  EndPaint(hwnd_, &ps);
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
