// Shared helpers for the Zuxia setup and uninstall binaries.
// Both are built as 32-bit so a single binary runs on x86 and x64 Windows;
// everything that must touch the 64-bit view does so explicitly.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <objbase.h>
#include <shlobj.h>
#include <string>
#include <vector>

#define ZX_PRODUCT      L"应物音形足下输入法"
#define ZX_PUBLISHER    L"应物音形足下输入法"
#define ZX_VERSION      L"0.4.2"
#define ZX_DIRNAME      L"Zuxia"
#define ZX_TSF_DLL      L"ZuxiaTSF.dll"
#define ZX_ARP_KEY      L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ZuxiaIME"
#define ZX_PRODUCT_KEY  L"SOFTWARE\\Zuxia"
#define ZX_UNINST_EXE   L"ZuxiaUninstall.exe"
#define ZX_SETTINGS_EXE L"ZuxiaSettings.exe"
// 开始菜单里唯一的入口。Windows 11 默认不显示语言栏，没有这个快捷方式，
// 设置程序就等于不存在 —— 用户根本不会知道有它。
#define ZX_SHORTCUT     L"足下输入法设置.lnk"

// {A0073A11-FF52-4185-A655-D0C9171B7850} and
// {699B0EC1-3FDB-415D-89C0-0E55AA2EFFAF}, mirroring src/Globals.cpp.
#define ZX_CLSID   L"{A0073A11-FF52-4185-A655-D0C9171B7850}"
#define ZX_PROFILE L"{699B0EC1-3FDB-415D-89C0-0E55AA2EFFAF}"

namespace zx {

inline bool Is64BitOS() {
  SYSTEM_INFO info = {};
  GetNativeSystemInfo(&info);
  return info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ||
         info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ||
         info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_IA64;
}

inline bool IsArm64OS() {
  SYSTEM_INFO info = {};
  GetNativeSystemInfo(&info);
  return info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64;
}

inline std::wstring Env(const wchar_t* name) {
  wchar_t buffer[MAX_PATH] = {};
  const DWORD length = GetEnvironmentVariableW(name, buffer, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return std::wstring();
  return std::wstring(buffer);
}

// ProgramW6432 is only set on 64-bit Windows and always points at the native
// Program Files, even when this 32-bit process is running under WOW64.
inline std::wstring ProgramFilesRoot() {
  std::wstring root = Env(L"ProgramW6432");
  if (root.empty()) root = Env(L"ProgramFiles");
  if (root.empty()) root = L"C:\\Program Files";
  return root;
}

inline std::wstring DefaultInstallRoot() {
  return ProgramFilesRoot() + L"\\" + ZX_DIRNAME;
}

inline std::wstring WindowsDir() {
  wchar_t buffer[MAX_PATH] = {};
  GetWindowsDirectoryW(buffer, MAX_PATH);
  return std::wstring(buffer);
}

// regsvr32 that drives the 64-bit COM view. Sysnative is the WOW64 escape
// hatch; it does not exist on 32-bit Windows, where there is no 64-bit view.
inline std::wstring Regsvr32For64() {
  if (!Is64BitOS()) return std::wstring();
  return WindowsDir() + L"\\Sysnative\\regsvr32.exe";
}

inline std::wstring Regsvr32For32() {
  return WindowsDir() + (Is64BitOS() ? L"\\SysWOW64\\regsvr32.exe"
                                     : L"\\System32\\regsvr32.exe");
}

inline REGSAM NativeView() { return Is64BitOS() ? KEY_WOW64_64KEY : 0; }

inline bool FileExists(const std::wstring& path) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

inline bool DirExists(const std::wstring& path) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

inline bool EnsureDir(const std::wstring& path) {
  if (DirExists(path)) return true;
  return SHCreateDirectoryExW(nullptr, path.c_str(), nullptr) ==
             ERROR_SUCCESS ||
         DirExists(path);
}

// Lets sandboxed hosts reach the text service.
//
// Edge, every Store app and a growing number of hardened programs run inside
// an AppContainer. Such a process can only load a DLL, or read the Rime data
// beside it, when the file system grants access to the ALL APPLICATION
// PACKAGES group -- Program Files does not by default, so without this the
// input method simply does nothing in those hosts.
//
// The group is addressed by SID rather than by name because its name is
// localised. S-1-15-2-1 is ALL APPLICATION PACKAGES; S-1-15-2-2 is ALL
// RESTRICTED APPLICATION PACKAGES, used by more tightly sandboxed hosts.
inline bool GrantAppContainerAccess(const std::wstring& path, bool writable) {
  static const wchar_t* kSids[] = {L"S-1-15-2-1", L"S-1-15-2-2"};

  PACL current = nullptr;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT,
                            DACL_SECURITY_INFORMATION, nullptr, nullptr,
                            &current, nullptr,
                            &descriptor) != ERROR_SUCCESS) {
    return false;
  }

