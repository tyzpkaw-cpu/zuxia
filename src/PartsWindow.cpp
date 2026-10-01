// PartsWindow.cpp —— 拆字窗（学习模式）
//
// 候选高亮时实时显示这个词怎么拆：每个字一个米字格，后面是结构和结构码，
// 再往后每个部件一行：部件、它的各个名称（单字名称带拼音）和对应的键。
// 上屏之后窗口留着刚上屏的那个词，可以拖到一边、放大了慢慢看。
//
// 0.4.1 及以前它是带系统标题栏和可调边框的普通窗口，真机上有三个毛病：
//   1. 一拖就没了。系统的拖动循环会走一遍激活，文档失去焦点、组字结束，
//      而组字结束会连带隐藏这个窗口。
//   2. 调不了大小。「不抢焦点」和系统的边框拖动互相冲突。
//   3. 词组的最后一行被截掉。尺寸是按内容算的客户区大小，却被当成整个
//      窗口（含标题栏、边框）的大小用了。
// 现在它是没有系统边框的弹出窗：标题条、关闭钮、拖动和缩放都自己做。拖动
// 用 SetCapture，不进系统的模态循环，从头到尾不激活；窗口尺寸永远由内容
// 实测出来，缩放改的是整体比例（字号跟着变），不会裁掉任何一行。
//
// 什么时候隐藏：点 ×、右键、关掉学习模式、切到西文、切到别的程序（定时器
// 看前台窗口属于哪个进程）、文本服务停用。点 × 之后，这一次组字里不再弹出，
// 下一次组字照常出来（TextService 调 ResetDismissed）。

#include "Globals.h"
#include "PartsWindow.h"
#include "Settings.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <unordered_map>

