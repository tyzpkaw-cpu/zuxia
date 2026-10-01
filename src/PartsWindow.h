#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace zuxia {

// 一个部件的显示信息
struct PartEntry {
    std::wstring glyph;    // 部件字符（单字）
    std::wstring names;    // 名称列表，逗号分隔（如「人旁,双立人」）
    std::wstring letters;  // 可用字母（如「rs」）
    std::wstring pinyin;   // 第一个名称首字的完整带调拼音（如「rén」）
};

// 一个字的拆分记录
struct CharParts {
    wchar_t ch = 0;
    std::wstring structure;          // z/s/b/p/d
    std::vector<PartEntry> parts;
};

// 米字格拆字窗口（任务 7）
// - 不抢焦点（WS_EX_NOACTIVATE），可拖动
// - 选字后自动弹出，显示所选字的拆分；双击标题栏或右键关闭
class CPartsWindow {
 public:
    CPartsWindow();
    ~CPartsWindow();

    // 注册窗口类；在 Activate 时调用一次
    static BOOL InitWindowClass();
    static void UninitWindowClass();

    bool Create();
    void Destroy();

    // 显示指定字的拆分（text 取第一个字符）
    void ShowChar(const std::wstring& text);
    void Hide();
    bool Visible() const;

 private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg,
                                       WPARAM w, LPARAM l);
    static BOOL CALLBACK RegisterClassOnce(PINIT_ONCE, PVOID, PVOID*);

    void Paint(HDC dc, const RECT& client);
    void DrawMiziGrid(HDC dc, const RECT& cell);
    void DrawStructureLabel(HDC dc, int x, int& y, int right);
    void DrawPartRow(HDC dc, int x, int& y, int right, const PartEntry& p,
                     HFONT big, HFONT small);
    void RecalcSize();
    int Scale(int v) const;

    // 懒加载 data/zuxia.parts.tsv
    void EnsureLoaded();
    static std::wstring StructureName(const std::wstring& code);

    static ATOM  atom_;
    static INIT_ONCE init_once_;

    HWND  hwnd_   = nullptr;
    HFONT font_big_   = nullptr;  // 米字格里的大字
    HFONT font_label_ = nullptr;  // 部件行文字

    // 懒加载数据
    bool loaded_ = false;
    std::unordered_map<wchar_t, CharParts> table_;

    // 当前显示的字
    CharParts current_;
    bool has_current_ = false;

    // 拖动
    bool  dragging_   = false;
    POINT drag_start_ = {};
    POINT win_origin_ = {};

    // 窗口尺寸
    int width_  = 260;
    int height_ = 200;
};

}  // namespace zuxia
