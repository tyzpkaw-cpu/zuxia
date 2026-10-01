// PartsWindow.cpp -- Zuxia IME parts-analysis window (learning mode)
// Vertical layout (default): one row per character, grid left, full info right.
// Horizontal layout: side-by-side grids (compact).
// Non-focus-stealing; draggable; user-resizable; right-click to hide.

#include "Globals.h"
#include "PartsWindow.h"
#include "Settings.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace zuxia {

namespace {
constexpr wchar_t kClassName[] = L"ZuxiaIMEPartsWindow";
constexpr wchar_t kTsvName[]   = L"zuxia.parts.tsv";

const wchar_t* StructureNameFor(const std::wstring& code) {
    if (code == L"z") return L"\u5de6\u53f3\u7ed3\u6784";
    if (code == L"s") return L"\u4e0a\u4e0b\u7ed3\u6784";
    if (code == L"b") return L"\u5305\u56f4\u7ed3\u6784";
    if (code == L"p") return L"\u54c1\u5b57/\u5176\u4ed6";
    if (code == L"d") return L"\u72ec\u4f53\u5b57";
    return L"\u2014";
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
    return out;
}

std::vector<std::wstring> SplitTab(const std::wstring& line) {
    std::vector<std::wstring> out;
    std::wstring cur;
    for (wchar_t c : line) {
        if (c == L'\t') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

std::vector<std::wstring> Split(const std::wstring& s, wchar_t sep) {
    std::vector<std::wstring> out;
    std::wstring cur;
    for (wchar_t c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

}  // namespace

// -- static members --------------------------------------------------------
ATOM      CPartsWindow::atom_      = 0;
INIT_ONCE CPartsWindow::init_once_ = INIT_ONCE_STATIC_INIT;

// -- lifecycle -------------------------------------------------------------
CPartsWindow::CPartsWindow()  = default;
CPartsWindow::~CPartsWindow() { Destroy(); }

BOOL CPartsWindow::InitWindowClass() {
    return InitOnceExecuteOnce(&init_once_, RegisterClassOnce, nullptr, nullptr);
}
void CPartsWindow::UninitWindowClass() {}

BOOL CALLBACK CPartsWindow::RegisterClassOnce(PINIT_ONCE, PVOID, PVOID*) {
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = g_hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;
    atom_ = RegisterClassExW(&wc);
    return atom_ != 0;
}

bool CPartsWindow::Create() {
    if (hwnd_) return true;
    if (!InitWindowClass()) return false;
    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kClassName, L"\u62c6\u5b57",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX,
        100, 100, width_, height_,
        nullptr, nullptr, g_hInst, this);
    return hwnd_ != nullptr;
}

void CPartsWindow::Destroy() {
    if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; }
    if (font_big_)   { DeleteObject(font_big_);   font_big_   = nullptr; }
    if (font_label_) { DeleteObject(font_label_); font_label_ = nullptr; }
}

// -- show/hide -------------------------------------------------------------
void CPartsWindow::ShowWord(const std::wstring& text) {
    if (!hwnd_ || text.empty()) { Hide(); return; }
    EnsureLoaded();

    std::vector<CharParts> chars;
    for (wchar_t ch : text) {
        auto it = table_.find(ch);
        if (it != table_.end()) chars.push_back(it->second);
    }
    if (chars.empty()) { Hide(); return; }

    // If the number of characters changed, forget the user-resize so the
    // window resizes to fit the new content automatically.
    if (chars.size() != current_chars_.size()) {
        user_resized_ = false;
    }

    current_chars_ = std::move(chars);
    current_word_  = text;

    // Rebuild fonts in case DPI changed.
    if (font_big_)   { DeleteObject(font_big_);   font_big_   = nullptr; }
    if (font_label_) { DeleteObject(font_label_); font_label_ = nullptr; }

    // Resize only when user hasn't manually dragged the window border.
    if (!user_resized_) {
        RecalcSize();
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, width_, height_,
                     SWP_NOMOVE | SWP_NOACTIVATE);
    } else {
        // Still keep TOPMOST and no-activate, but don't change size.
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    InvalidateRect(hwnd_, nullptr, TRUE);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
}

void CPartsWindow::Hide() {
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
    current_chars_.clear();
    current_word_.clear();
    // Don't reset user_resized_ here -- user dragged it to a preferred size,
    // keep that across hide/show cycles of the same word length.
}

bool CPartsWindow::Visible() const {
    return hwnd_ && IsWindowVisible(hwnd_);
}

// -- lazy-load TSV ---------------------------------------------------------
void CPartsWindow::EnsureLoaded() {
    if (loaded_) return;
    loaded_ = true;
    wchar_t dll_path[MAX_PATH] = {};
    GetModuleFileNameW(g_hInst, dll_path, MAX_PATH);
    std::filesystem::path dll_fs(dll_path);
    std::wstring tsv_path =
        (dll_fs.parent_path().parent_path() / L"data" / kTsvName).wstring();
    std::ifstream f(tsv_path, std::ios::binary);
    if (!f.is_open()) return;
    std::string line_u8;
    while (std::getline(f, line_u8)) {
        if (!line_u8.empty() && line_u8.back() == '\r') line_u8.pop_back();
        if (line_u8.empty() || line_u8[0] == '#') continue;
        std::wstring line = Utf8ToWide(line_u8);
        auto cols = SplitTab(line);
        if (cols.size() < 3) continue;
        CharParts cp;
        cp.ch        = cols[0].empty() ? 0 : cols[0][0];
        cp.structure = cols[1];
        if (!cp.ch) continue;
        for (size_t i = 2; i < cols.size(); ++i) {
            auto fields = Split(cols[i], L'|');
            if (fields.empty()) continue;
            PartEntry pe;
            pe.glyph   = fields.size() > 0 ? fields[0] : L"";
            pe.names   = fields.size() > 1 ? fields[1] : L"";
            pe.letters = fields.size() > 2 ? fields[2] : L"";
            pe.pinyin  = fields.size() > 3 ? fields[3] : L"";
            if (!pe.glyph.empty()) cp.parts.push_back(std::move(pe));
        }
        table_[cp.ch] = std::move(cp);
    }
}

// -- size ------------------------------------------------------------------
void CPartsWindow::RecalcSize() {
    if (current_chars_.empty()) {
        width_  = Scale(320);
        height_ = Scale(200);
        return;
    }
    const bool vert  = CurrentAppearance().parts_vertical;
    const int  pad   = Scale(8);
    const int  cellW = Scale(90);
    const int  cellH = Scale(90);
    const int  rowH  = Scale(22);
    const int  n     = static_cast<int>(current_chars_.size());

    if (vert) {
        int total_h = pad;
        for (const auto& cp : current_chars_) {
            const int info_h = rowH + static_cast<int>(cp.parts.size()) * rowH;
            total_h += std::max(cellH, info_h) + pad;
        }
        width_  = cellW + Scale(260) + 2 * pad;
        height_ = total_h;
    } else {
        int max_parts = 0;
        for (const auto& cp : current_chars_)
            max_parts = std::max(max_parts, static_cast<int>(cp.parts.size()));
        const int col_w = cellW + Scale(150);
        width_  = n * col_w + (n + 1) * pad;
        height_ = pad + cellH + rowH + max_parts * Scale(28) + pad;
    }
    if (width_  < Scale(300)) width_  = Scale(300);
    if (height_ < Scale(120)) height_ = Scale(120);
}

int CPartsWindow::Scale(int v) const {
    UINT dpi = 96;
    if (hwnd_) {
        using Fn = UINT(WINAPI*)(HWND);
        const auto fn = reinterpret_cast<Fn>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
        if (fn) dpi = fn(hwnd_);
    }
    return MulDiv(v, dpi, 96);
}

// -- paint -----------------------------------------------------------------
void CPartsWindow::Paint(HDC dc, const RECT& client) {
    HBRUSH bg = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
    if (bg) { FillRect(dc, &client, bg); DeleteObject(bg); }
    SetBkMode(dc, TRANSPARENT);
    if (current_chars_.empty()) return;

    const bool vert = CurrentAppearance().parts_vertical;
    const int  pad  = Scale(8);
    const int  n    = static_cast<int>(current_chars_.size());

    if (vert) {
        const int cellW = Scale(90);
        const int cellH = Scale(90);
        const int rowH  = Scale(22);
        const int cw    = static_cast<int>(client.right);

        int y = pad;
        for (int i = 0; i < n; ++i) {
            const CharParts& cp = current_chars_[i];
            const int info_lines = 1 + static_cast<int>(cp.parts.size());
            const int info_h     = info_lines * rowH;
            const int row_h      = std::max(cellH, info_h);

            const int grid_y = y + (row_h - cellH) / 2;
            RECT cell_rc = { pad, grid_y, pad + cellW, grid_y + cellH };
            DrawMiziGrid(dc, cell_rc);

            EnsureFonts();
            HFONT old = font_big_
                ? reinterpret_cast<HFONT>(SelectObject(dc, font_big_)) : nullptr;
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            wchar_t ch_str[2] = { cp.ch, 0 };
            DrawTextW(dc, ch_str, 1, &cell_rc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (old) SelectObject(dc, old);

            old = font_label_
                ? reinterpret_cast<HFONT>(SelectObject(dc, font_label_)) : nullptr;

            const int ix = pad + cellW + pad;
            int iy = y + (row_h - info_h) / 2;

            {
                std::wstring s = L"\u7ed3\u6784\uff1a";
                s += StructureNameFor(cp.structure);
                RECT r = { ix, iy, cw - pad, iy + rowH };
                SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
                DrawTextW(dc, s.c_str(), -1, &r,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                iy += rowH;
            }

            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            for (const auto& p : cp.parts) {
                std::wstring first_name = p.names;
                auto comma = first_name.find(L',');
                if (comma != std::wstring::npos)
                    first_name = first_name.substr(0, comma);

                std::wstring keys = p.letters;
                for (auto& c : keys)
                    if (c >= L'a' && c <= L'z') c -= 32;

                std::wstring line = p.glyph + L"  " + first_name;
                if (!p.pinyin.empty()) line += L"  " + p.pinyin;
                if (!keys.empty())     line += L"  \u2192  " + keys;

                RECT r = { ix, iy, cw - pad, iy + rowH };
                DrawTextW(dc, line.c_str(), -1, &r,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                iy += rowH;
            }

            if (old) SelectObject(dc, old);

            if (i < n - 1) {
                const int sep_y = y + row_h + pad / 2;
                HPEN pen = CreatePen(PS_SOLID, 1, RGB(220, 220, 220));
                if (pen) {
                    HPEN op = reinterpret_cast<HPEN>(SelectObject(dc, pen));
                    MoveToEx(dc, pad, sep_y, nullptr);
                    LineTo(dc, cw - pad, sep_y);
                    SelectObject(dc, op);
                    DeleteObject(pen);
                }
            }
            y += row_h + pad;
        }
    } else {
        const int col_w =
            (static_cast<int>(client.right) - (n + 1) * pad) / n;
        if (col_w <= 0) return;
        for (int i = 0; i < n; ++i) {
            int cx = pad + i * (col_w + pad);
            RECT col_rc = { cx, pad,
                            cx + col_w,
                            static_cast<int>(client.bottom) - pad };
            DrawCharGridHoriz(dc, col_rc, current_chars_[i]);
        }
    }
}

void CPartsWindow::DrawCharGridHoriz(HDC dc, const RECT& col_rc,
                                      const CharParts& cp) {
    const int pad   = Scale(8);
    const int cellW = Scale(90);
    const int cellH = Scale(90);
    const int col_w = static_cast<int>(col_rc.right  - col_rc.left);
    const int col_h = static_cast<int>(col_rc.bottom - col_rc.top);
    const int gw = std::min(cellW, col_w);
    const int gh = std::min(cellH, col_h);

    RECT cell_rc = { col_rc.left, col_rc.top,
                     col_rc.left + gw, col_rc.top + gh };
    DrawMiziGrid(dc, cell_rc);

    EnsureFonts();
    HFONT old = font_big_
        ? reinterpret_cast<HFONT>(SelectObject(dc, font_big_)) : nullptr;
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    wchar_t ch_str[2] = { cp.ch, 0 };
    DrawTextW(dc, ch_str, 1, &cell_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (old) SelectObject(dc, old);

    old = font_label_
        ? reinterpret_cast<HFONT>(SelectObject(dc, font_label_)) : nullptr;
    const int ix = col_rc.left + gw + pad;
    int y = col_rc.top;
    {
        std::wstring s = L"\u7ed3\u6784\uff1a";
        s += StructureNameFor(cp.structure);
        RECT r = { ix, y, col_rc.right, y + Scale(22) };
        SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
        DrawTextW(dc, s.c_str(), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        y += Scale(24);
    }
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    for (const auto& p : cp.parts) {
        std::wstring first_name = p.names;
        auto comma = first_name.find(L',');
        if (comma != std::wstring::npos) first_name = first_name.substr(0, comma);
        std::wstring keys = p.letters;
        for (auto& c : keys) if (c >= L'a' && c <= L'z') c -= 32;
        std::wstring row_text = p.glyph + L"  " + first_name;
        if (!p.pinyin.empty()) row_text += L"  " + p.pinyin;
        if (!keys.empty())     row_text += L"  \u2192  " + keys;
        RECT r = { ix, y, col_rc.right, y + Scale(26) };
        DrawTextW(dc, row_text.c_str(), -1, &r,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        y += Scale(28);
    }
    if (old) SelectObject(dc, old);
}

void CPartsWindow::EnsureFonts() {
    if (!font_big_) {
        font_big_ = CreateFontW(
            -Scale(56), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"\u5b8b\u4f53");
    }
    if (!font_label_) {
        font_label_ = CreateFontW(
            -Scale(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"\u5fae\u8f6f\u96c5\u9ed8");
    }
}

void CPartsWindow::DrawMiziGrid(HDC dc, const RECT& r) {
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(180, 180, 200));
    HPEN old = pen ? reinterpret_cast<HPEN>(SelectObject(dc, pen)) : nullptr;
    Rectangle(dc, r.left, r.top, r.right, r.bottom);
    const int cx = static_cast<int>((r.left + r.right)  / 2);
    const int cy = static_cast<int>((r.top  + r.bottom) / 2);
    MoveToEx(dc, r.left, cy, nullptr); LineTo(dc, r.right, cy);
    MoveToEx(dc, cx, r.top, nullptr);  LineTo(dc, cx, r.bottom);
    HPEN pen2 = CreatePen(PS_DOT, 1, RGB(210, 210, 220));
    if (pen2) {
        SelectObject(dc, pen2);
        MoveToEx(dc, r.left, r.top,  nullptr); LineTo(dc, r.right, r.bottom);
        MoveToEx(dc, r.right, r.top, nullptr); LineTo(dc, r.left,  r.bottom);
        SelectObject(dc, old ? old
            : reinterpret_cast<HPEN>(GetStockObject(BLACK_PEN)));
        DeleteObject(pen2);
    }
    if (old) SelectObject(dc, old);
    if (pen) DeleteObject(pen);
}

// -- message handler -------------------------------------------------------
LRESULT CALLBACK CPartsWindow::WindowProc(HWND hwnd, UINT msg,
                                           WPARAM w, LPARAM l) {
    CPartsWindow* self = reinterpret_cast<CPartsWindow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
        self = static_cast<CPartsWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
    }
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (msg == WM_CLOSE)  { if (self) self->Hide(); return 0; }
    if (msg == WM_SIZE) {
        if (self) {
            // Mark as user-resized only when the user drags the border
            // (wParam == SIZE_RESTORED and window was already visible).
            if (w == SIZE_RESTORED && IsWindowVisible(hwnd))
                self->user_resized_ = true;
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    }
    if (msg == WM_PAINT && self) {
        PAINTSTRUCT ps = {};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT client = {};
        GetClientRect(hwnd, &client);
        self->Paint(dc, client);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_RBUTTONUP && self) { self->Hide(); return 0; }
    return DefWindowProcW(hwnd, msg, w, l);
}

}  // namespace zuxia