namespace zuxia {

namespace {

constexpr wchar_t kClassName[] = L"ZuxiaIMEPartsWindow";
constexpr wchar_t kTsvName[] = L"zuxia.parts.tsv";
constexpr wchar_t kRegistryPath[] = L"Software\\Zuxia\\PartsWindow";
constexpr UINT_PTR kForegroundTimer = 1;
constexpr UINT kForegroundTimerMs = 400;
constexpr size_t kMaxChars = 8;
constexpr double kMinZoom = 0.6;
constexpr double kMaxZoom = 3.0;
constexpr double kWheelStep = 1.1;

constexpr int kEdgeLeft = 1;
constexpr int kEdgeTop = 2;
constexpr int kEdgeRight = 4;
constexpr int kEdgeBottom = 8;

// ---- 拆字表：进程内只读一次，之后只读不写，各线程共用 -------------------

std::unordered_map<char32_t, CharParts>& Table() {
  static std::unordered_map<char32_t, CharParts> table;
  return table;
}
INIT_ONCE g_table_once = INIT_ONCE_STATIC_INIT;

char32_t CodePointAt(std::wstring_view s, size_t i, size_t* length) {
  const wchar_t c = s[i];
  if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size()) {
    const wchar_t d = s[i + 1];
    if (d >= 0xDC00 && d <= 0xDFFF) {
      *length = 2;
      return 0x10000 + ((static_cast<char32_t>(c) - 0xD800) << 10) +
             (static_cast<char32_t>(d) - 0xDC00);
    }
  }
  *length = 1;
  return static_cast<char32_t>(c);
}

size_t CountCodePoints(std::wstring_view s) {
  size_t count = 0;
  for (size_t i = 0; i < s.size();) {
    size_t length = 1;
    CodePointAt(s, i, &length);
    i += length;
    ++count;
  }
  return count;
}

std::vector<std::wstring> Split(std::wstring_view s, wchar_t sep) {
  std::vector<std::wstring> out;
  size_t start = 0;
  while (true) {
    const size_t at = s.find(sep, start);
    if (at == std::wstring_view::npos) {
      out.emplace_back(s.substr(start));
      return out;
    }
    out.emplace_back(s.substr(start, at - start));
    start = at + 1;
  }
}

// <安装目录>\x64\ZuxiaTSF.dll -> <安装目录>\data\<name>
std::wstring DataFile(const wchar_t* name) {
  wchar_t module[MAX_PATH] = {};
  const DWORD length = GetModuleFileNameW(g_hInst, module, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return std::wstring();
  std::wstring path(module, length);
  for (int i = 0; i < 2; ++i) {
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return std::wstring();
    path.erase(slash);
  }
  return path + L"\\data\\" + name;
}

bool ReadWholeFile(const std::wstring& path, std::string* out) {
  if (path.empty()) return false;
  const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size = {};
  bool ok = GetFileSizeEx(file, &size) && size.QuadPart > 0 &&
            size.QuadPart < (64LL << 20);
  if (ok) {
    out->resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    ok = ReadFile(file, out->data(), static_cast<DWORD>(out->size()), &read,
                  nullptr) &&
         read == out->size();
  }
  CloseHandle(file);
  return ok;
}

// 一行：字 TAB 结构码 TAB 部件|名称,名称|键|拼音|键:拼音,键:拼音 TAB …
// 最后一段与名称一一对应；老格式没有这一段，就把「键」整串挂在名称后面。
void ParseLine(std::wstring_view line,
               std::unordered_map<char32_t, CharParts>* table) {
  if (line.empty() || line[0] == L'#') return;
  const std::vector<std::wstring> cols = Split(line, L'\t');
  if (cols.size() < 2 || cols[0].empty()) return;
  CharParts entry;
  entry.glyph = cols[0];
  entry.structure = cols[1].empty() ? 0 : cols[1][0];
  for (size_t i = 2; i < cols.size(); ++i) {
    const std::vector<std::wstring> fields = Split(cols[i], L'|');
    if (fields.empty() || fields[0].empty()) continue;
    PartEntry part;
    part.glyph = fields[0];
    std::vector<std::wstring> names;
    if (fields.size() > 1 && !fields[1].empty()) names = Split(fields[1], L',');
    std::vector<std::wstring> keyed;
    if (fields.size() > 4 && !fields[4].empty()) keyed = Split(fields[4], L',');
    const bool aligned = keyed.size() == names.size();
    for (size_t k = 0; k < names.size(); ++k) {
      PartName one;
      one.name = names[k];
      if (aligned) {
        const std::wstring& item = keyed[k];
        const size_t colon = item.find(L':');
        if (colon != std::wstring::npos) {
          if (colon > 0) one.letter = item[0];
          one.pinyin = item.substr(colon + 1);
        }
      }
      part.names.push_back(std::move(one));
    }
    if (!aligned && fields.size() > 2) part.letters = fields[2];
    entry.parts.push_back(std::move(part));
  }
  size_t length = 1;
  const char32_t key = CodePointAt(entry.glyph, 0, &length);
  table->emplace(key, std::move(entry));
}

BOOL CALLBACK LoadTableOnce(PINIT_ONCE, PVOID, PVOID*) {
  std::string bytes;
  if (!ReadWholeFile(DataFile(kTsvName), &bytes)) return TRUE;  // 窗口不出来而已
  const int wide_length = MultiByteToWideChar(
      CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
  if (wide_length <= 0) return TRUE;
  std::wstring text(static_cast<size_t>(wide_length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()),
                      text.data(), wide_length);
  auto& table = Table();
  table.reserve(9000);
  std::wstring_view all(text);
  size_t start = 0;
  while (start < all.size()) {
    size_t end = all.find(L'\n', start);
    if (end == std::wstring_view::npos) end = all.size();
    std::wstring_view line = all.substr(start, end - start);
    if (!line.empty() && line.back() == L'\r') line.remove_suffix(1);
    if (!line.empty() && line.front() == 0xFEFF) line.remove_prefix(1);
    ParseLine(line, &table);
    start = end + 1;
  }
  return TRUE;
}

const std::unordered_map<char32_t, CharParts>& LoadedTable() {
  InitOnceExecuteOnce(&g_table_once, LoadTableOnce, nullptr, nullptr);
  return Table();
}

// ---- 显示用的文字 ---------------------------------------------------------

wchar_t Upper(wchar_t c) {
  return (c >= L'a' && c <= L'z') ? static_cast<wchar_t>(c - L'a' + L'A') : c;
}

std::wstring StructureLine(wchar_t code) {
  const wchar_t* name = nullptr;
  switch (code) {
    case L'z': name = L"左右结构"; break;
    case L's': name = L"上下结构"; break;
    case L'b': name = L"包围结构"; break;
    case L'p': name = L"品字形及其他"; break;
    case L'd': name = L"独体字"; break;
    default: return L"结构未标";
  }
  std::wstring out = name;
  out += L' ';
  out += Upper(code);
  return out;
}

// 扌  手 shǒu S · 提手 T
// 名称就是部件本身时不重复写名称；单字名称和「整字」带拼音。
std::wstring PartLine(const PartEntry& part) {
  std::wstring body;
  for (const PartName& n : part.names) {
    std::wstring one;
    const bool itself = n.name == part.glyph;
    if (!itself) one += n.name;
    const bool spell = !n.pinyin.empty() &&
                       (itself || CountCodePoints(n.name) == 1 ||
                        n.name == L"整字");
    if (spell) {
      if (!one.empty()) one += L' ';
      one += n.pinyin;
    }
    if (n.letter) {
      if (!one.empty()) one += L' ';
      one += Upper(n.letter);
    }
    if (one.empty()) continue;
    if (!body.empty()) body += L" · ";
    body += one;
  }
  if (!part.letters.empty()) {
    if (!body.empty()) body += L"  ";
    body += L"→ ";
    for (wchar_t c : part.letters) body += Upper(c);
  }
  return part.glyph + L"  " + body;
}

int TextWidth(HDC dc, const std::wstring& text) {
  SIZE size = {};
  if (text.empty() ||
      !GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()),
                             &size)) {
    return 0;
  }
  return static_cast<int>(size.cx);
}

int LineHeight(HDC dc) {
  TEXTMETRICW metrics = {};
  if (!GetTextMetricsW(dc, &metrics)) return 16;
  return static_cast<int>(metrics.tmHeight);
}

RECT WorkAreaAt(POINT point) {
  RECT work = {};
  MONITORINFO info = {};
  info.cbSize = sizeof(info);
  const HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
  if (monitor && GetMonitorInfoW(monitor, &info)) return info.rcWork;
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  return work;
}

DWORD ForegroundPid() {
  const HWND foreground = GetForegroundWindow();
  if (!foreground) return 0;
  DWORD pid = 0;
  GetWindowThreadProcessId(foreground, &pid);
  return pid;
}

void Fill(HDC dc, const RECT& rect, COLORREF color) {
  const HBRUSH brush = CreateSolidBrush(color);
  if (!brush) return;
  FillRect(dc, &rect, brush);
  DeleteObject(brush);
}

void Segment(HDC dc, int x1, int y1, int x2, int y2) {
  MoveToEx(dc, x1, y1, nullptr);
  LineTo(dc, x2, y2);
}

// 米字格只是背景参考线：外框、十字实线、两条对角虚线。
void DrawMiziGrid(HDC dc, const RECT& r) {
  const int left = static_cast<int>(r.left);
  const int top = static_cast<int>(r.top);
  const int right = static_cast<int>(r.right) - 1;
  const int bottom = static_cast<int>(r.bottom) - 1;
  const int cx = (left + right) / 2;
  const int cy = (top + bottom) / 2;
  const HPEN frame = CreatePen(PS_SOLID, 1, RGB(196, 120, 120));
  const HPEN cross = CreatePen(PS_DOT, 1, RGB(220, 170, 170));
  const HGDIOBJ old = SelectObject(dc, frame ? frame : GetStockObject(BLACK_PEN));
  Segment(dc, left, top, right, top);
  Segment(dc, right, top, right, bottom);
  Segment(dc, right, bottom, left, bottom);
  Segment(dc, left, bottom, left, top);
  if (cross) SelectObject(dc, cross);
  Segment(dc, left, cy, right, cy);
  Segment(dc, cx, top, cx, bottom);
  Segment(dc, left, top, right, bottom);
  Segment(dc, right, top, left, bottom);
  SelectObject(dc, old);
  if (cross) DeleteObject(cross);
  if (frame) DeleteObject(frame);
}

void DrawCross(HDC dc, const RECT& r, COLORREF color) {
  const int h = static_cast<int>(r.bottom - r.top);
  const int cx = static_cast<int>(r.left + r.right) / 2;
  const int cy = static_cast<int>(r.top + r.bottom) / 2;
  const int arm = std::max(3, h * 3 / 14);
  const HPEN pen = CreatePen(PS_SOLID, std::max(1, h / 16), color);
  const HGDIOBJ old = SelectObject(dc, pen ? pen : GetStockObject(BLACK_PEN));
  Segment(dc, cx - arm, cy - arm, cx + arm + 1, cy + arm + 1);
  Segment(dc, cx + arm, cy - arm, cx - arm - 1, cy + arm + 1);
  SelectObject(dc, old);
  if (pen) DeleteObject(pen);
}

LONG ReadDword(HKEY key, const wchar_t* name, bool* found) {
  DWORD value = 0;
  DWORD type = 0;
  DWORD size = sizeof(value);
  *found = RegQueryValueExW(key, name, nullptr, &type,
                            reinterpret_cast<BYTE*>(&value), &size) ==
               ERROR_SUCCESS &&
           type == REG_DWORD && size == sizeof(value);
  return static_cast<LONG>(value);
}

void WriteDword(HKEY key, const wchar_t* name, LONG value) {
  const DWORD raw = static_cast<DWORD>(value);
  RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&raw),
                 sizeof(raw));
}

}  // namespace

