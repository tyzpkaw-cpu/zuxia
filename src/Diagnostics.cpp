#include "Diagnostics.h"

#include <shlobj.h>

#include <mutex>

namespace zuxia {

namespace {

// A text service is loaded into many processes at once, all of them appending
// to the same file, so every write opens, appends and closes under a named
// mutex rather than holding a handle open.
constexpr wchar_t kMutexName[] = L"Local\\ZuxiaIME.Diagnostics";
constexpr long long kMaxBytes = 256 * 1024;

std::once_flag g_once;
std::wstring g_path;
bool g_enabled = true;

// Matches RimeEngine::LocalAppDataDirectory so the log sits beside the Rime
// user directory it describes.
std::wstring LocalAppData() {
  wchar_t path[MAX_PATH] = {};
  if (SUCCEEDED(SHGetFolderPathW(nullptr,
                                 CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
                                 nullptr, SHGFP_TYPE_CURRENT, path))) {
    return std::wstring(path);
  }
  return std::wstring();
}

void Initialize() {
  const std::wstring local = LocalAppData();
  if (local.empty()) {
    g_enabled = false;
    return;
  }
  const std::wstring dir = local + L"\\Zuxia";
  CreateDirectoryW(dir.c_str(), nullptr);  // ok if it already exists
  g_path = dir + L"\\zuxia.log";
}

std::wstring Timestamp() {
  SYSTEMTIME now = {};
  GetLocalTime(&now);
  wchar_t buffer[32] = {};
  swprintf_s(buffer, L"%04u-%02u-%02u %02u:%02u:%02u.%03u", now.wYear,
             now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
             now.wMilliseconds);
  return std::wstring(buffer);
}

std::string Narrow(const std::wstring& text) {
  if (text.empty()) return std::string();
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                       static_cast<int>(text.size()), nullptr,
                                       0, nullptr, nullptr);
  if (size <= 0) return std::string();
  std::string out(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                      out.data(), size, nullptr, nullptr);
  return out;
}

// Keeps the file from growing without bound across months of use. Truncating
// rather than rotating is deliberate: the log is for diagnosing a failure that
// is happening now, not for history.
void TruncateIfLarge(HANDLE file) {
  LARGE_INTEGER size = {};
  if (!GetFileSizeEx(file, &size) || size.QuadPart < kMaxBytes) return;
  SetFilePointer(file, 0, nullptr, FILE_BEGIN);
  SetEndOfFile(file);
}

}  // namespace

void DisableLogging() { g_enabled = false; }

void LogEvent(const wchar_t* event, const std::wstring& detail) {
  if (!g_enabled || !event) return;
  std::call_once(g_once, Initialize);
  if (!g_enabled || g_path.empty()) return;

  std::wstring line = Timestamp();
  wchar_t prefix[32] = {};
  swprintf_s(prefix, L"  [%lu] ", GetCurrentProcessId());
  line += prefix;
  line += event;
  if (!detail.empty()) {
    line += L": ";
    line += detail;
  }
  line += L"\r\n";
  const std::string bytes = Narrow(line);
  if (bytes.empty()) return;

  HANDLE mutex = CreateMutexW(nullptr, FALSE, kMutexName);
  if (mutex) WaitForSingleObject(mutex, 2000);

  HANDLE file = CreateFileW(g_path.c_str(), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    TruncateIfLarge(file);
    DWORD written = 0;
    WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written,
              nullptr);
    CloseHandle(file);
  } else {
    // Most likely an AppContainer host with no write access. Stop trying.
    g_enabled = false;
  }

  if (mutex) {
    ReleaseMutex(mutex);
    CloseHandle(mutex);
  }
}

void LogFailure(const wchar_t* event, unsigned long code) {
  wchar_t buffer[32] = {};
  swprintf_s(buffer, L"0x%08lX", code);
  LogEvent(event, buffer);
}

}  // namespace zuxia
