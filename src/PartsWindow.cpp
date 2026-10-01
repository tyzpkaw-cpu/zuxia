// PartsWindow.cpp — 足下输入法米字格拆字窗口（任务 7）
// 落字后自动弹出，显示所选字的部件拆分、名称、带调拼音、字母键。
// 不抢焦点；可拖动；双击标题区或右键隐藏。

#include "Globals.h"
#include "PartsWindow.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace zuxia {

namespace {
constexpr wchar_t kClassName[] = L"ZuxiaIMEPartsWindow";

// data/ 下相对于 DLL 所在目录的路径
constexpr wchar_t kTsvName[] = L"zuxia.parts.tsv";

// 结构码 → 中文名
const wchar_t* StructureNameFor(const std::wstring& code) {
    if (code == L"z") return L"左右结构";
    if (code == L"s") return L"上下结构";
    if (code == L"b") return L"包围结构";
    if (code == L"p") return L"品字/其他";
    if (code == L"d") return L"独体字";
    return L"—";
}

// 把 UTF-8 std::string 转成 std::wstring
std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
    return out;
}

// 按 TAB 分割（单字节，不需要宽字符）
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

// 按指定字符分割
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

// ── 静态成员 ────────────────────────────────────────────────────────────
ATOM      CPartsWindow::atom_      = 0;
INIT_ONCE CPartsWindow::init_once_ = INIT_ONCE_STATIC_INIT;

// ── 生命周期 ────────────────────────────────────────────────────────────
CPartsWindow::CPartsWindow()  = default;
CPartsWindow::~CPartsWindow() { Destroy(); }

BOOL CPartsWindow::InitWindowClass() {
    return InitOnceExecuteOnce(&init_once_, RegisterClassOnce,
                               nullptr, nullptr);
}

void CPartsWindow::UninitWindowClass() { /* 随 DLL 自动释放 */ }

BOOL CALLBACK CPartsWindow::RegisterClassOnce(
        PINIT_ONCE, PVOID, PVOID*) {
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

    // WS_CAPTION 提供可拖动的标题栏；WS_EX_NOACTIVATE 保证不抢焦点。
    // WS_SYSMENU 让标题栏有关闭按鈕（Alt+F4 可关），不显示最小化/最大化。
    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kClassName, L"拆字",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        100, 100, width_, height_,
        nullptr, nullptr, g_hInst, this);
    return hwnd_ != nullptr;
}

void CPartsWindow::Destroy() {
    if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; }
    if (font_big_)   { DeleteObject(font_big_);   font_big_   = nullptr; }
    if (font_label_) { DeleteObject(font_label_); font_label_ = nullptr; }
}

// ── 显示/隐藏 ────────────────────────────────────────────────────────────
void CPartsWindow::ShowChar(const std::wstring& text) {
    if (!hwnd_ || text.empty()) return;
    wchar_t ch = text[0];

    EnsureLoaded();

    auto it = table_.find(ch);
    if (it == table_.end()) {
        // 字不在表里（标点、数字等），直接隐藏
        Hide();
        return;
    }
    current_    = it->second;
    has_current_ = true;

    RecalcSize();
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, width_, height_,
                 SWP_NOMOVE | SWP_NOACTIVATE);
    InvalidateRect(hwnd_, nullptr, TRUE);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
}

void CPartsWindow::Hide() {
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
    has_current_ = false;
}

bool CPartsWindow::Visible() const {
    return hwnd_ && IsWindowVisible(hwnd_);
}

// ── 懒加载 TSV ────────────────────────────────────────────────────────────
void CPartsWindow::EnsureLoaded() {
    if (loaded_) return;
    loaded_ = true;

    // DLL 在 x64\ 或 x86\ 下，data\ 在它的上一级目录
    // （与 RimeEngine::InitializeRuntime 的路径逻辑一致）
    wchar_t dll_path[MAX_PATH] = {};
    GetModuleFileNameW(g_hInst, dll_path, MAX_PATH);
    std::filesystem::path dll_fs(dll_path);
    std::wstring tsv_path =
        (dll_fs.parent_path().parent_path() / L"data" / kTsvName).wstring();

    // 以 UTF-8 模式打开
    std::ifstream f;
    f.open(tsv_path, std::ios::binary);
    if (!f.is_open()) return;

    std::string line_u8;
    while (std::getline(f, line_u8)) {
        // strip CR
        if (!line_u8.empty() && line_u8.back() == '\r')
            line_u8.pop_back();
        if (line_u8.empty() || line_u8[0] == '#') continue;

        std::wstring line = Utf8ToWide(line_u8);
        auto cols = SplitTab(line);
        // 至少：char + structure + one part column
        if (cols.size() < 3) continue;

        CharParts cp;
        cp.ch        = cols[0].empty() ? 0 : cols[0][0];
        cp.structure = cols[1];
        if (!cp.ch) continue;

        for (size_t i = 2; i < cols.size(); ++i) {
            // 格式：部件|名称列表|字母|拼音
            auto fields = Split(cols[i], L'|');
            if (fields.empty()) continue;
            PartEntry pe;
            pe.glyph   = fields.size() > 0 ? fields[0] : L"";
            pe.names   = fields.size() > 1 ? fields[1] : L"";
            pe.letters = fields.size() > 2 ? fields[2] : L"";
            pe.pinyin  = fields.size() > 3 ? fields[3] : L"";
            if (!pe.glyph.empty())
                cp.parts.push_back(std::move(pe));
        }
        table_[cp.ch] = std::move(cp);
    }
}

