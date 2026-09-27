// Cabinet extraction shared by the installer and its test harness, so the
// tested code path is the shipped one.
#pragma once

#include "zxcommon.h"

#include <setupapi.h>
#include <string>

namespace zx {

struct ExtractContext {
  std::wstring root;         // destination directory
  bool skip_x64 = false;     // drop the x64 payload on 32-bit Windows
  unsigned long long bytes = 0;
  int files = 0;
  void (*on_progress)(unsigned long long bytes) = nullptr;
};

inline bool StartsWithNoCase(const std::wstring& value, const wchar_t* prefix) {
  const size_t length = wcslen(prefix);
  return value.size() >= length &&
         _wcsnicmp(value.c_str(), prefix, length) == 0;
}

inline UINT CALLBACK CabCallback(PVOID context, UINT notification,
                                 UINT_PTR param1, UINT_PTR param2) {
  UNREFERENCED_PARAMETER(param2);
  auto* state = static_cast<ExtractContext*>(context);
  switch (notification) {
    case SPFILENOTIFY_FILEINCABINET: {
      auto* info = reinterpret_cast<FILE_IN_CABINET_INFO_W*>(param1);
      const std::wstring name = info->NameInCabinet;
      if (state->skip_x64 && StartsWithNoCase(name, L"x64\\")) {
        return FILEOP_SKIP;
      }
      const std::wstring target = state->root + L"\\" + name;
      const size_t slash = target.find_last_of(L'\\');
      if (slash != std::wstring::npos &&
          !EnsureDir(target.substr(0, slash))) {
        return FILEOP_ABORT;
      }
      wcscpy_s(info->FullTargetName, MAX_PATH, target.c_str());
      return FILEOP_DOIT;
    }
    case SPFILENOTIFY_FILEEXTRACTED: {
      auto* paths = reinterpret_cast<FILEPATHS_W*>(param1);
      if (paths->Win32Error != NO_ERROR) {
        return static_cast<UINT>(paths->Win32Error);
      }
      WIN32_FILE_ATTRIBUTE_DATA attributes = {};
      if (GetFileAttributesExW(paths->Target, GetFileExInfoStandard,
                               &attributes)) {
        state->bytes +=
            (static_cast<unsigned long long>(attributes.nFileSizeHigh) << 32) +
            attributes.nFileSizeLow;
      }
      ++state->files;
      if (state->on_progress) state->on_progress(state->bytes);
      return NO_ERROR;
    }
    case SPFILENOTIFY_NEEDNEWCABINET:
      return NO_ERROR;
    default:
      return NO_ERROR;
  }
}

inline bool ExtractCabinet(const std::wstring& cab, ExtractContext* state,
                           DWORD* error) {
  if (!SetupIterateCabinetW(cab.c_str(), 0, CabCallback, state)) {
    if (error) *error = GetLastError();
    return false;
  }
  return true;
}

}  // namespace zx
