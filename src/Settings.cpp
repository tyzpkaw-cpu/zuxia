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
           c == L'\u3000';
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
  if (bytes.empty()) {
    out->clear();
    return false;
  }
  const int needed = MultiByteToWideChar(CP_UTF8, 0, bytes.c_str(),
                                         static_cast<int>(bytes.size()),
                                         nullptr, 0);
  if (needed <= 0) {
    out->clear();
    return false;
  }
  out->assign(static_cast<size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, bytes.c_str(),
                      static_cast<int>(bytes.size()), out->data(), needed);
  return true;
}

bool WriteAll(HANDLE file, const void* data, size_t size) {
  const char* cursor = static_cast<const char*>(data);
  while (size > 0) {
    DWORD written = 0;
    const DWORD chunk =
        static_cast<DWORD>(size > 0x10000000u ? 0x10000000u : size);
    if (!WriteFile(file, cursor, chunk, &written, nullptr)) return false;
    if (written == 0) return false;
    cursor += written;
    size -= written;
  }
  return true;
}

bool WriteBody(HANDLE file, const std::string& bytes) {
  const char bom[3] = {'\xEF', '\xBB', '\xBF'};
  if (!WriteAll(file, bom, 3)) return false;
  if (!bytes.empty() && !WriteAll(file, bytes.data(), bytes.size()))
    return false;
  return FlushFileBuffers(file) != FALSE;
}

bool WriteUtf8(const std::wstring& path, const std::wstring& text,
               bool overwrite) {
  const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                         static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
  if (needed < 0) return false;
  std::string bytes(static_cast<size_t>(needed), '\0');
  if (needed > 0) {
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        bytes.data(), needed, nullptr, nullptr);
  }

  if (!overwrite) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const bool ok = WriteBody(file, bytes);
    CloseHandle(file);
    if (!ok) DeleteFileW(path.c_str());
    return ok;
  }

  const std::wstring temp = path + L".new";
  HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  const bool ok = WriteBody(file, bytes);
  CloseHandle(file);
  if (!ok) {
    DeleteFileW(temp.c_str());
    return false;
  }
  if (!MoveFileExW(temp.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(temp.c_str());
    return false;
  }
  return true;
}

std::wstring Num(int value) { return std::to_wstring(value); }

std::wstring Hex(COLORREF color) {
  static const wchar_t* kDigits = L"0123456789ABCDEF";
  const int parts[3] = {GetRValue(color), GetGValue(color), GetBValue(color)};
  std::wstring out = L"#";
  for (int part : parts) {
    out += kDigits[(part >> 4) & 0xF];
    out += kDigits[part & 0xF];
  }
  return out;
}

std::wstring ColorField(bool follow_system, COLORREF color) {
  return follow_system ? std::wstring(L"\u8ddf\u968f\u7cfb\u7edf") : Hex(color);
}

