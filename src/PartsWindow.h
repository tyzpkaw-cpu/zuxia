#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace zuxia {

struct PartEntry {
    std::wstring glyph;
    std::wstring names;
    std::wstring letters;
    std::wstring pinyin;
};

struct CharParts {
    wchar_t ch = 0;
    std::wstring structure;
    std::vector<PartEntry> parts;
};

class CPartsWindow {
 public:
    CPartsWindow();
    ~CPartsWindow();

    static BOOL InitWindowClass();
    static void UninitWindowClass();

    bool Create();
    void Destroy();

    void ShowWord(const std::wstring& text);
    void Hide();
    bool Visible() const;

 private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);
    static BOOL CALLBACK RegisterClassOnce(PINIT_ONCE, PVOID, PVOID*);

    void Paint(HDC dc, const RECT& client);
    void DrawCharGridHoriz(HDC dc, const RECT& col_rc, const CharParts& cp);
    void DrawMiziGrid(HDC dc, const RECT& r);
    void EnsureFonts();
    void RecalcSize();
    int  Scale(int v) const;
    void EnsureLoaded();

    static ATOM      atom_;
    static INIT_ONCE init_once_;

    HWND  hwnd_       = nullptr;
    HFONT font_big_   = nullptr;
    HFONT font_label_ = nullptr;

    bool loaded_        = false;
    bool user_resized_  = false;
    int  last_client_w_ = 0;
    int  last_client_h_ = 0;

    std::unordered_map<wchar_t, CharParts> table_;
    std::vector<CharParts> current_chars_;
    std::wstring current_word_;

    int width_  = 320;
    int height_ = 200;
};

}  // namespace zuxia