// ---- 生命周期 --------------------------------------------------------------

ATOM CPartsWindow::atom_ = 0;
INIT_ONCE CPartsWindow::init_once_ = INIT_ONCE_STATIC_INIT;

CPartsWindow::CPartsWindow() = default;
CPartsWindow::~CPartsWindow() { Destroy(); }

BOOL CPartsWindow::InitWindowClass() {
  return InitOnceExecuteOnce(&init_once_, RegisterClassOnce, nullptr, nullptr);
}

void CPartsWindow::UninitWindowClass() {}

BOOL CALLBACK CPartsWindow::RegisterClassOnce(PINIT_ONCE, PVOID, PVOID*) {
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.style = CS_DBLCLKS;  // 双击标题条复位 100%
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = g_hInst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;  // 全部自己画（双缓冲）
  wc.lpszClassName = kClassName;
  atom_ = RegisterClassExW(&wc);
  return atom_ != 0;
}

bool CPartsWindow::Create() {
  if (hwnd_) return true;
  if (!InitWindowClass()) return false;
  // 置顶、不进任务栏、不激活。点它、拖它、缩放它都不会把焦点从文档抢走。
  hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                          kClassName, L"拆字", WS_POPUP, 0, 0, 320, 200,
                          nullptr, nullptr, g_hInst, this);
  return hwnd_ != nullptr;
}

