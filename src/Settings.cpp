#include "Settings.h"

#include <shlobj.h>

#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace zuxia {
namespace {

std::mutex g_mutex;
Appearance g_appearance;
FILETIME g_stamp = {};
DWORD g_checked = 0;
bool g_loaded = false;

// 文件被改过之后最多 500 ms 生效。再勤快就是每次按键都去问一次文件系统，
// 而设置文件一天也改不了几次。
constexpr DWORD kRecheckMs = 500;

std::wstring LocalAppData() {
  wchar_t path[MAX_PATH] = {};
  if (SUCCEEDED(SHGetFolderPathW(nullptr,
                                 CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
                                 nullptr, SHGFP_TYPE_CURRENT, path))) {
    return path;
  }
  return L".";
}

std::wstring Trim(const std::wstring& text) {
  auto blank = [](wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' ||
           c == L'\u3000';  // 全角空格：中文输入法用户很容易打出来
  };
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && blank(text[begin])) ++begin;
  while (end > begin && blank(text[end - 1])) --end;
  return text.substr(begin, end - begin);
}

std::wstring Lower(std::wstring text) {
  for (wchar_t& c : text) c = static_cast<wchar_t>(towlower(c));
  return text;
}

bool Contains(const std::wstring& text, const wchar_t* needle) {
  return text.find(needle) != std::wstring::npos;
}

// 读整个文件并当 UTF-8 解码。记事本默认存 UTF-8 带 BOM，所以 BOM 要吃掉。
bool ReadUtf8(const std::wstring& path, std::wstring* out) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size = {};
  if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
      size.QuadPart > 1 << 20) {
    CloseHandle(file);
    return false;
  }
  std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
  DWORD read = 0;
  const BOOL ok = ReadFile(file, bytes.data(),
                           static_cast<DWORD>(bytes.size()), &read, nullptr);
  CloseHandle(file);
  if (!ok) return false;
  bytes.resize(read);
  if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
      static_cast<unsigned char>(bytes[1]) == 0xBB &&
      static_cast<unsigned char>(bytes[2]) == 0xBF) {
    bytes.erase(0, 3);
  }
  const int needed = MultiByteToWideChar(CP_UTF8, 0, bytes.c_str(),
                                         static_cast<int>(bytes.size()),
                                         nullptr, 0);
  if (needed <= 0) {
    out->clear();
    return bytes.empty();
  }
  out->assign(static_cast<size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, bytes.c_str(),
                      static_cast<int>(bytes.size()), out->data(), needed);
  return true;
}

bool WriteUtf8(const std::wstring& path, const std::wstring& text) {
  const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                         static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
  if (needed < 0) return false;
  std::string bytes(static_cast<size_t>(needed), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                      bytes.data(), needed, nullptr, nullptr);
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                            nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  // 带 BOM 写出去：记事本不看 BOM 就会把中文当 ANSI 读，一开就是乱码。
  const char bom[3] = {'\xEF', '\xBB', '\xBF'};
  DWORD written = 0;
  WriteFile(file, bom, 3, &written, nullptr);
  WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written,
            nullptr);
  CloseHandle(file);
  return true;
}

const wchar_t kDefaultFile[] =
    L"# 应物音形足下输入法 —— 候选窗外观设置\r\n"
    L"#\r\n"
    L"# 改完保存即可，最慢半秒生效，不用重启输入法。\r\n"
    L"# 井号开头的是说明，删不删都行。写坏了不要紧：认不出来的行会被跳过，\r\n"
    L"# 整个文件删掉则恢复默认，下次打字会重新生成一份。\r\n"
    L"#\r\n"
    L"# 颜色写 #RRGGBB（网页那种十六进制），或者写「跟随系统」。\r\n"
    L"\r\n"
    L"字体 = Microsoft YaHei UI\r\n"
    L"字号 = 16\r\n"
    L"\r\n"
    L"# 竖排 = 候选一行一个；横排 = 候选排成一行\r\n"
    L"候选排列 = 竖排\r\n"
    L"\r\n"
    L"行高 = 30\r\n"
    L"内边距 = 8\r\n"
    L"最小宽度 = 220\r\n"
    L"最大宽度 = 720\r\n"
    L"\r\n"
    L"窗口背景 = 跟随系统\r\n"
    L"正文颜色 = 跟随系统\r\n"
    L"编码颜色 = 跟随系统\r\n"
    L"选中底色 = #2368BE\r\n"
    L"选中文字 = #FFFFFF\r\n"
    L"\r\n"
    L"# 候选个数、中英切换键这些不在这里 —— 它们属于 librime 的行为，\r\n"
    L"# 在安装目录的 data\\default.yaml 与 data\\zuxia.schema.yaml 里。\r\n";

