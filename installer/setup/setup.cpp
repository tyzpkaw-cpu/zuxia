// Zuxia IME setup: a self-contained 32-bit installer that carries the
// x64 and x86 text services plus the Rime data as an embedded cabinet.
#include "zxcommon.h"

#include "cabextract.h"

#include <commctrl.h>
#include <shellapi.h>
#include <string>

#include "resource.h"
#include "payload_info.h"

namespace {

constexpr UINT kMsgProgress = WM_APP + 1;  // wParam = percent
constexpr UINT kMsgStatus = WM_APP + 2;    // lParam = heap wchar_t*
constexpr UINT kMsgDone = WM_APP + 3;      // wParam = ok, lParam = heap text

HINSTANCE g_instance = nullptr;
HWND g_dialog = nullptr;
HFONT g_head_font = nullptr;
std::wstring g_install_root;
bool g_silent = false;
volatile LONG g_busy = 0;

void Post(UINT message, WPARAM w, const std::wstring& text) {
  wchar_t* copy = nullptr;
  if (!text.empty()) {
    copy = new wchar_t[text.size() + 1];
    wcscpy_s(copy, text.size() + 1, text.c_str());
  }
  if (g_dialog) {
    PostMessageW(g_dialog, message, w, reinterpret_cast<LPARAM>(copy));
  } else {
    delete[] copy;
  }
}

void Status(const std::wstring& text) { Post(kMsgStatus, 0, text); }
void Progress(int percent) { Post(kMsgProgress, percent, std::wstring()); }

// ---------------------------------------------------------------- cabinet --

// Extraction fills 10%-75% of the bar; zx::ExtractCabinet does the work.
void OnExtractProgress(unsigned long long bytes) {
  static int last = -1;
  int percent = 10 + static_cast<int>((bytes * 65ULL) / ZX_PAYLOAD_BYTES);
  if (percent > 75) percent = 75;
  if (percent != last) {
    last = percent;
    Progress(percent);
  }
}

bool WriteResourceToFile(int resource_id, const std::wstring& path) {
  HRSRC found =
      FindResourceW(g_instance, MAKEINTRESOURCEW(resource_id), RT_RCDATA);
  if (!found) return false;
  HGLOBAL loaded = LoadResource(g_instance, found);
  if (!loaded) return false;
  const void* data = LockResource(loaded);
  const DWORD size = SizeofResource(g_instance, found);
  if (!data || size == 0) return false;

  // CREATE_NEW: refuse to adopt a file that is already there.
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const bool ok =
      WriteFile(file, data, size, &written, nullptr) != 0 && written == size;
  CloseHandle(file);
  if (!ok) DeleteFileW(path.c_str());
  return ok;
}

// --------------------------------------------------------------- install --

void UnregisterAt(const std::wstring& root) {
  const std::wstring x86 = root + L"\\x86\\" ZX_TSF_DLL;
  const std::wstring x64 = root + L"\\x64\\" ZX_TSF_DLL;
  if (zx::FileExists(x86)) zx::RegisterDll(zx::Regsvr32For32(), x86, true);
  if (zx::FileExists(x64)) zx::RegisterDll(zx::Regsvr32For64(), x64, true);
  // On ARM64 the keys were written directly (see PublishComRegistration),
  // and no regsvr32 there can undo them.
  if (zx::IsArm64OS()) {
    zx::RemoveKey(L"SOFTWARE\\Classes\\CLSID\\" ZX_CLSID);
    zx::RemoveKey(L"SOFTWARE\\Microsoft\\CTF\\TIP\\" ZX_CLSID);
  }
}

// Moves the verified staging tree over the live tree, one file at a time so a
// DLL that is still mapped into a running host can be parked and replaced.
bool PromoteTree(const std::wstring& staging, const std::wstring& target,
                 std::wstring* error) {
  const std::wstring search = staging + L"\\*";
  WIN32_FIND_DATAW find = {};
  HANDLE handle = FindFirstFileW(search.c_str(), &find);
  if (handle == INVALID_HANDLE_VALUE) {
    // Nothing to promote. Saying true here once reported 安装完成 for an
    // install that moved no files at all.
    *error = L"暂存目录为空：" + staging;
    return false;
  }
  bool ok = true;
  do {
    const std::wstring name = find.cFileName;
    if (name == L"." || name == L"..") continue;
    const std::wstring from = staging + L"\\" + name;
    const std::wstring to = target + L"\\" + name;
    if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (!zx::EnsureDir(to) || !PromoteTree(from, to, error)) ok = false;
    } else if (!zx::ReplaceFile_(from, to)) {
      *error = L"无法写入文件：" + to;
      ok = false;
    }
  } while (ok && FindNextFileW(handle, &find));
  FindClose(handle);
  return ok;
}