void CPartsWindow::Destroy() {
  if (hwnd_) {
    KillTimer(hwnd_, kForegroundTimer);
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
  DeleteFonts();
}

void CPartsWindow::DeleteFonts() {
  if (big_font_) DeleteObject(big_font_);
  if (label_font_) DeleteObject(label_font_);
  if (title_font_) DeleteObject(title_font_);
  big_font_ = label_font_ = title_font_ = nullptr;
  big_px_ = label_px_ = title_px_ = 0;
}

// ---- 显示与隐藏 ------------------------------------------------------------

void CPartsWindow::ShowWord(const std::wstring& text) {
  if (!hwnd_ || dismissed_ || text.empty()) return;
  // 日期（dt）、码串之类带字母数字的不拆，窗口保持原样。
  for (wchar_t c : text) {
    if ((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'z') ||
        (c >= L'A' && c <= L'Z')) {
      return;
    }
  }
  const auto& table = LoadedTable();
  std::vector<const CharParts*> found;
  bool more = false;
  for (size_t i = 0; i < text.size();) {
    size_t length = 1;
    const char32_t key = CodePointAt(text, i, &length);
    i += length;
    const auto it = table.find(key);
    if (it == table.end()) continue;
    if (found.size() == kMaxChars) {
      more = true;
      break;
    }
    found.push_back(&it->second);
  }
  if (found.empty()) return;  // 标点之类：留着上一个词

  const bool vertical = CurrentAppearance().parts_vertical;
  const bool visible = Visible();
  if (visible && found == chars_ && more == truncated_ &&
      vertical == vertical_) {
    return;
  }
  chars_ = std::move(found);
  truncated_ = more;
  vertical_ = vertical;
  if (!visible) {
    // 每次从隐藏到显示都重读位置和缩放：别的程序里的拆字窗可能刚被挪过。
    has_anchor_ = false;
    LoadPlacement();
    if (!has_anchor_) DefaultAnchor();
    shown_pid_ = ForegroundPid();
  }
  Relayout();
  if (!visible) {
    SetTimer(hwnd_, kForegroundTimer, kForegroundTimerMs, nullptr);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  }
}

void CPartsWindow::Hide() {
  if (!hwnd_) return;
  if (drag_ != Drag::kNone) EndDrag(false);
  KillTimer(hwnd_, kForegroundTimer);
  close_hot_ = false;
  if (IsWindowVisible(hwnd_)) ShowWindow(hwnd_, SW_HIDE);
}

void CPartsWindow::Dismiss() {
  dismissed_ = true;
  Hide();
}

bool CPartsWindow::Visible() const { return hwnd_ && IsWindowVisible(hwnd_); }

void CPartsWindow::CheckForeground() {
  if (!Visible()) return;
  if (!CurrentAppearance().show_parts_window) {
    Hide();
    return;
  }
  if (drag_ != Drag::kNone) return;
  // 切到别的程序就收起来。比的是「显示时前台窗口所在的进程」，不是本进程：
  // UWP 程序的前台窗口属于 ApplicationFrameHost，跟文本服务不在一个进程。
  const DWORD pid = ForegroundPid();
  if (!pid) return;  // 切换途中前台可能短暂为空
  if (!shown_pid_) {
    shown_pid_ = pid;
    return;
  }
  if (pid != shown_pid_) Hide();
}

// ---- 尺寸与布局 ------------------------------------------------------------

UINT CPartsWindow::Dpi() const {
  using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
  static const auto get_dpi = reinterpret_cast<GetDpiForWindowFn>(
      reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                             "GetDpiForWindow")));
  UINT dpi = 0;
  if (get_dpi && hwnd_) dpi = get_dpi(hwnd_);
  if (!dpi) {
    const HDC screen = GetDC(nullptr);
    if (screen) {
      dpi = static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSY));
      ReleaseDC(nullptr, screen);
    }
  }
  return dpi ? dpi : 96;
}

void CPartsWindow::EnsureFonts(int big_px, int label_px, int title_px) {
  auto make = [](int px, const wchar_t* face) {
    return CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
  };
  if (!big_font_ || big_px_ != big_px) {
    if (big_font_) DeleteObject(big_font_);
    big_font_ = make(big_px, L"SimSun");  // 宋体：部件的笔画看得最清楚
    big_px_ = big_px;
  }
  if (!label_font_ || label_px_ != label_px) {
    if (label_font_) DeleteObject(label_font_);
    label_font_ = make(label_px, L"Microsoft YaHei UI");
    label_px_ = label_px;
  }
  if (!title_font_ || title_px_ != title_px) {
    if (title_font_) DeleteObject(title_font_);
    title_font_ = make(title_px, L"Microsoft YaHei UI");
    title_px_ = title_px;
  }
}

