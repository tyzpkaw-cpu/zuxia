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
  int min_width = 220;
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
};

// 当前设置。第一次调用时读文件；之后每隔至多 500 ms 看一眼文件有没有被改过，
// 改过就重读。按值返回：TSF 是多线程的，交出引用等于交出一个数据竞争。
Appearance CurrentAppearance();

// 设置文件的完整路径。文件不存在时会写一份带中文注释的默认设置，
// 这样用户打开它就知道能改什么，不必去翻文档。
std::wstring SettingsFilePath();

}  // namespace zuxia
