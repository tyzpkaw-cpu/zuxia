#pragma once

#include <windows.h>

#include <string>

namespace zuxia {

// 候选窗的外观。全部来自 %LOCALAPPDATA%\Zuxia\设置.txt，用户随时可改，
// 改完下一次按键就生效，不用重启输入法，也不用重新部署词库。
//
// 这里只放「看得见的东西」。候选个数、中英切换键那些属于 librime 的行为，
// 在 data\default.yaml 和 data\zuxia.schema.yaml 里，不在这个文件的管辖范围。
struct Appearance {
  std::wstring font = L"Microsoft YaHei UI";
  int font_size = 16;        // 逻辑像素，会按显示器 DPI 缩放
  int row_height = 30;
  int padding = 8;
  int min_width = 120;
  int max_width = 720;
  bool horizontal = false;   // 竖排一列 / 横排一行

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
};

// 当前设置。第一次调用时读文件；之后每隔至多 500 ms 看一眼文件有没有被改过，
// 改过就重读。按局返回：TSF 是多线程的，交出引用等于交出一个数据竞争。
Appearance CurrentAppearance();

// 设置文件的完整路径。文件不存在时会写一份带中文注释的默认设置，
// 这样用户打开它就知道能改什么，不必去翻文档。
std::wstring SettingsFilePath();

// 下面两个是给设置程序 ZuxiaSettings.exe 用的。输入法本体只用上面那个
// CurrentAppearance()，它带缓存；这两个每次都真的读盘 / 写盘。

// 读一次设置文件。文件不在、读不动、内容是坏的，都返回默认値。
Appearance LoadAppearance();

// 整份重写设置文件，注释一并写回去—— 设置程序改完之后，用记事本打开
// 仍然要看得懂。写成功返回 true。输入法那边最慢半秒就会看到改动。
bool SaveAppearance(const Appearance& look);

}  // namespace zuxia