// librime compiles the dictionaries into this directory on first use. An
// AppContainer host cannot create it and cannot write there unless it is
// granted access, which would leave Edge and Store apps recompiling (or
// failing) on every launch. Creating it for the installing user up front
// covers the common single-user case; other users get it created by the text
// service the first time they type in a non-sandboxed application.
void PrepareUserDataDir() {
  const std::wstring local = zx::Env(L"LOCALAPPDATA");
  if (local.empty()) return;
  const std::wstring dir = local + L"\\" ZX_DIRNAME L"\\Rime";
  if (!zx::EnsureDir(dir)) return;
  zx::GrantAppContainerAccess(local + L"\\" ZX_DIRNAME, /*writable=*/true);
}

void SetString(HKEY target, const wchar_t* name, const std::wstring& value) {
  RegSetValueExW(target, name, 0, REG_SZ,
                 reinterpret_cast<const BYTE*>(value.c_str()),
                 static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

bool WriteRegistry(const std::wstring& root, std::wstring* error) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, ZX_PRODUCT_KEY, 0, nullptr, 0,
                      KEY_WRITE | zx::NativeView(), nullptr, &key,
                      nullptr) != ERROR_SUCCESS) {
    *error = L"无法写入注册表。";
    return false;
  }
  SetString(key, L"InstallPath", root);
  SetString(key, L"Version", ZX_VERSION);
  // The scope an ARM64 install actually has. Written so that support and
  // scripts\verify-install.ps1 can distinguish "works in emulated hosts
  // only" from "registration failed".
  if (zx::IsArm64OS()) {
    SetString(key, L"HostScope", L"arm64-emulated-hosts-only");
  }
  RegCloseKey(key);

  if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, ZX_ARP_KEY, 0, nullptr, 0,
                      KEY_WRITE | zx::NativeView(), nullptr, &key,
                      nullptr) != ERROR_SUCCESS) {
    *error = L"无法写入卸载信息。";
    return false;
  }
  const std::wstring uninstaller = root + L"\\" ZX_UNINST_EXE;
  SetString(key, L"DisplayName", ZX_PRODUCT);
  SetString(key, L"DisplayVersion", ZX_VERSION);
  SetString(key, L"Publisher", ZX_PUBLISHER);
  SetString(key, L"InstallLocation", root);
  SetString(key, L"UninstallString", L"\"" + uninstaller + L"\"");
  SetString(key, L"QuietUninstallString", L"\"" + uninstaller + L"\" /silent");
  SetString(key, L"DisplayIcon", uninstaller + L",0");
  DWORD one = 1;
  RegSetValueExW(key, L"NoModify", 0, REG_DWORD,
                 reinterpret_cast<const BYTE*>(&one), sizeof(one));
  RegSetValueExW(key, L"NoRepair", 0, REG_DWORD,
                 reinterpret_cast<const BYTE*>(&one), sizeof(one));
  DWORD estimate = static_cast<DWORD>(ZX_PAYLOAD_BYTES / 1024);
  RegSetValueExW(key, L"EstimatedSize", 0, REG_DWORD,
                 reinterpret_cast<const BYTE*>(&estimate), sizeof(estimate));
  RegCloseKey(key);
  return true;
}

// ------------------------------------------------- ARM64 注册 --
//
// On ARM64 the only 64-bit regsvr32 a 32-bit process can reach is the
// ARM64 one, and an ARM64 process cannot load an x64 DLL -- so
// DllRegisterServer never runs for the x64 text service and neither its
// CLSID nor its TSF language profile would be published. Both can be
// done from here instead:
//
//   * the 64-bit registry view is shared by ARM64, ARM64EC and
//     x64-emulated processes (only the 32-bit x86 view is redirected),
//     so a CLSID written with KEY_WOW64_64KEY is exactly what an
//     x64-emulated host resolves;
//   * the TSF profile store HKLM\SOFTWARE\Microsoft\CTF\TIP is Shared,
//     so one registration covers every architecture.
//
// ARM64-native hosts still cannot use this product: a process can only
// load a DLL of its own architecture, and the x64 text service in the
// 64-bit view is not an ARM64X image. See docs/审计发现.md.
#include <msctf.h>

