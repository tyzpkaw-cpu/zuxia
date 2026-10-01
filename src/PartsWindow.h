#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace zuxia {

// One name of a component and the key it gives, e.g. name "shou" -> key S.
struct PartName {
  std::wstring name;
  std::wstring pinyin;  // toned pinyin of the name's first character
  wchar_t letter = 0;   // lower-case key letter, 0 if unknown
};

struct PartEntry {
  std::wstring glyph;
  std::vector<PartName> names;
  // Only filled for an old table without per-name keys: all keys of the
  // component, shown after the names.
  std::wstring letters;
};

struct CharParts {
  std::wstring glyph;     // the character itself (may be a surrogate pair)
  wchar_t structure = 0;  // z s b p d
  std::vector<PartEntry> parts;
};

// Learning-mode window: how the highlighted (or just committed) word breaks
// into components. It is a borderless, non-activating top-level popup with
// its own title strip: clicking, dragging or resizing it never takes the
// focus away from the document, so a composition in progress carries on.
// Resizing changes the zoom; the window always fits its content exactly.
class CPartsWindow {
 public:
  CPartsWindow();
  ~CPartsWindow();
  CPartsWindow(const CPartsWindow&) = delete;
  CPartsWindow& operator=(const CPartsWindow&) = delete;

  static BOOL InitWindowClass();
  static void UninitWindowClass();

  bool Create();
  void Destroy();

  // Shows the components of `text`. Text containing ASCII letters or digits,
  // or no character from the table, leaves the window exactly as it is.
  // After the user closed the window this does nothing until
  // ResetDismissed().
  void ShowWord(const std::wstring& text);
  // Hides the window; the content is kept for the next ShowWord().
  void Hide();
  // Lets ShowWord() bring the window back after the user closed it.
  void ResetDismissed() { dismissed_ = false; }
  bool Visible() const;

 private:
  struct Line {
    std::wstring text;
    int x = 0;
    int y = 0;
    bool dim = false;
  };
  struct Cell {
    RECT box = {};
    std::wstring glyph;
  };
  struct Layout {
    bool vertical = true;
    int width = 0;
    int height = 0;
    int title_height = 0;
    int pad = 0;
    int big_px = 0;
    int label_px = 0;
    int title_px = 0;
    std::wstring title;
    std::vector<Cell> cells;
    std::vector<Line> lines;
    std::vector<RECT> rules;
  };
  enum class Drag { kNone, kMove, kResize, kClose };

  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);
  static BOOL CALLBACK RegisterClassOnce(PINIT_ONCE, PVOID, PVOID*);
  LRESULT OnMessage(UINT msg, WPARAM w, LPARAM l);

  void Dismiss();
  void Relayout();
  void BuildLayout(double scale, bool vertical, Layout* out);
  void Paint(HDC dc);
  void EnsureFonts(int big_px, int label_px, int title_px);
  void DeleteFonts();
  UINT Dpi() const;
  double DpiFactor() const { return Dpi() / 96.0; }
  RECT CloseRect() const;
  int HitEdges(POINT client) const;
  LPCWSTR CursorFor(POINT client) const;
  void BeginDrag(Drag kind, int edges, POINT screen);
  void UpdateDrag(POINT screen);
  void EndDrag(bool keep);
  void DefaultAnchor();
  void AnchorToWindow();
  void LoadPlacement();
  void SavePlacement() const;
  void CheckForeground();

  static ATOM atom_;
  static INIT_ONCE init_once_;

  HWND hwnd_ = nullptr;
  HFONT big_font_ = nullptr;
  HFONT label_font_ = nullptr;
  HFONT title_font_ = nullptr;
  int big_px_ = 0;
  int label_px_ = 0;
  int title_px_ = 0;

  std::vector<const CharParts*> chars_;
  bool truncated_ = false;
  bool vertical_ = true;
  Layout layout_;
  double scale_ = 1.0;

  // zoom_ is what the user asked for; the window may be drawn smaller to fit
  // the work area. The anchor is the corner that stays put when the content
  // changes size: the one nearest the screen edge the window was left at.
  double zoom_ = 1.0;
  bool has_anchor_ = false;
  POINT anchor_ = {};
  bool anchor_right_ = true;
  bool anchor_bottom_ = false;

  bool dismissed_ = false;
  DWORD shown_pid_ = 0;

  Drag drag_ = Drag::kNone;
  int drag_edges_ = 0;
  bool drag_changed_ = false;
  POINT drag_start_ = {};
  RECT drag_rect_ = {};
  double drag_zoom_ = 1.0;
  bool close_hot_ = false;
  bool tracking_mouse_ = false;
};

}  // namespace zuxia
