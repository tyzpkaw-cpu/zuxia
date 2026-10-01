#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace zuxia {

struct PartEntry {
    std::wstring glyph;    // component character
    std::wstring names;    // comma-separated names (e.g. L"人旁,双立人")
    std::wstring letters;  // usable key letters (e.g. L"rs")
    std::wstring pinyin;   // toned pinyin of first name's head char (e.g. L"ren2")
};

struct CharParts {
    wchar_t ch = 0;
    std::wstring structure;               // z/s/b/p/d
    std::vector<PartEntry> parts;
};

// Floating parts-analysis window (Task 7 / learning mode).
// Shown after each commit; non-focus-stealing; draggable; right-click to hide.
class CPartsWindow {
 public:
    CPartsWindow();
    ~CPartsWindow();

    static BOOL InitWindowClass();
    static void UninitWindowClass();

    bool Create();
    void Destroy();

    // Show decomposition for the first character of |text|.
    void ShowChar(const std::wstring& text);
    void Hide();
    bool Visible() const;

 private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg,
                                       WPARAM w, LPARAM l);
    static BOOL CALLBACK RegisterClassOnce(PINIT_ONCE, PVOID, PVOID*);

    void Paint(HDC dc, const RECT& client);
    void DrawMiziGrid(HDC dc, const RECT& cell);
    void RecalcSize();
    int  Scale(int v) const;
    void EnsureLoaded();

    static ATOM      atom_;
    static INIT_ONCE init_once_;

    HWND  hwnd_       = nullptr;
    HFONT font_big_   = nullptr;
    HFONT font_label_ = nullptr;

    bool loaded_ = false;
    std::unordered_map<wchar_t, CharParts> table_;

    CharParts current_;
    bool has_current_ = false;

    int width_  = 260;
    int height_ = 200;
};

}  // namespace zuxia