std::wstring Serialize(const Appearance& look) {
  std::wstring out;
  out += L"# \u5e94\u7269\u97f3\u5f62\u8db3\u4e0b\u8f93\u5165\u6cd5 \u2014\u2014 \u5019\u9009\u7a97\u5916\u89c2\u8bbe\u7f6e\r\n";
  out += L"#\r\n";
  out += L"# \u6539\u5b8c\u4fdd\u5b58\u5373\u53ef\uff0c\u6700\u6162\u534a\u79d2\u751f\u6548\uff0c\u4e0d\u7528\u91cd\u542f\u8f93\u5165\u6cd5\u3002\r\n";
  out += L"# \u4e5f\u53ef\u4ee5\u7528\u5f00\u59cb\u83dc\u5355\u91cc\u7684\u300c\u8db3\u4e0b\u8f93\u5165\u6cd5\u8bbe\u7f6e\u300d\u6539\uff0c\u90a3\u8fb9\u662f\u56fe\u5f62\u754c\u9762\u3002\r\n";
  out += L"# \u4e95\u53f7\u5f00\u5934\u7684\u662f\u8bf4\u660e\uff0c\u5220\u4e0d\u5220\u90fd\u884c\u3002\u5199\u574f\u4e86\u4e0d\u8981\u7d27\uff1a\u8ba4\u4e0d\u51fa\u6765\u7684\u884c\u4f1a\u88ab\u8df3\u8fc7\uff0c\r\n";
  out += L"# \u6574\u4e2a\u6587\u4ef6\u5220\u6389\u5219\u6062\u590d\u9ed8\u8ba4\uff0c\u4e0b\u6b21\u6253\u5b57\u4f1a\u91cd\u65b0\u751f\u6210\u4e00\u4efd\u3002\r\n";
  out += L"#\r\n";
  out += L"# \u989c\u8272\u5199 #RRGGBB\uff08\u7f51\u9875\u90a3\u79cd\u5341\u516d\u8fdb\u5236\uff09\uff0c\u6216\u8005\u5199\u300c\u8ddf\u968f\u7cfb\u7edf\u300d\u3002\r\n";
  out += L"\r\n";
  out += L"\u5b57\u4f53 = " + look.font + L"\r\n";
  out += L"\u5b57\u53f7 = " + Num(look.font_size) + L"\r\n";
  out += L"\r\n";
  out += L"# \u7ad6\u6392 = \u5019\u9009\u4e00\u884c\u4e00\u4e2a\uff1b\u6a2a\u6392 = \u5019\u9009\u6392\u6210\u4e00\u884c\r\n";
  out += L"\u5019\u9009\u6392\u5217 = " + std::wstring(look.horizontal ? L"\u6a2a\u6392" : L"\u7ad6\u6392") + L"\r\n";
  out += L"\r\n";
  out += L"\u884c\u9ad8 = " + Num(look.row_height) + L"\r\n";
  out += L"\u5185\u8fb9\u8ddd = " + Num(look.padding) + L"\r\n";
  out += L"\u6700\u5c0f\u5bbd\u5ea6 = " + Num(look.min_width) + L"\r\n";
  out += L"\u6700\u5927\u5bbd\u5ea6 = " + Num(look.max_width) + L"\r\n";
  out += L"\r\n";
  out += L"\u7a97\u53e3\u80cc\u666f = " + ColorField(look.system_background, look.background) + L"\r\n";
  out += L"\u6b63\u6587\u989c\u8272 = " + ColorField(look.system_text, look.text) + L"\r\n";
  out += L"\u7f16\u7801\u989c\u8272 = " + ColorField(look.system_dim, look.dim) + L"\r\n";
  out += L"\u9009\u4e2d\u5e95\u8272 = " + Hex(look.highlight_bg) + L"\r\n";
  out += L"\u9009\u4e2d\u6587\u5b57 = " + Hex(look.highlight_fg) + L"\r\n";
  out += L"\r\n";
  out += L"# \u4efb\u52a1\u680f\u53f3\u4e0b\u89d2\u90a3\u4e2a\u8f93\u5165\u6307\u793a\u5668\u4e0a\u663e\u793a\u7684\u5b57\uff0c\u4e00\u4e2a\u5b57\u6700\u597d\u770b\u3002\r\n";
  out += L"\u4efb\u52a1\u680f\u56fe\u6807 = " + look.tray_chinese + L"\r\n";
  out += L"\u897f\u6587\u56fe\u6807 = " + look.tray_western + L"\r\n";
  out += L"\r\n";
  out += L"# \u62c6\u5b57\u7a97\u53e3 = \u5f00\u542f\u5b66\u4e60\u6a21\u5f0f\uff08\u5019\u9009\u9ad8\u4eae\u65f6\u5b9e\u65f6\u663e\u793a\u62c6\u5b57\uff09\r\n";
  out += L"\u62c6\u5b57\u7a97\u53e3 = " + std::wstring(look.show_parts_window ? L"\u5f00\u542f" : L"\u5173\u95ed") + L"\r\n";
  out += L"\r\n";
  out += L"# \u5019\u9009\u4e2a\u6570\u3001\u4e2d\u82f1\u5207\u6362\u952e\u8fd9\u4e9b\u4e0d\u5728\u8fd9\u91cc \u2014\u2014 \u5b83\u4eec\u5c5e\u4e8e librime \u7684\u884c\u4e3a\uff0c\r\n";
  out += L"# \u5728\u5b89\u88c5\u76ee\u5f55\u7684 data\\\\default.yaml \u4e0e data\\\\zuxia.schema.yaml \u91cc\u3002\r\n";
  return out;
}