// 按给定比例实测一遍：每一行文字都量过，窗口就是内容的外接框。
void CPartsWindow::BuildLayout(double scale, bool vertical, Layout* out) {
  auto px = [scale](double v) {
    return std::max(1, static_cast<int>(std::lround(v * scale)));
  };
  Layout lay;
  lay.vertical = vertical;
  lay.pad = px(10);
  lay.title_height = px(26);
  lay.big_px = px(60);
  lay.label_px = px(15);
  lay.title_px = px(13);
  const int pad = lay.pad;
  const int grid = px(84);
  const int gap = px(4);
  EnsureFonts(lay.big_px, lay.label_px, lay.title_px);

  lay.title = L"拆字  ";
  for (const CharParts* one : chars_) lay.title += one->glyph;
  if (truncated_) lay.title += L"…";

  const HDC dc = CreateCompatibleDC(nullptr);
  const HGDIOBJ old = dc ? SelectObject(dc, label_font_) : nullptr;
  const int text_h = dc ? LineHeight(dc) : px(20);
  const int line_h = text_h + gap;

  int content_w = 0;
  int y_end = 0;
  const int top = lay.title_height + pad;
  int x = pad;
  int y = top;
  for (size_t i = 0; i < chars_.size(); ++i) {
    const CharParts& one = *chars_[i];
    std::vector<std::wstring> texts;
    texts.push_back(StructureLine(one.structure));
    for (const PartEntry& part : one.parts) texts.push_back(PartLine(part));
    int info_w = 0;
    for (const std::wstring& t : texts) {
      info_w = std::max(info_w, dc ? TextWidth(dc, t) : px(15) * static_cast<int>(t.size()));
    }
    const int info_h = static_cast<int>(texts.size()) * line_h - gap;

    if (vertical) {
      // 每字一行：格在左，说明在右，两者竖直居中。
      const int row_h = std::max(grid, info_h);
      Cell cell;
      cell.box = {pad, y + (row_h - grid) / 2, pad + grid, y + (row_h - grid) / 2 + grid};
      cell.glyph = one.glyph;
      lay.cells.push_back(cell);
      int ly = y + (row_h - info_h) / 2;
      for (size_t k = 0; k < texts.size(); ++k) {
        Line line;
        line.text = texts[k];
        line.x = pad + grid + pad;
        line.y = ly;
        line.dim = k == 0;
        lay.lines.push_back(std::move(line));
        ly += line_h;
      }
      content_w = std::max(content_w, pad + grid + pad + info_w + pad);
      y += row_h;
      if (i + 1 < chars_.size()) {
        lay.rules.push_back({pad, y + pad / 2, 0, y + pad / 2 + 1});  // 右端最后补
        y += pad;
      }
      y_end = y + pad;
    } else {
      // 多字并排：格在上，说明在下。
      const int col_w = std::max(grid, info_w);
      Cell cell;
      cell.box = {x + (col_w - grid) / 2, top, x + (col_w - grid) / 2 + grid, top + grid};
      cell.glyph = one.glyph;
      lay.cells.push_back(cell);
      int ly = top + grid + pad / 2;
      for (size_t k = 0; k < texts.size(); ++k) {
        Line line;
        line.text = texts[k];
        line.x = x;
        line.y = ly;
        line.dim = k == 0;
        lay.lines.push_back(std::move(line));
        ly += line_h;
      }
      y_end = std::max(y_end, top + grid + pad / 2 + info_h + pad);
      x += col_w;
      if (i + 1 < chars_.size()) {
        lay.rules.push_back({x + pad, top, x + pad + 1, 0});  // 下端最后补
        x += 2 * pad;
      }
      content_w = x + pad;
    }
  }

  // 标题条要放得下标题和关闭钮。
  int title_w = px(120);
  if (dc) {
    SelectObject(dc, title_font_);
    title_w = TextWidth(dc, lay.title);
  }
  lay.width = std::max({content_w, pad + title_w + pad + lay.title_height, px(200)});
  lay.height = std::max(y_end, lay.title_height + pad + grid + pad);
  for (RECT& rule : lay.rules) {
    if (vertical) {
      rule.right = lay.width - pad;
    } else {
      rule.bottom = lay.height - pad;
    }
  }
  if (dc) {
    SelectObject(dc, old);
    DeleteDC(dc);
  }
  *out = std::move(lay);
}