// ── 尺寸计算 ────────────────────────────────────────────────────────────
void CPartsWindow::RecalcSize() {
    // 米字格高度 = 3× 标准行高；部件行每行 Scale(28)；结构标签 Scale(22)
    const int cell   = Scale(90);
    const int row_h  = Scale(28);
    const int label_h = Scale(22);
    int parts_count = has_current_ ? static_cast<int>(current_.parts.size()) : 0;
    height_ = cell + label_h + parts_count * row_h + Scale(12);
    width_  = Scale(260);
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

// ── 绘制 ────────────────────────────────────────────────────────────
void CPartsWindow::Paint(HDC dc, const RECT& client) {
    // 背景
    HBRUSH bg = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
    if (bg) { FillRect(dc, &client, bg); DeleteObject(bg); }
    SetBkMode(dc, TRANSPARENT);

    if (!has_current_) return;

    const int pad   = Scale(8);
    const int cellW = Scale(90);
    const int cellH = Scale(90);

    // ── 米字格 ──
    RECT cell_rc = { pad, pad, pad + cellW, pad + cellH };
    DrawMiziGrid(dc, cell_rc);

    // 大字（米字格里）
    if (!font_big_) {
        font_big_ = CreateFontW(
            -Scale(56), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"宋体");
    }
    HFONT old = font_big_
        ? reinterpret_cast<HFONT>(SelectObject(dc, font_big_)) : nullptr;
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    wchar_t ch_str[2] = { current_.ch, 0 };
    DrawTextW(dc, ch_str, 1, &cell_rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (old) SelectObject(dc, old);

    // ── 结构标签（米字格右边）──
    if (!font_label_) {
        font_label_ = CreateFontW(
            -Scale(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微软雅黑");
    }
    old = font_label_
        ? reinterpret_cast<HFONT>(SelectObject(dc, font_label_)) : nullptr;

    const int info_x = pad + cellW + Scale(10);
    int y = pad;

    // 结构行
    {
        std::wstring struct_text = L"结构：";
        struct_text += StructureNameFor(current_.structure);
        RECT r = { info_x, y, client.right - pad, y + Scale(22) };
        SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
        DrawTextW(dc, struct_text.c_str(), -1, &r,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        y += Scale(24);
    }

    // 部件行：每行显示「部件字  名称  拼音  → 字母」
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    for (const auto& p : current_.parts) {
        RECT r = { info_x, y, client.right - pad, y + Scale(26) };

        // 取第一个名称显示
        std::wstring first_name = p.names;
        auto comma = first_name.find(L',');
        if (comma != std::wstring::npos)
            first_name = first_name.substr(0, comma);

        // 字母：大写显示
        std::wstring keys = p.letters;
        for (auto& c : keys) if (c >= L'a' && c <= L'z') c -= 32;

        // 行文：「彳  人旁  rén  → R」
        std::wstring row_text = p.glyph + L"  " + first_name;
        if (!p.pinyin.empty()) row_text += L"  " + p.pinyin;
        if (!keys.empty()) row_text += L"  \u2192  " + keys;

        DrawTextW(dc, row_text.c_str(), -1, &r,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        y += Scale(28);
    }

    if (old) SelectObject(dc, old);
}

void CPartsWindow::DrawMiziGrid(HDC dc, const RECT& r) {
    // 外框
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(180, 180, 200));
    HPEN old = pen ? reinterpret_cast<HPEN>(SelectObject(dc, pen)) : nullptr;

    Rectangle(dc, r.left, r.top, r.right, r.bottom);

    int cx = (r.left + r.right)  / 2;
    int cy = (r.top  + r.bottom) / 2;

    // 横竖中线
    MoveToEx(dc, r.left,  cy, nullptr); LineTo(dc, r.right, cy);
    MoveToEx(dc, cx, r.top,  nullptr); LineTo(dc, cx, r.bottom);

    // 对角线（点线，弱化）
    HPEN pen2 = CreatePen(PS_DOT, 1, RGB(210, 210, 220));
    if (pen2) {
        SelectObject(dc, pen2);
        MoveToEx(dc, r.left, r.top,    nullptr); LineTo(dc, r.right, r.bottom);
        MoveToEx(dc, r.right, r.top,   nullptr); LineTo(dc, r.left,  r.bottom);
        SelectObject(dc, old ? old : reinterpret_cast<HPEN>(GetStockObject(BLACK_PEN)));
        DeleteObject(pen2);
    }

    if (old) SelectObject(dc, old);
    if (pen)  DeleteObject(pen);
}

// ── 消息处理 ────────────────────────────────────────────────────────────
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

    // 标题栏点击/拖动不激活窗口
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;

    // 关闭按鈕 → 隐藏，不销毁
    if (msg == WM_CLOSE) {
        if (self) self->Hide();
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

    // 右键任意位置 → 隐藏
    if (msg == WM_RBUTTONUP && self) {
        self->Hide();
        return 0;
    }

    return DefWindowProcW(hwnd, msg, w, l);
}

}  // namespace zuxia