bool ParseColor(const std::wstring& value, COLORREF* out, bool* system) {
  const std::wstring lowered = Lower(value);
  if (Contains(value, L"跟随") || Contains(value, L"系统") ||
      lowered == L"system" || lowered == L"auto" || lowered == L"default") {
    *system = true;
    return true;
  }
  std::wstring digits;
  for (wchar_t c : value) {
    if (iswxdigit(c)) digits.push_back(c);
  }
  if (digits.size() != 6) return false;
  const unsigned long packed = wcstoul(digits.c_str(), nullptr, 16);
  *system = false;
  // 文件里写的是 #RRGGBB，Win32 的 COLORREF 是 0x00BBGGRR，要反过来。
  *out = RGB((packed >> 16) & 0xFF, (packed >> 8) & 0xFF, packed & 0xFF);
  return true;
}

int ParseInt(const std::wstring& value, int fallback, int low, int high) {
  wchar_t* end = nullptr;
  const long parsed = wcstol(value.c_str(), &end, 10);
  if (end == value.c_str()) return fallback;
  if (parsed < low) return low;
  if (parsed > high) return high;
  return static_cast<int>(parsed);
}

void ApplyLine(const std::wstring& raw, Appearance* out) {
  std::wstring line = raw;
  const size_t hash = line.find(L'#');
  if (hash != std::wstring::npos) line.erase(hash);
  size_t split = line.find_first_of(L"=:：＝");
  if (split == std::wstring::npos) return;
  const std::wstring key = Trim(line.substr(0, split));
  const std::wstring value = Trim(line.substr(split + 1));
  if (key.empty() || value.empty()) return;
  const std::wstring k = Lower(key);

  if (key == L"字体" || k == L"font") {
    out->font = value;
  } else if (key == L"字号" || k == L"font_size" || k == L"size") {
    out->font_size = ParseInt(value, out->font_size, 8, 72);
  } else if (key == L"候选排列" || key == L"排列" || k == L"layout") {
    out->horizontal = Contains(value, L"横") || Contains(Lower(value), L"horiz");
  } else if (key == L"行高" || k == L"row_height") {
    out->row_height = ParseInt(value, out->row_height, 16, 120);
  } else if (key == L"内边距" || k == L"padding") {
    out->padding = ParseInt(value, out->padding, 0, 40);
  } else if (key == L"最小宽度" || k == L"min_width") {
    out->min_width = ParseInt(value, out->min_width, 60, 4000);
  } else if (key == L"最大宽度" || k == L"max_width") {
    out->max_width = ParseInt(value, out->max_width, 120, 8000);
  } else if (key == L"窗口背景" || key == L"背景" || k == L"background") {
    ParseColor(value, &out->background, &out->system_background);
  } else if (key == L"正文颜色" || key == L"文字颜色" || k == L"text_color") {
    ParseColor(value, &out->text, &out->system_text);
  } else if (key == L"编码颜色" || k == L"dim_color") {
    ParseColor(value, &out->dim, &out->system_dim);
  } else if (key == L"选中底色" || k == L"highlight_background") {
    bool ignored = false;
    ParseColor(value, &out->highlight_bg, &ignored);
  } else if (key == L"选中文字" || k == L"highlight_text") {
    bool ignored = false;
    ParseColor(value, &out->highlight_fg, &ignored);
  }
  if (out->max_width < out->min_width) out->max_width = out->min_width;
}

Appearance Parse(const std::wstring& text) {
  Appearance out;
  size_t begin = 0;
  while (begin <= text.size()) {
    const size_t end = text.find(L'\n', begin);
    const size_t stop = (end == std::wstring::npos) ? text.size() : end;
    ApplyLine(text.substr(begin, stop - begin), &out);
    if (end == std::wstring::npos) break;
    begin = end + 1;
  }
  return out;
}

}  // namespace

std::wstring SettingsFilePath() {
  std::filesystem::path path(LocalAppData());
  path /= L"Zuxia";
  std::error_code error;
  std::filesystem::create_directories(path, error);
  path /= L"设置.txt";
  return path.wstring();
}

Appearance CurrentAppearance() {
  std::lock_guard<std::mutex> guard(g_mutex);
  const DWORD now = GetTickCount();
  if (g_loaded && now - g_checked < kRecheckMs) return g_appearance;
  g_checked = now;

  const std::wstring path = SettingsFilePath();
  WIN32_FILE_ATTRIBUTE_DATA info = {};
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) {
    // 还没有这个文件。写一份默认的，让用户打开就看得见能改什么。
    // 写不出来也无所谓（只读目录、被杀软拦了），默认值照样用。
    WriteUtf8(path, kDefaultFile);
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) {
      g_appearance = Appearance();
      g_loaded = true;
      return g_appearance;
    }
  }
  if (g_loaded && CompareFileTime(&info.ftLastWriteTime, &g_stamp) == 0) {
    return g_appearance;
  }
  g_stamp = info.ftLastWriteTime;

  std::wstring text;
  g_appearance = ReadUtf8(path, &text) ? Parse(text) : Appearance();
  g_loaded = true;
  return g_appearance;
}

}  // namespace zuxia