bool PublishComRegistration(const std::wstring& module) {
  const std::wstring path =
      std::wstring(L"SOFTWARE\\Classes\\CLSID\\") + ZX_CLSID;
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, nullptr,
                      REG_OPTION_NON_VOLATILE, KEY_WRITE | KEY_WOW64_64KEY,
                      nullptr, &key, nullptr) != ERROR_SUCCESS) {
    return false;
  }
  SetString(key, nullptr, ZX_PRODUCT);
  HKEY inproc = nullptr;
  const bool ok =
      RegCreateKeyExW(key, L"InProcServer32", 0, nullptr,
                      REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &inproc,
                      nullptr) == ERROR_SUCCESS;
  if (ok) {
    SetString(inproc, nullptr, module);
    SetString(inproc, L"ThreadingModel", L"Apartment");
    RegCloseKey(inproc);
  }
  RegCloseKey(key);
  return ok;
}

bool PublishLanguageProfile(const std::wstring& module) {
  const HRESULT started = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  ITfInputProcessorProfiles* profiles = nullptr;
  bool ok = false;
  if (SUCCEEDED(CoCreateInstance(
          CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
          IID_ITfInputProcessorProfiles,
          reinterpret_cast<void**>(&profiles))) &&
      profiles) {
    CLSID clsid = {};
    GUID profile = {};
    if (SUCCEEDED(CLSIDFromString(ZX_CLSID, &clsid)) &&
        SUCCEEDED(CLSIDFromString(ZX_PROFILE, &profile))) {
      const LANGID langid = 0x0804;  // zh-CN, matching src/Globals.h
      profiles->Register(clsid);
      profiles->RemoveLanguageProfile(clsid, langid, profile);
      if (SUCCEEDED(profiles->AddLanguageProfile(
              clsid, langid, profile, ZX_PRODUCT,
              static_cast<ULONG>(wcslen(ZX_PRODUCT)), module.c_str(),
              static_cast<ULONG>(module.size()), 0))) {
        profiles->EnableLanguageProfile(clsid, langid, profile, TRUE);
        ok = true;
      }
    }
    profiles->Release();
  }
  if (SUCCEEDED(started)) CoUninitialize();
  return ok;
}

bool PublishArm64Registration(const std::wstring& x64_module) {
  return PublishComRegistration(x64_module) &&
         PublishLanguageProfile(x64_module);
}