void CPartsWindow::Relayout() {
  if (!hwnd_ || chars_.empty()) return;
  if (!has_anchor_) DefaultAnchor();
  const POINT probe = {anchor_right_ ? anchor_.x - 1 : anchor_.x,
                       anchor_bottom_ ? anchor_.y - 1 : anchor_.y};
  const RECT work = WorkAreaAt(probe);
  const int work_w = static_cast<int>(work.right - work.left);
  const int work_h = static_cast<int>(work.bottom - work.top);

  // 放不进工作区就整体缩小（只是这一次画小，zoom_ 不动）。字号取整，
  // 缩放不是严格线性的，所以每轮多缩一点、最多试几轮。
  double scale = zoom_ * DpiFactor();
  Layout next;
  for (int round = 0; round < 8; ++round) {
    BuildLayout(scale, vertical_, &next);
    if ((next.width <= work_w && next.height <= work_h) || scale <= 0.3) break;
    const double fit = std::min(static_cast<double>(work_w) / next.width,
                                static_cast<double>(work_h) / next.height);
    scale *= std::min(fit, 0.95);
  }
  scale_ = scale;
  layout_ = std::move(next);

  int x = anchor_right_ ? static_cast<int>(anchor_.x) - layout_.width
                        : static_cast<int>(anchor_.x);
  int y = anchor_bottom_ ? static_cast<int>(anchor_.y) - layout_.height
                         : static_cast<int>(anchor_.y);
  x = std::max(static_cast<int>(work.left),
               std::min(x, static_cast<int>(work.right) - layout_.width));
  y = std::max(static_cast<int>(work.top),
               std::min(y, static_cast<int>(work.bottom) - layout_.height));
  SetWindowPos(hwnd_, HWND_TOPMOST, x, y, layout_.width, layout_.height,
               SWP_NOACTIVATE);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---- 位置：锚点、默认位置、注册表 ----------------------------------------

void CPartsWindow::DefaultAnchor() {
  // 没挪过：放在当前程序所在屏幕的右上角，往左长。
  const HWND foreground = GetForegroundWindow();
  RECT work = {};
  MONITORINFO info = {};
  info.cbSize = sizeof(info);
  const HMONITOR monitor =
      foreground ? MonitorFromWindow(foreground, MONITOR_DEFAULTTOPRIMARY)
                 : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
  if (monitor && GetMonitorInfoW(monitor, &info)) {
    work = info.rcWork;
  } else {
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  }
  const int margin = MulDiv(24, static_cast<int>(Dpi()), 96);
  anchor_right_ = true;
  anchor_bottom_ = false;
  anchor_.x = work.right - margin;
  anchor_.y = work.top + margin;
  has_anchor_ = true;
}

// 用户放下窗口之后，记住离屏幕边最近的那个角：放在右边的窗口换了个更宽
// 的词就往左长，放在底下的往上长，不会长出屏幕。
void CPartsWindow::AnchorToWindow() {
  RECT rc = {};
  if (!hwnd_ || !GetWindowRect(hwnd_, &rc)) return;
  const POINT center = {(rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2};
  const RECT work = WorkAreaAt(center);
  anchor_right_ = center.x > (work.left + work.right) / 2;
  anchor_bottom_ = center.y > (work.top + work.bottom) / 2;
  anchor_.x = anchor_right_ ? rc.right : rc.left;
  anchor_.y = anchor_bottom_ ? rc.bottom : rc.top;
  has_anchor_ = true;
}

void CPartsWindow::LoadPlacement() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_QUERY_VALUE,
                    &key) != ERROR_SUCCESS) {
    return;
  }
  bool has_x = false, has_y = false, has_anchor = false, has_zoom = false;
  const LONG x = ReadDword(key, L"X", &has_x);
  const LONG y = ReadDword(key, L"Y", &has_y);
  const LONG anchor = ReadDword(key, L"Anchor", &has_anchor);
  const LONG zoom = ReadDword(key, L"Zoom", &has_zoom);
  RegCloseKey(key);
  if (has_zoom && zoom >= 40 && zoom <= 400) {
    zoom_ = std::clamp(zoom / 100.0, kMinZoom, kMaxZoom);
  }
  if (has_x && has_y) {
    const bool right = has_anchor && (anchor & 1) != 0;
    const bool bottom = has_anchor && (anchor & 2) != 0;
    const POINT probe = {right ? x - 1 : x, bottom ? y - 1 : y};
    // 那块屏幕已经拔掉了就当没存过。
    if (MonitorFromPoint(probe, MONITOR_DEFAULTTONULL)) {
      anchor_ = {x, y};
      anchor_right_ = right;
      anchor_bottom_ = bottom;
      has_anchor_ = true;
    }
  }
}

void CPartsWindow::SavePlacement() const {
  // 存不进去（受限进程、沙箱）就算了，下次用默认位置。
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, nullptr, 0,
                      KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
    return;
  }
  if (has_anchor_) {
    WriteDword(key, L"X", anchor_.x);
    WriteDword(key, L"Y", anchor_.y);
    WriteDword(key, L"Anchor", (anchor_right_ ? 1 : 0) | (anchor_bottom_ ? 2 : 0));
  }
  WriteDword(key, L"Zoom", static_cast<LONG>(std::lround(zoom_ * 100)));
  RegCloseKey(key);
}

// ---- 鼠标：拖动、缩放、关闭 ------------------------------------------------

RECT CPartsWindow::CloseRect() const {
  return {layout_.width - layout_.title_height, 0, layout_.width,
          layout_.title_height};
}

int CPartsWindow::HitEdges(POINT p) const {
  const int grip = std::max(4, MulDiv(6, static_cast<int>(Dpi()), 96));
  int edges = 0;
  if (p.x < grip) edges |= kEdgeLeft;
  if (p.x >= layout_.width - grip) edges |= kEdgeRight;
  if (p.y < grip) edges |= kEdgeTop;
  if (p.y >= layout_.height - grip) edges |= kEdgeBottom;
  return edges;
}

LPCWSTR CPartsWindow::CursorFor(POINT p) const {
  const int edges = HitEdges(p);
  const bool horizontal = (edges & (kEdgeLeft | kEdgeRight)) != 0;
  const bool vertical = (edges & (kEdgeTop | kEdgeBottom)) != 0;
  if (horizontal && vertical) {
    const bool main_diagonal = (edges & kEdgeLeft) ? (edges & kEdgeTop) != 0
                                                   : (edges & kEdgeBottom) != 0;
    return main_diagonal ? IDC_SIZENWSE : IDC_SIZENESW;
  }
  if (horizontal) return IDC_SIZEWE;
  if (vertical) return IDC_SIZENS;
  const RECT close = CloseRect();
  if (PtInRect(&close, p)) return IDC_HAND;
  if (p.y < layout_.title_height) return IDC_SIZEALL;
  return IDC_ARROW;
}

void CPartsWindow::BeginDrag(Drag kind, int edges, POINT screen) {
  drag_ = kind;
  drag_edges_ = edges;
  drag_changed_ = false;
  drag_start_ = screen;
  GetWindowRect(hwnd_, &drag_rect_);
  drag_zoom_ = std::clamp(scale_ / DpiFactor(), kMinZoom, kMaxZoom);
  if (kind == Drag::kResize) {
    // 缩放时钉住对边：拖右边，左边不动；拖左上角，右下角不动。
    anchor_right_ = (edges & kEdgeLeft) != 0;
    anchor_bottom_ = (edges & kEdgeTop) != 0;
    anchor_.x = anchor_right_ ? drag_rect_.right : drag_rect_.left;
    anchor_.y = anchor_bottom_ ? drag_rect_.bottom : drag_rect_.top;
    has_anchor_ = true;
  }
  SetCapture(hwnd_);
}