  EXPLICIT_ACCESSW entries[ARRAYSIZE(kSids)] = {};
  PSID sids[ARRAYSIZE(kSids)] = {};
  DWORD count = 0;
  for (size_t i = 0; i < ARRAYSIZE(kSids); ++i) {
    if (!ConvertStringSidToSidW(kSids[i], &sids[i])) continue;
    entries[count].grfAccessPermissions =
        GENERIC_READ | GENERIC_EXECUTE | (writable ? GENERIC_WRITE : 0);
    entries[count].grfAccessMode = GRANT_ACCESS;
    entries[count].grfInheritance =
        SUB_CONTAINERS_AND_OBJECTS_INHERIT | OBJECT_INHERIT_ACE |
        CONTAINER_INHERIT_ACE;
    entries[count].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    entries[count].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    entries[count].Trustee.ptstrName = static_cast<LPWSTR>(sids[i]);
    ++count;
  }

  bool ok = false;
  PACL updated = nullptr;
  if (count > 0 &&
      SetEntriesInAclW(count, entries, current, &updated) == ERROR_SUCCESS) {
    ok = SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT,
                               DACL_SECURITY_INFORMATION, nullptr, nullptr,
                               updated, nullptr) == ERROR_SUCCESS;
  }
  if (updated) LocalFree(updated);
  for (PSID sid : sids) {
    if (sid) LocalFree(sid);
  }
  if (descriptor) LocalFree(descriptor);
  return ok;
}

inline DWORD RunWait(const std::wstring& exe, const std::wstring& args) {
  std::wstring command = L"\"" + exe + L"\" " + args;
  std::vector<wchar_t> mutable_command(command.begin(), command.end());
  mutable_command.push_back(L'\0');

  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESHOWWINDOW;
  startup.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION process = {};
  if (!CreateProcessW(exe.c_str(), mutable_command.data(), nullptr, nullptr,
                      FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                      &process)) {
    return 0xFFFFFFFF;
  }
  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD code = 0xFFFFFFFF;
  GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return code;
}

// regsvr32 /s exits non-zero on failure; callers decide whether that is fatal.
inline DWORD RegisterDll(const std::wstring& regsvr, const std::wstring& dll,
                         bool unregister) {
  if (regsvr.empty() || !FileExists(regsvr) || !FileExists(dll)) {
    return 0xFFFFFFFF;
  }
  std::wstring args = unregister ? L"/u /s \"" : L"/s \"";
  args += dll;
  args += L"\"";
  return RunWait(regsvr, args);
}

inline bool EndsWithNoCase(const std::wstring& value, const wchar_t* suffix) {
  const size_t length = wcslen(suffix);
  return value.size() >= length &&
         _wcsicmp(value.c_str() + value.size() - length, suffix) == 0;
}

inline bool DeleteTree(const std::wstring& dir) {
  if (!DirExists(dir)) return true;
  const std::wstring search = dir + L"\\*";
  WIN32_FIND_DATAW find = {};
  HANDLE handle = FindFirstFileW(search.c_str(), &find);
  if (handle != INVALID_HANDLE_VALUE) {
    do {
      const std::wstring name = find.cFileName;
      if (name == L"." || name == L"..") continue;
      const std::wstring child = dir + L"\\" + name;
      // A reparse point (junction or symlink) points somewhere else, and
      // this function runs elevated over trees the user can write to.
      // Recursing through one would let any user of this account aim an
      // administrator's recursive delete at an arbitrary directory --
      // %LOCALAPPDATA%\Zuxia\j -> C:\Windows\System32 would do it, and
      // junctions need no privilege. Remove the link itself, never the
      // directory it opens onto.
      if (find.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (!DeleteFileW(child.c_str()) &&
            !RemoveDirectoryW(child.c_str())) {
          MoveFileExW(child.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
        }
        continue;
      }
      if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        DeleteTree(child);
      } else {
        SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (!DeleteFileW(child.c_str())) {
          // A DLL still mapped into a running host cannot be deleted, but it
          // can be renamed out of the way and reaped at the next boot.
          // Something already parked just gets queued again -- renaming it a
          // second time would grow ".old.old" on every install/uninstall pass.
          if (EndsWithNoCase(child, L".old")) {
            MoveFileExW(child.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
          } else {
            const std::wstring parked = child + L".old";
            DeleteFileW(parked.c_str());
            if (MoveFileExW(child.c_str(), parked.c_str(),
                            MOVEFILE_REPLACE_EXISTING)) {
              MoveFileExW(parked.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
            }
          }
        }
      }
    } while (FindNextFileW(handle, &find));
    FindClose(handle);
  }
  if (RemoveDirectoryW(dir.c_str())) return true;
  // Children parked for the next boot keep this directory alive. Queue it as
  // well; the boot-time sweep honours the order entries were added, so the
  // depth-first walk above has already queued everything inside it.
  MoveFileExW(dir.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
  return false;
}

// Overwrites target even when the current file is loaded by a running process.
inline bool ReplaceFile_(const std::wstring& source,
                         const std::wstring& target) {
  if (MoveFileExW(source.c_str(), target.c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
    return true;
  }
  const std::wstring parked = target + L".old";
  DeleteFileW(parked.c_str());
  if (!MoveFileExW(target.c_str(), parked.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    return false;
  }
  MoveFileExW(parked.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
  return MoveFileExW(source.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) != 0;
}

// Deletes HKLM\<path> in the native registry view.
//
// Opening the PARENT with DELETE is not an option: HKLM\SOFTWARE does not
// grant administrators DELETE on itself, so "SOFTWARE\Zuxia" would fail even
// elevated. Instead clear the target's own contents, then delete the target
// by name -- that only needs DELETE on the key we created ourselves.
inline bool RemoveKey(const std::wstring& path) {
  HKEY key = nullptr;
  const REGSAM access = KEY_READ | KEY_WRITE | NativeView();
  const LONG opened =
      RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, access, &key);
  if (opened == ERROR_FILE_NOT_FOUND) return true;
  if (opened != ERROR_SUCCESS) return false;
  RegDeleteTreeW(key, nullptr);  // values and subkeys, but not the key itself
  RegCloseKey(key);

  const LONG deleted = RegDeleteKeyExW(HKEY_LOCAL_MACHINE, path.c_str(),
                                       NativeView(), 0);
  return deleted == ERROR_SUCCESS || deleted == ERROR_FILE_NOT_FOUND;
}

inline std::wstring StartMenuShortcut() {
  PWSTR folder = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_CommonPrograms, 0, nullptr,
                                  &folder))) {
    return std::wstring();
  }
  std::wstring path(folder);
  CoTaskMemFree(folder);
  return path + L"\\" ZX_SHORTCUT;
}

inline std::wstring ReadInstallRoot(const wchar_t* product_key = ZX_PRODUCT_KEY) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, product_key, 0,
                    KEY_READ | NativeView(), &key) != ERROR_SUCCESS) {
    return std::wstring();
  }
  wchar_t buffer[MAX_PATH] = {};
  DWORD bytes = sizeof(buffer);
  DWORD type = 0;
  const LONG result = RegQueryValueExW(key, L"InstallPath", nullptr, &type,
                                       reinterpret_cast<BYTE*>(buffer), &bytes);
  RegCloseKey(key);
  if (result != ERROR_SUCCESS || type != REG_SZ) return std::wstring();
  return std::wstring(buffer);
}