bool DoInstall(std::wstring* error) {
  const std::wstring root = g_install_root;
  const std::wstring staging = root + L".new";
  // An unpredictable name: a fixed one in %TEMP% can be pre-created as a
  // hardlink to any file on the volume, and this process is elevated.
  const std::wstring cab =
      zx::TempDir() + zx::UniqueTempName(L"zuxia-payload", L".cab");

  Status(L"正在准备安装文件…");
  Progress(2);
  if (!WriteResourceToFile(IDR_PAYLOAD, cab)) {
    *error = L"无法释放安装载荷。";
    return false;
  }

  zx::DeleteTree(staging);
  if (!zx::EnsureDir(staging)) {
    DeleteFileW(cab.c_str());
    *error = L"无法创建暂存目录：" + staging;
    return false;
  }

  Status(L"正在解压程序文件…");
  Progress(10);
  zx::ExtractContext context;
  context.root = staging;
  context.skip_x64 = !zx::Is64BitOS();
  context.on_progress = OnExtractProgress;
  DWORD code = 0;
  if (!zx::ExtractCabinet(cab, &context, &code)) {
    DeleteFileW(cab.c_str());
    zx::DeleteTree(staging);
    wchar_t buffer[64] = {};
    swprintf_s(buffer, L"（错误码 %lu）", code);
    *error = std::wstring(L"解压安装载荷失败") +
             buffer;
    return false;
  }
  DeleteFileW(cab.c_str());

  if (!WriteResourceToFile(IDR_UNINSTALLER, staging + L"\\" ZX_UNINST_EXE)) {
    zx::DeleteTree(staging);
    *error = L"无法写入卸载程序。";
    return false;
  }

  // Nothing on the live tree has been touched yet, so any failure up to here
  // leaves the previous installation intact.
  Status(L"正在校验文件…");
  Progress(76);
  const wchar_t* required[] = {L"\\data\\zuxia.schema.yaml",
                               L"\\data\\zuxia.dict.yaml",
                               L"\\data\\zuxia_char_codes.dict.yaml",
                               L"\\x86\\" ZX_TSF_DLL, L"\\x86\\rime.dll"};
  for (const wchar_t* relative : required) {
    if (!zx::FileExists(staging + relative)) {
      zx::DeleteTree(staging);
      *error = std::wstring(
                   L"安装载荷不完整，缺少 ") +
               relative;
      return false;
    }
  }
  if (zx::Is64BitOS() && !zx::FileExists(staging + L"\\x64\\" ZX_TSF_DLL)) {
    zx::DeleteTree(staging);
    *error =
        L"安装载荷不完整，缺少 x64\\" ZX_TSF_DLL;
    return false;
  }

  Status(L"正在注销旧版本…");
  Progress(80);
  const std::wstring previous = zx::ReadInstallRoot();
  if (!previous.empty() && _wcsicmp(previous.c_str(), root.c_str()) != 0) {
    UnregisterAt(previous);
  }

  Status(L"正在写入程序文件…");
  Progress(84);
  if (!zx::EnsureDir(root)) {
    zx::DeleteTree(staging);
    *error = L"无法创建安装目录：" + root;
    return false;
  }
  if (!PromoteTree(staging, root, error)) return false;
  zx::DeleteTree(staging);

  // Must happen before registration: a sandboxed host that cannot read the
  // install directory cannot load the text service at all.
  Status(L"正在授予沙盒应用访问权限…");
  Progress(88);
  zx::GrantAppContainerAccess(root, /*writable=*/false);
  PrepareUserDataDir();

  Status(L"正在注册输入法…");
  Progress(90);
  // x86 first, x64 last, so the shared profile icon resolves to the native
  // text service on a 64-bit system.
  const std::wstring x86 = root + L"\\x86\\" ZX_TSF_DLL;
  const std::wstring x64 = root + L"\\x64\\" ZX_TSF_DLL;
  if (zx::FileExists(x86) &&
      zx::RegisterDll(zx::Regsvr32For32(), x86, false) != 0) {
    *error = L"注册 32 位输入法失败。";
    UnregisterAt(root);
    return false;
  }
  if (zx::Is64BitOS() && zx::IsArm64OS()) {
    // No 64-bit regsvr32 reachable from here can load an x64 DLL, so this
    // is not a failure of the install: publish the CLSID and the profile
    // directly, which is what the hosts that *can* load it look up.
    if (!PublishArm64Registration(x64)) {
      *error = L"注册 64 位输入法失败。";
      UnregisterAt(root);
      return false;
    }
  } else if (zx::Is64BitOS() &&
             zx::RegisterDll(zx::Regsvr32For64(), x64, false) != 0) {
    *error = L"注册 64 位输入法失败。";
    UnregisterAt(root);
    return false;
  }

  Status(L"正在写入注册信息…");
  Progress(96);
  if (!WriteRegistry(root, error)) {
    UnregisterAt(root);
    return false;
  }

  Progress(100);
  return true;
}

DWORD WINAPI InstallThread(LPVOID) {
  std::wstring error;
  if (DoInstall(&error)) {
    std::wstring message =
        L"安装完成。按 Win+空格 "
        L"选择「应物音形足下输入法」。\n"
        L"首次切换时会编译词库，"
        L"请稍候几秒。";
    if (zx::IsArm64OS()) {
      message +=
          L"\n\n注意：本机是 Windows on ARM（ARM64）。"
          L"文本服务只有 x64 与 x86 版本，因此只在"
          L"以 x86 / x64 / Arm64EC 模式运行的应用里"
          L"可用；ARM64 原生应用（Edge ARM64、"
          L"记事本、资源管理器等）里无法输入。\n"
          L"原因：TSF 把输入法 DLL 载入宿主进程，"
          L"而进程只能载入与本进程同架构的 DLL。";
    }
    Post(kMsgDone, 1, message);
  } else {
    Post(kMsgDone, 0, error);
  }
  return 0;
}

