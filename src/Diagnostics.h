#pragma once

// Diagnostic log for the text service.
//
// A text service runs inside someone else's process with no console and no UI
// of its own, so when it fails the user just sees keys doing nothing. This
// writes a short event trail to %LOCALAPPDATA%\Zuxia\zuxia.log instead.
//
// PRIVACY: the log records events, never content. Nothing the user types --
// no code, no candidate, no committed text -- may be passed to these
// functions. The product promises that input does not leave the device and
// that no user text is recorded; this file must not become the exception.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// ZUXIA_COM_GUARD_END catches std::exception by reference, so every file that
// uses the guard needs this whether or not it includes <exception> itself.
#include <exception>
#include <string>

namespace zuxia {

// Appends one line. `detail` is for stable technical values (HRESULTs, error
// codes, file names) -- never user text.
//
// 这些函数是 noexcept，而且内部把一切异常吞掉。原因：ZUXIA_COM_GUARD_END
// 的 catch 体里就调它们。它们自己在低内存时抛出去，防护网就从「拦住异常」
// 变成了「制造异常」—— 从 COM 方法里抛出去等于把宿主程序带走。
// 首选传 const wchar_t*：那条路上一个 std::wstring 临时量都不构造，所以
// 连调用点都不会抛。
void LogEvent(const wchar_t* event, const wchar_t* detail = nullptr) noexcept;
void LogEvent(const wchar_t* event, const std::wstring& detail) noexcept;

// Convenience for reporting a Win32 or COM failure code.
void LogFailure(const wchar_t* event, unsigned long code) noexcept;

// The file LogEvent appends to, or empty when logging is off. Exposed so the
// self-test can check that appending really appends -- writing every line at
// offset zero is a failure mode that leaves the log looking plausible.
std::wstring LogPath();

// Turns logging off for the life of the process. Used when the log file
// cannot be opened, so a broken log never costs more than the log.
void DisableLogging() noexcept;

}  // namespace zuxia

// Guards a COM entry point. Letting a C++ exception escape a COM method is
// undefined behaviour and, in a text service, takes the host application down
// with it. Every ITf* method that can reach the engine is wrapped.
#define ZUXIA_COM_GUARD_BEGIN try {
#define ZUXIA_COM_GUARD_END(event, fallback)                   \
  }                                                             \
  catch (const std::exception&) {                               \
    ::zuxia::LogEvent(L"exception", L"std::exception in " event); \
    return (fallback);                                          \
  }                                                             \
  catch (...) {                                                 \
    ::zuxia::LogEvent(L"exception", L"unknown in " event);     \
    return (fallback);                                          \
  }