bool ParseColor(const std::wstring& value, COLORREF* out, bool* system) {
  const std::wstring lowered = Lower(value);
  if (Contains(value, L"\u8ddf\u968f") || Contains(value, L"\u7cfb\u7edf") ||
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

std::wstring StripInlineComment(const std::wstring& value) {
  for (size_t i = 1; i < value.size(); ++i) {
    if (value[i] != L'#') continue;
    if (value[i - 1] == L' ' || value[i - 1] == L'\t')
      return value.substr(0, i);
  }
  return value;
}

void ApplyLine(const std::wstring& raw, Appearance* out) {
  const std::wstring line = Trim(raw);
  if (line.empty() || line[0] == L'#' || line[0] == L';') return;
  const size_t split = line.find_first_of(L"=:\uff1a\uff1d");
  if (split == std::wstring::npos) return;
  if (line.substr(0, split).find(L'#') != std::wstring::npos) return;
  const std::wstring key = Trim(line.substr(0, split));
  const std::wstring value =
      Trim(StripInlineComment(Trim(line.substr(split + 1))));
  if (key.empty() || value.empty()) return;
  const std::wstring k = Lower(key);

  if (key == L"\u5b57\u4f53" || k == L"font") {
    out->font = value;
  } else if (key == L"\u5b57\u53f7" || k == L"font_size" || k == L"size") {
    out->font_size = ParseInt(value, out->font_size, 8, 72);
  } else if (key == L"\u5019\u9009\u6392\u5217" || key == L"\u6392\u5217" || k == L"layout") {
    out->horizontal = Contains(value, L"\u6a2a") || Contains(Lower(value), L"horiz");
  } else if (key == L"\u884c\u9ad8" || k == L"row_height") {
    out->row_height = ParseInt(value, out->row_height, 16, 120);
  } else if (key == L"\u5185\u8fb9\u8ddd" || k == L"padding") {
    out->padding = ParseInt(value, out->padding, 0, 40);
  } else if (key == L"\u6700\u5c0f\u5bbd\u5ea6" || k == L"min_width") {
    out->min_width = ParseInt(value, out->min_width, 60, 4000);
  } else if (key == L"\u6700\u5927\u5bbd\u5ea6" || k == L"max_width") {
    out->max_width = ParseInt(value, out->max_width, 120, 8000);
  } else if (key == L"\u7a97\u53e3\u80cc\u666f" || key == L"\u80cc\u666f" || k == L"background") {
    ParseColor(value, &out->background, &out->system_background);
  } else if (key == L"\u6b63\u6587\u989c\u8272" || key == L"\u6587\u5b57\u989c\u8272" || k == L"text_color") {
    ParseColor(value, &out->text, &out->system_text);
  } else if (key == L"\u7f16\u7801\u989c\u8272" || k == L"dim_color") {
    ParseColor(value, &out->dim, &out->system_dim);
  } else if (key == L"\u9009\u4e2d\u5e95\u8272" || k == L"highlight_background") {
    bool ignored = false;
    ParseColor(value, &out->highlight_bg, &ignored);
  } else if (key == L"\u9009\u4e2d\u6587\u5b57" || k == L"highlight_text") {
    bool ignored = false;
    ParseColor(value, &out->highlight_fg, &ignored);
  } else if (key == L"\u4efb\u52a1\u680f\u56fe\u6807" || k == L"tray_icon") {
    if (!value.empty()) out->tray_chinese = value.substr(0, 2);
  } else if (key == L"\u897f\u6587\u56fe\u6807" || k == L"tray_icon_western") {
    if (!value.empty()) out->tray_western = value.substr(0, 2);
  } else if (key == L"\u62c6\u5b57\u7a97\u53e3" || k == L"parts_window" || k == L"show_parts_window") {
    const std::wstring lv = Lower(value);
    out->show_parts_window = Contains(lv, L"\u5f00") || lv == L"on" || lv == L"true" || lv == L"1" || lv == L"yes";
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
  path /= L"\u8bbe\u7f6e.txt";
  return path.wstring();
}

Appearance LoadAppearance() {
  std::wstring text;
  if (!ReadUtf8(SettingsFilePath(), &text)) return Appearance();
  return Parse(text);
}

bool SaveAppearance(const Appearance& look) {
  const bool ok = WriteUtf8(SettingsFilePath(), Serialize(look), true);
  if (ok) {
    std::lock_guard<std::mutex> guard(g_mutex);
    g_loaded = false;
  }
  return ok;
}

Appearance CurrentAppearance() {
  std::lock_guard<std::mutex> guard(g_mutex);
  const DWORD now = GetTickCount();
  if (g_loaded && now - g_checked < kRecheckMs) return g_appearance;
  g_checked = now;

  const std::wstring path = SettingsFilePath();
  WIN32_FILE_ATTRIBUTE_DATA info = {};
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) {
    WriteUtf8(path, Serialize(Appearance()), false);
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
