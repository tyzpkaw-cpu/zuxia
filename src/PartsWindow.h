#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace zuxia {

struct PartEntry {
    std::wstring glyph;    // component character
    std::wstring names;    // comma-separated names
    std::wstring letters;  // usable key letters
    std::wstring pinyin;   // toned pinyin
};

struct CharParts {
    wchar_t ch = 0;
    std::wstring structure;  // z/s/b/p/d
    std::vector<PartEntry> parts;
};

// Floating parts-analysis window (learning mode).
// Updates in realtime as the highlighted candidate changes.
// Shows one grid per character for multi-char words.
// Non-focus-stealing; draggable; user-resizable; right-click to hide.
class CPartsWindow {
 public:
    CPartsWindow();
    ~CPartsWindow();

    static BOOL InitWindowClass();
    static void UninitWindowClass();

    bool Create();
    void Destroy();

    // Show decomposition for all characters in |text| (one grid each).
    // Pass empty string to hide.
    void ShowWord(const std::wstring& text);
    void Hide();
    bool Visible() const;

 private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg,
                                       WPARAM w, LPARAM l);
    static BOOL CALLBACK RegisterClassOnce(PINIT_ONCE, PVOID, PVOID*);

    void Paint(HDC dc, const RECT& client);
    void DrawCharGrid(HDC dc, const RECT& cell_rc, const CharParts& cp);
    void DrawMiziGrid(HDC dc, const RECT& r);
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

    std::vector<CharParts> current_chars_;  // one per character in word
    std::wstring current_word_;

    int width_  = 280;
    int height_ = 200;
};

}  // namespace zuxia