inline std::wstring SelfPath() {
  wchar_t buffer[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  return std::wstring(buffer);
}

// A file name nobody else can predict, for the temporary files an
// elevated installer writes. %TEMP% is chosen by the installing user
// (HKCU\Environment\TMP), so a fixed name there is a handle into the
// elevated process: pre-create it as a hardlink and the write lands on
// an arbitrary file on the volume.
inline std::wstring UniqueTempName(const wchar_t* stem,
                                  const wchar_t* suffix) {
  GUID guid = {};
  if (SUCCEEDED(CoCreateGuid(&guid))) {
    wchar_t text[40] = {};
    if (StringFromGUID2(guid, text, ARRAYSIZE(text)) > 0) {
      // {........-....-....-....-............}: keep the hex, drop braces.
      return std::wstring(stem) + L"-" + std::wstring(text + 1, 36) + suffix;
    }
  }
  return std::wstring(stem) + L"-" + std::to_wstring(GetTickCount()) +
         L"-" + std::to_wstring(GetCurrentProcessId()) + suffix;
}

// Two paths naming the same place, ignoring case and trailing separators.
inline bool SamePath(const std::wstring& a, const std::wstring& b) {
  std::wstring x = a;
  std::wstring y = b;
  while (!x.empty() && (x.back() == L'\\' || x.back() == L'/')) x.pop_back();
  while (!y.empty() && (y.back() == L'\\' || y.back() == L'/')) y.pop_back();
  return _wcsicmp(x.c_str(), y.c_str()) == 0;
}

inline bool DirIsEmpty(const std::wstring& dir) {
  const std::wstring search = dir + L"\\*";
  WIN32_FIND_DATAW find = {};
  HANDLE handle = FindFirstFileW(search.c_str(), &find);
  if (handle == INVALID_HANDLE_VALUE) return true;
  bool empty = true;
  do {
    if (wcscmp(find.cFileName, L".") != 0 &&
        wcscmp(find.cFileName, L"..") != 0) {
      empty = false;
      break;
    }
  } while (FindNextFileW(handle, &find));
  FindClose(handle);
  return empty;
}

// UTF-8, for the error log the silent install path writes.
inline std::string Narrow(const std::wstring& text) {
  if (text.empty()) return std::string();
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                       static_cast<int>(text.size()),
                                       nullptr, 0, nullptr, nullptr);
  if (size <= 0) return std::string();
  std::string out(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                      static_cast<int>(text.size()), out.data(), size,
                      nullptr, nullptr);
  return out;
}

inline std::wstring TempDir() {
  wchar_t buffer[MAX_PATH] = {};
  GetTempPathW(MAX_PATH, buffer);
  return std::wstring(buffer);
}

inline bool IsElevated() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
  TOKEN_ELEVATION elevation = {};
  DWORD size = sizeof(elevation);
  const bool ok =
      GetTokenInformation(token, TokenElevation, &elevation, size, &size) != 0;
  CloseHandle(token);
  return ok && elevation.TokenIsElevated != 0;
}

}  // namespace zx
