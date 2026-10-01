#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "RimeEngine.h"
#include "Settings.h"

class CCandidateWindow {
 public:
  CCandidateWindow();
  ~CCandidateWindow();

  static BOOL InitWindowClass();
  static void UninitWindowClass();

  bool Create();
  void Destroy();
  void Move(int x, int y);
  void Show();
  void Hide();
  bool Visible() const;

  void Update(const std::wstring& preedit,
              const std::vector<zuxia::Candidate>& candidates,
              int highlighted);

 private:
  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w_param,
                                     LPARAM l_param);
  static BOOL CALLBACK RegisterClassOnce(PINIT_ONCE init_once,
                                         PVOID parameter, PVOID* context);
  void Paint();
  void PaintVertical(HDC dc, const RECT& client, int y);
  void PaintHorizontal(HDC dc, const RECT& client, int y);
  void RecalculateSize();
  // 读一次用户设置；字体名或字号变了就重建字体。每次刷新候选都调用，
  // 所以改完设置文件下一次按键就看得见效果。
  void ApplySettings();
  std::wstring RowText(const zuxia::Candidate& candidate) const;
  int Scale(int value) const;
  COLORREF BackgroundColor() const;
  COLORREF TextColor() const;
  COLORREF DimColor() const;

  static ATOM atom_;
  static INIT_ONCE init_once_;
  HWND hwnd_ = nullptr;
  HFONT font_ = nullptr;
  zuxia::Appearance look_;
  std::wstring font_in_use_;
  int font_size_in_use_ = 0;
  std::wstring preedit_;
  std::vector<zuxia::Candidate> candidates_;
  std::vector<int> item_widths_;  // 横排时每个候选占的宽度
  int highlighted_ = 0;
  int width_ = 280;
  // 同一次组字里的最大宽度。窗口跟着内容走，但只长不缩 —— 每按一个键都
  // 重新量一次的话，候选换了宽度就跳一下，看着很晃。组字结束（Hide）清零。
  int session_width_ = 0;
  int height_ = 48;
};
