#pragma once

#include <windows.h>

#include <string>

namespace zuxia {

struct Appearance {
  std::wstring font = L"Microsoft YaHei UI";
  int font_size = 16;
  int row_height = 30;
  int padding = 8;
  int min_width = 120;
  int max_width = 720;
  bool horizontal = false;

  bool system_background = true;
  bool system_text = true;
  COLORREF background = RGB(255, 255, 255);
  COLORREF text = RGB(0, 0, 0);
  bool system_dim = true;
  COLORREF dim = RGB(128, 128, 128);
  COLORREF highlight_bg = RGB(35, 104, 190);
  COLORREF highlight_fg = RGB(255, 255, 255);

  std::wstring tray_chinese = L"\u8db3";
  std::wstring tray_western = L"A";

  // 拆字窗口（学习模式）。候选高亮时实时显示拆字；默认关闭。
  bool show_parts_window = false;
  // true = 竖排（每字一行，格左信息右）；false = 横排（多字并排）。默认竖排。
  bool parts_vertical = true;
};

Appearance CurrentAppearance();
std::wstring SettingsFilePath();
Appearance LoadAppearance();
bool SaveAppearance(const Appearance& look);

}  // namespace zuxia
