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
  // 下限，不是固定宽度。0.2.0 默认 220 —— 九个单字候选的最长一行量出来远
  // 不到 220，于是宽度始终卡在下限，作者实测反馈「键入窗口宽度不会自动变
  // 化，疑似 bug」。算法没错，是下限设得太宽。降到 120 之后窗口才真的跟着
  // 内容走。已经有设置文件的用户，文件里存的还是 220，要自己改这一行。
  int min_width = 120;
  int max_width = 720;
  bool horizontal = false;   // 竖排一列 / 横排一行

  // 背景与正文默认跟随系统主题（深色模式下自动变深）。一旦用户写死了颜色，
  // 就以他写的为准 —— 他比我们更清楚自己屏幕上什么好看。
  bool system_background = true;
  bool system_text = true;
  COLORREF background = RGB(255, 255, 255);
  COLORREF text = RGB(0, 0, 0);
  bool system_dim = true;
  COLORREF dim = RGB(128, 128, 128);           // 编码行
  COLORREF highlight_bg = RGB(35, 104, 190);   // 选中项底色
  COLORREF highlight_fg = RGB(255, 255, 255);  // 选中项文字

  // 任务栏输入指示器上那个字。Windows 11 画的是图标，不是文字 —— 不给图标
  // 它就退回去显示语言缩写「简体」。这两个字会在运行时画成图标。
  std::wstring tray_chinese = L"足";
  std::wstring tray_western = L"A";
};

// 当前设置。第一次调用时读文件；之后每隔至多 500 ms 看一眼文件有没有被改过，
// 改过就重读。按值返回：TSF 是多线程的，交出引用等于交出一个数据竞争。
Appearance CurrentAppearance();

// 设置文件的完整路径。文件不存在时会写一份带中文注释的默认设置，
// 这样用户打开它就知道能改什么，不必去翻文档。
std::wstring SettingsFilePath();

// 下面两个是给设置程序 ZuxiaSettings.exe 用的。输入法本体只用上面那个
// CurrentAppearance()，它带缓存；这两个每次都真的读盘 / 写盘。

// 读一次设置文件。文件不在、读不动、内容是坏的，都返回默认值。
Appearance LoadAppearance();

// 整份重写设置文件，注释一并写回去 —— 设置程序改完之后，用记事本打开
// 仍然要看得懂。写成功返回 true。输入法那边最慢半秒就会看到改动。
bool SaveAppearance(const Appearance& look);

}  // namespace zuxia
