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

#include <string>

namespace zuxia {

// Appends one line. `detail` is for stable technical values (HRESULTs, error
// codes, file names) -- never user text.
void LogEvent(const wchar_t* event, const std::wstring& detail = std::wstring());

// Convenience for reporting a Win32 or COM failure code.
void LogFailure(const wchar_t* event, unsigned long code);

// Turns logging off for the life of the process. Used when the log file
// cannot be opened, so a broken log never costs more than the log.
void DisableLogging();

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