void CPartsWindow::UpdateDrag(POINT screen) {
  const int dx = static_cast<int>(screen.x - drag_start_.x);
  const int dy = static_cast<int>(screen.y - drag_start_.y);
  if (drag_ == Drag::kMove) {
    if (dx == 0 && dy == 0 && !drag_changed_) return;
    drag_changed_ = true;
    SetWindowPos(hwnd_, nullptr, static_cast<int>(drag_rect_.left) + dx,
                 static_cast<int>(drag_rect_.top) + dy, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return;
  }
  if (drag_ != Drag::kResize) return;
  // 等比缩放：拖哪条边就看哪个方向拉长了多少；拖角取变化大的那个方向。
  const int w0 = std::max(1, static_cast<int>(drag_rect_.right - drag_rect_.left));
  const int h0 = std::max(1, static_cast<int>(drag_rect_.bottom - drag_rect_.top));
  int w = w0;
  int h = h0;
  if (drag_edges_ & kEdgeRight) w += dx;
  if (drag_edges_ & kEdgeLeft) w -= dx;
  if (drag_edges_ & kEdgeBottom) h += dy;
  if (drag_edges_ & kEdgeTop) h -= dy;
  const double rw = static_cast<double>(std::max(1, w)) / w0;
  const double rh = static_cast<double>(std::max(1, h)) / h0;
  const bool horizontal = (drag_edges_ & (kEdgeLeft | kEdgeRight)) != 0;
  const bool vertical = (drag_edges_ & (kEdgeTop | kEdgeBottom)) != 0;
  double ratio = horizontal ? rw : rh;
  if (horizontal && vertical) {
    ratio = std::fabs(rw - 1.0) > std::fabs(rh - 1.0) ? rw : rh;
  }
  const double zoom = std::clamp(drag_zoom_ * ratio, kMinZoom, kMaxZoom);
  if (std::fabs(zoom - zoom_) < 0.005 && drag_changed_) return;
  zoom_ = zoom;
  drag_changed_ = true;
  Relayout();
  // 拉到放不下了，窗口就停在那个大小上；zoom_ 跟着停，往回拖立刻有反应。
  zoom_ = std::min(zoom_, scale_ / DpiFactor());
}

void CPartsWindow::EndDrag(bool keep) {
  const Drag was = drag_;
  const bool changed = drag_changed_;
  drag_ = Drag::kNone;  // 先清：下面的 ReleaseCapture 会同步发 WM_CAPTURECHANGED
  drag_changed_ = false;
  if (hwnd_ && GetCapture() == hwnd_) ReleaseCapture();
  if (keep && changed && (was == Drag::kMove || was == Drag::kResize)) {
    AnchorToWindow();
    SavePlacement();
  }
}

// ---- 绘制 ------------------------------------------------------------------

void CPartsWindow::Paint(HDC dc) {
  const RECT all = {0, 0, layout_.width, layout_.height};
  Fill(dc, all, GetSysColor(COLOR_WINDOW));
  SetBkMode(dc, TRANSPARENT);

  // 标题条
  const RECT bar = {0, 0, layout_.width, layout_.title_height};
  Fill(dc, bar, GetSysColor(COLOR_3DFACE));
  const RECT close = CloseRect();
  const HGDIOBJ old_font = SelectObject(dc, title_font_);
  SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
  RECT title = {layout_.pad, 0, close.left, layout_.title_height};
  DrawTextW(dc, layout_.title.c_str(), -1, &title,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  if (close_hot_) Fill(dc, close, RGB(232, 17, 35));
  DrawCross(dc, close, close_hot_ ? RGB(255, 255, 255) : GetSysColor(COLOR_BTNTEXT));

  // 米字格和大字
  SelectObject(dc, big_font_);
  SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
  for (const Cell& cell : layout_.cells) {
    DrawMiziGrid(dc, cell.box);
    RECT box = cell.box;
    DrawTextW(dc, cell.glyph.c_str(), static_cast<int>(cell.glyph.size()), &box,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }

  // 结构与部件
  SelectObject(dc, label_font_);
  for (const Line& line : layout_.lines) {
    SetTextColor(dc, GetSysColor(line.dim ? COLOR_GRAYTEXT : COLOR_WINDOWTEXT));
    TextOutW(dc, line.x, line.y, line.text.c_str(),
             static_cast<int>(line.text.size()));
  }
  for (const RECT& rule : layout_.rules) Fill(dc, rule, RGB(225, 225, 232));

  // 一像素的边框，窗口没有系统边框，靠它跟底下的内容分开
  const HBRUSH frame = CreateSolidBrush(GetSysColor(COLOR_BTNSHADOW));
  if (frame) {
    FrameRect(dc, &all, frame);
    DeleteObject(frame);
  }
  SelectObject(dc, old_font);
}

// ---- 消息 ------------------------------------------------------------------

LRESULT CALLBACK CPartsWindow::WindowProc(HWND hwnd, UINT msg, WPARAM w,
                                          LPARAM l) {
  if (msg == WM_NCCREATE) {
    const auto* create = reinterpret_cast<CREATESTRUCTW*>(l);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  }
  auto* self =
      reinterpret_cast<CPartsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (!self || self->hwnd_ != hwnd) {
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(hwnd, msg, w, l);
  }
  return self->OnMessage(msg, w, l);
}

LRESULT CPartsWindow::OnMessage(UINT msg, WPARAM w, LPARAM l) {
  switch (msg) {
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;  // 点它不抢焦点
    case WM_SETCURSOR:
      if (LOWORD(l) == HTCLIENT && drag_ == Drag::kNone) {
        POINT p = {};
        GetCursorPos(&p);
        ScreenToClient(hwnd_, &p);
        SetCursor(LoadCursor(nullptr, CursorFor(p)));
        return TRUE;
      }
      break;
    case WM_LBUTTONDOWN: {
      const POINT p = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
      POINT screen = p;
      ClientToScreen(hwnd_, &screen);
      const RECT close = CloseRect();
      const int edges = HitEdges(p);
      if (edges) {
        BeginDrag(Drag::kResize, edges, screen);
      } else if (PtInRect(&close, p)) {
        BeginDrag(Drag::kClose, 0, screen);
      } else {
        BeginDrag(Drag::kMove, 0, screen);  // 整个窗口哪里都能拖
      }
      return 0;
    }
    case WM_MOUSEMOVE: {
      if (drag_ == Drag::kMove || drag_ == Drag::kResize) {
        POINT screen = {};
        GetCursorPos(&screen);
        UpdateDrag(screen);
        return 0;
      }
      const POINT p = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
      const RECT close = CloseRect();
      const bool hot = PtInRect(&close, p) && !HitEdges(p) &&
                       (drag_ == Drag::kNone || drag_ == Drag::kClose);
      if (hot != close_hot_) {
        close_hot_ = hot;
        InvalidateRect(hwnd_, &close, FALSE);
      }
      if (!tracking_mouse_) {
        TRACKMOUSEEVENT track = {};
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = hwnd_;
        tracking_mouse_ = TrackMouseEvent(&track) != FALSE;
      }
      return 0;
    }
    case WM_MOUSELEAVE:
      tracking_mouse_ = false;
      if (close_hot_) {
        close_hot_ = false;
        const RECT close = CloseRect();
        InvalidateRect(hwnd_, &close, FALSE);
      }
      return 0;
    case WM_LBUTTONUP: {
      const POINT p = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
      const Drag was = drag_;
      EndDrag(true);
      const RECT close = CloseRect();
      if (was == Drag::kClose && PtInRect(&close, p)) Dismiss();
      return 0;
    }
    case WM_CAPTURECHANGED:
      // 拖到一半被别人抢走鼠标（比如 Alt+Tab）：就停在当前位置。
      if (drag_ != Drag::kNone) EndDrag(true);
      return 0;
    case WM_LBUTTONDBLCLK: {
      // 双击标题条：缩放复位 100%。
      const POINT p = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
      const RECT close = CloseRect();
      if (p.y < layout_.title_height && !PtInRect(&close, p) && !HitEdges(p)) {
        zoom_ = 1.0;
        Relayout();
        SavePlacement();
      }
      return 0;
    }
    case WM_RBUTTONUP:
      Dismiss();  // 右键关掉；这次组字里不再弹出
      return 0;
    case WM_MOUSEWHEEL: {
      const int delta = GET_WHEEL_DELTA_WPARAM(w);
      if (delta != 0 && drag_ == Drag::kNone) {
        const double base = std::clamp(scale_ / DpiFactor(), kMinZoom, kMaxZoom);
        zoom_ = std::clamp(delta > 0 ? base * kWheelStep : base / kWheelStep,
                           kMinZoom, kMaxZoom);
        Relayout();
        zoom_ = std::min(zoom_, scale_ / DpiFactor());
        SavePlacement();
      }
      return 0;
    }
    case WM_TIMER:
      if (w == kForegroundTimer) CheckForeground();
      return 0;
    case WM_DPICHANGED:
      Relayout();
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps = {};
      const HDC dc = BeginPaint(hwnd_, &ps);
      if (dc) {
        // 双缓冲：先画进内存位图再一次贴上去，缩放拖动时不闪。
        const int width = std::max(1, layout_.width);
        const int height = std::max(1, layout_.height);
        const HDC memory = CreateCompatibleDC(dc);
        const HBITMAP bitmap =
            memory ? CreateCompatibleBitmap(dc, width, height) : nullptr;
        if (memory && bitmap) {
          const HGDIOBJ old = SelectObject(memory, bitmap);
          Paint(memory);
          BitBlt(dc, 0, 0, width, height, memory, 0, 0, SRCCOPY);
          SelectObject(memory, old);
        } else {
          Paint(dc);
        }
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
      }
      EndPaint(hwnd_, &ps);
      return 0;
    }
    case WM_CLOSE:
      Dismiss();
      return 0;
    case WM_DESTROY:
      KillTimer(hwnd_, kForegroundTimer);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd_, msg, w, l);
}

}  // namespace zuxia
