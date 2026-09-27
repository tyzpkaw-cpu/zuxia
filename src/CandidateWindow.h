#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "RimeEngine.h"

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
  void RecalculateSize();
  int Scale(int value) const;

  static ATOM atom_;
  static INIT_ONCE init_once_;
  HWND hwnd_ = nullptr;
  HFONT font_ = nullptr;
  std::wstring preedit_;
  std::vector<zuxia::Candidate> candidates_;
  int highlighted_ = 0;
  int width_ = 280;
  int height_ = 48;
};
