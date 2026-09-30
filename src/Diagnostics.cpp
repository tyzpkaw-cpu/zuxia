#include "Diagnostics.h"

#include <shlobj.h>

#include <atomic>
#include <mutex>
#include <vector>

namespace zuxia {

namespace {

// A text service is loaded into many processes at once, all of them appending
// to the same file, so every write opens, appends and closes under a named
// mutex rather than holding a handle open.
constexpr wchar_t kMutexName[] = L"Local\\ZuxiaIME.Diagnostics";
constexpr long long kMaxBytes = 256 * 1024;
// 到顶之后保留的尾部。整个清空等于在最需要证据的那一刻把证据扔掉。
constexpr long long kKeepBytes = 64 * 1024;
// 连着打不开这么多次才放弃。一次打不开可能只是别人正拿着独占句柄。
constexpr long kOpenFailureLimit = 8;

std::once_flag g_once;
std::wstring g_path;
std::wstring g_host;
// 同一个进程里可能有多个线程在写（TSF 每个输入线程一份），所以是原子的。
std::atomic<bool> g_enabled{true};
std::atomic<long> g_open_failures{0};

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

// A text service is loaded into every application that accepts keyboard
// input, and all of them write to this one file. A bare process id cannot be
// attributed to an application once that process has exited, which is exactly
// when the log is read, so the host's file name goes on every line.
std::wstring HostName() {
  wchar_t path[MAX_PATH] = {};
  const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return std::wstring(L"?");
  const std::wstring full(path, length);
  const size_t slash = full.find_last_of(L'\\');
  return slash == std::wstring::npos ? full : full.substr(slash + 1);
}

void Initialize() {
  g_host = HostName();
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

// Keeps the file from growing without bound across months of use.
//
// It used to call SetEndOfFile at offset zero, which threw the whole log away
// the moment it reached the cap. That is exactly backwards: a text service
// that has been misbehaving for an hour hits the cap *because* it is
// misbehaving, and the run-up to the failure is the part worth reading. So
// the last kKeepBytes are kept and the rest dropped, starting at the first
// line boundary so the file never opens on half a line.
void TrimIfLarge(HANDLE file) {
  LARGE_INTEGER size = {};
  if (!GetFileSizeEx(file, &size) || size.QuadPart < kMaxBytes) return;

  const long long start = size.QuadPart - kKeepBytes;
  std::vector<char> tail(static_cast<size_t>(kKeepBytes));
  LARGE_INTEGER at = {};
  at.QuadPart = start;
  if (!SetFilePointerEx(file, at, nullptr, FILE_BEGIN)) return;
  DWORD read = 0;
  if (!ReadFile(file, tail.data(), static_cast<DWORD>(tail.size()), &read,
                nullptr)) {
    return;
  }

  // 从第一个换行之后开始，免得文件头是半行。找不到换行就整块保留。
  size_t from = 0;
  for (size_t i = 0; i < read; ++i) {
    if (tail[i] == '\n') {
      from = i + 1;
      break;
    }
  }

  LARGE_INTEGER zero = {};
  if (!SetFilePointerEx(file, zero, nullptr, FILE_BEGIN)) return;
  DWORD written = 0;
  if (!WriteFile(file, tail.data() + from,
                 static_cast<DWORD>(read - from), &written, nullptr)) {
    return;
  }
  SetEndOfFile(file);
}

}  // namespace

void DisableLogging() { g_enabled = false; }

std::wstring LogPath() {
  std::call_once(g_once, Initialize);
  return g_enabled ? g_path : std::wstring();
}

void LogEvent(const wchar_t* event, const std::wstring& detail) {
  if (!g_enabled || !event) return;
  std::call_once(g_once, Initialize);
  if (!g_enabled || g_path.empty()) return;

  std::wstring line = Timestamp();
  wchar_t prefix[32] = {};
  swprintf_s(prefix, L" %lu] ", GetCurrentProcessId());
  line += L"  [";
  line += g_host;
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
  bool held = false;
  if (mutex) {
    const DWORD waited = WaitForSingleObject(mutex, 2000);
    // WAIT_ABANDONED 也是拿到了（上一个持有者崩在里面），同样得释放。
    held = waited == WAIT_OBJECT_0 || waited == WAIT_ABANDONED;
  }

  // FILE_WRITE_DATA as well: TrimIfLarge rewrites the tail and calls
  // SetEndOfFile on this handle, which fails without it, so the size cap
  // above never worked.
  HANDLE file = CreateFileW(g_path.c_str(),
                            FILE_APPEND_DATA | FILE_WRITE_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    g_open_failures = 0;
    TrimIfLarge(file);
    // FILE_APPEND_DATA forces writes to the end of the file only when it is
    // the *one* write right requested. FILE_WRITE_DATA is needed for the
    // SetEndOfFile above, and asking for both silently restores ordinary
    // write semantics -- and a freshly opened handle starts at offset zero.
    // Every line therefore landed at offset zero and overwrote the line
    // before it, leaving a file whose length was that of the longest event
    // ever logged and whose contents were fragments of several. Seeking to
    // the end is what makes the append actually append.
    SetFilePointer(file, 0, nullptr, FILE_END);
    DWORD written = 0;
    WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written,
              nullptr);
    CloseHandle(file);
  } else if (g_open_failures.fetch_add(1) + 1 >= kOpenFailureLimit) {
    // 一次打不开多半是别的进程正拿着句柄，不该就此永久闭嘴 —— 那样后面
    // 真出问题时一行日志都没有。连着失败这么多次才认定是 AppContainer
    // 之类根本没有写权限的宿主，然后放弃。
    g_enabled = false;
  }

  if (mutex) {
    if (held) ReleaseMutex(mutex);
    CloseHandle(mutex);
  }
}

void LogFailure(const wchar_t* event, unsigned long code) {
  wchar_t buffer[32] = {};
  swprintf_s(buffer, L"0x%08lX", code);
  LogEvent(event, buffer);
}

}  // namespace zuxia