// ------------------------------------------------------------------- UI --

void RefreshButtons() {
  const bool installed = !zx::ReadInstallRoot().empty();
  EnableWindow(GetDlgItem(g_dialog, IDC_UNINSTALL), installed && !g_busy);
  EnableWindow(GetDlgItem(g_dialog, IDC_INSTALL), !g_busy);
  SetDlgItemTextW(g_dialog, IDC_INSTALL,
                  installed ? L"覆盖安装(&I)"
                            : L"安装(&I)");
}

INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM w, LPARAM l) {
  switch (message) {
    case WM_INITDIALOG: {
      g_dialog = dialog;
      SendMessageW(
          dialog, WM_SETICON, ICON_BIG,
          reinterpret_cast<LPARAM>(LoadIconW(g_instance,
                                             MAKEINTRESOURCEW(IDI_APP))));
      LOGFONTW font = {};
      HFONT current =
          reinterpret_cast<HFONT>(SendMessageW(dialog, WM_GETFONT, 0, 0));
      GetObjectW(current, sizeof(font), &font);
      font.lfHeight = static_cast<LONG>(font.lfHeight * 1.45);
      font.lfWeight = FW_SEMIBOLD;
      g_head_font = CreateFontIndirectW(&font);
      SendDlgItemMessageW(dialog, IDC_HEAD, WM_SETFONT,
                          reinterpret_cast<WPARAM>(g_head_font), TRUE);
      SetDlgItemTextW(dialog, IDC_HEAD, ZX_PRODUCT L"  " ZX_VERSION);
      SetDlgItemTextW(
          dialog, IDC_DESC,
          L"本机 TSF 输入法，内置 librime "
          L"与静态词库，完全离线运行。\n"
          L"安装需要管理员权限，将同时"
          L"注册 64 位与 32 位文本服务。\n"
          L"首次切换到本输入法时会编译"
          L"词库，约需数秒。");
      SetDlgItemTextW(dialog, IDC_PATHLABEL, L"安装位置");
      SetDlgItemTextW(dialog, IDC_PATH, g_install_root.c_str());
      SendDlgItemMessageW(dialog, IDC_PROGRESS, PBM_SETRANGE32, 0, 100);
      SetDlgItemTextW(
          dialog, IDC_STATUS,
          zx::Is64BitOS()
              ? L"就绪（64 位 Windows：将安装 x64 + x86）"
              : L"就绪（32 位 Windows：将安装 x86）");
      RefreshButtons();
      return TRUE;
    }
    case kMsgProgress:
      SendDlgItemMessageW(dialog, IDC_PROGRESS, PBM_SETPOS,
                          static_cast<WPARAM>(w), 0);
      return TRUE;
    case kMsgStatus: {
      auto* text = reinterpret_cast<wchar_t*>(l);
      if (text) {
        SetDlgItemTextW(dialog, IDC_STATUS, text);
        delete[] text;
      }
      return TRUE;
    }
    case kMsgDone: {
      auto* text = reinterpret_cast<wchar_t*>(l);
      const std::wstring body = text ? text : L"";
      delete[] text;
      InterlockedExchange(&g_busy, 0);
      SetDlgItemTextW(dialog, IDC_STATUS,
                      w ? L"安装完成。"
                        : L"安装失败。");
      RefreshButtons();
      MessageBoxW(dialog, body.c_str(), ZX_PRODUCT,
                  MB_OK | (w ? MB_ICONINFORMATION : MB_ICONERROR));
      return TRUE;
    }
    case WM_COMMAND:
      switch (LOWORD(w)) {
        case IDC_INSTALL:
          if (g_busy) return TRUE;
          InterlockedExchange(&g_busy, 1);
          RefreshButtons();
          CloseHandle(
              CreateThread(nullptr, 0, InstallThread, nullptr, 0, nullptr));
          return TRUE;
        case IDC_UNINSTALL: {
          const std::wstring root = zx::ReadInstallRoot();
          const std::wstring uninstaller = root + L"\\" ZX_UNINST_EXE;
          if (zx::FileExists(uninstaller)) {
            ShellExecuteW(dialog, L"open", uninstaller.c_str(), nullptr,
                          nullptr, SW_SHOWNORMAL);
            EndDialog(dialog, 0);
          } else {
            MessageBoxW(dialog,
                        L"未找到卸载程序。",
                        ZX_PRODUCT, MB_OK | MB_ICONWARNING);
          }
          return TRUE;
        }
        case IDCANCEL:
          if (g_busy) {
            MessageBoxW(
                dialog,
                L"安装正在进行，请稍候。",
                ZX_PRODUCT, MB_OK | MB_ICONINFORMATION);
            return TRUE;
          }
          EndDialog(dialog, 0);
          return TRUE;
        default:
          break;
      }
      return FALSE;
    case WM_CLOSE:
      if (!g_busy) EndDialog(dialog, 0);
      return TRUE;
    case WM_DESTROY:
      if (g_head_font) DeleteObject(g_head_font);
      return FALSE;
    default:
      return FALSE;
  }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
  g_instance = instance;
  g_install_root = zx::DefaultInstallRoot();

  int count = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &count);
  for (int i = 1; argv && i < count; ++i) {
    const std::wstring arg = argv[i];
    if (_wcsicmp(arg.c_str(), L"/S") == 0 ||
        _wcsicmp(arg.c_str(), L"/silent") == 0) {
      g_silent = true;
    } else if (arg.size() > 5 && _wcsnicmp(arg.c_str(), L"/dir=", 5) == 0) {
      g_install_root = arg.substr(5);
    }
  }
  if (argv) LocalFree(argv);

  // Refuse a target that is neither empty nor already ours. The payload
  // shares file names with the sibling product (x86|rime.dll,
  // data\default.yaml) and PromoteTree overwrites in place, so pointing
  // /dir= at the sibling's own directory would quietly corrupt it.
  // Refusing costs a custom install into a non-empty directory, which was
  // never a supported arrangement anyway.
  if (!zx::SamePath(g_install_root, zx::DefaultInstallRoot()) &&
      zx::DirExists(g_install_root) &&
      !zx::FileExists(g_install_root + L"\\" ZX_UNINST_EXE) &&
      !zx::DirIsEmpty(g_install_root)) {
    MessageBoxW(nullptr,
                (L"安装目录非空，且不是本输入法的目录，已停止：\n" +
                 g_install_root).c_str(),
                ZX_PRODUCT, MB_OK | MB_ICONERROR);
    return 1;
  }

  if (!zx::IsElevated()) {
    MessageBoxW(nullptr,
                L"安装程序需要管理员权限。",
                ZX_PRODUCT, MB_OK | MB_ICONERROR);
    return 1;
  }

  INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_PROGRESS_CLASS};
  InitCommonControlsEx(&controls);

  if (g_silent) {
    // No UI, ever: a modal box here blocks an unattended deployment that
    // has nobody to dismiss it. The failure goes to a log beside the
    // installer and to the exit code, which is all a deployment tool reads.
    std::wstring error;
    const bool ok = DoInstall(&error);
    if (!ok) {
      const std::wstring log =
          zx::TempDir() + zx::UniqueTempName(L"zuxia-setup-error", L".log");
      HANDLE file = CreateFileW(log.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (file != INVALID_HANDLE_VALUE) {
        const std::string utf8 = zx::Narrow(error);
        DWORD written = 0;
        WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()),
                  &written, nullptr);
        CloseHandle(file);
      }
      OutputDebugStringW(error.c_str());
    }
    return ok ? 0 : 1;
  }

  // One installer at a time. Two concurrent runs share the staging
  // directory and one empties the other's verified tree mid-install.
  HANDLE single = CreateMutexW(nullptr, FALSE, L"Global\\ZuxiaSetup");
  if (single && GetLastError() == ERROR_ALREADY_EXISTS) {
    MessageBoxW(nullptr, L"安装程序已在运行。", ZX_PRODUCT,
                MB_OK | MB_ICONINFORMATION);
    CloseHandle(single);
    return 1;
  }

  DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_MAIN), nullptr, DialogProc, 0);
  return 0;
}
