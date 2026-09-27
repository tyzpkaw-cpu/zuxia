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

  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
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
}

// Moves the verified staging tree over the live tree, one file at a time so a
// DLL that is still mapped into a running host can be parked and replaced.
bool PromoteTree(const std::wstring& staging, const std::wstring& target,
                 std::wstring* error) {
  const std::wstring search = staging + L"\\*";
  WIN32_FIND_DATAW find = {};
  HANDLE handle = FindFirstFileW(search.c_str(), &find);
  if (handle == INVALID_HANDLE_VALUE) return true;
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

bool DoInstall(std::wstring* error) {
  const std::wstring root = g_install_root;
  const std::wstring staging = root + L".new";
  const std::wstring cab = zx::TempDir() + L"zuxia-payload.cab";

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
  if (zx::Is64BitOS() &&
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
          L"\n\n注意：本机为 ARM64 Windows，"
          L"本版本只含 x64/x86 文本服务，"
          L"仅在模拟运行的应用中可用。";
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

  if (!zx::IsElevated()) {
    MessageBoxW(nullptr,
                L"安装程序需要管理员权限。",
                ZX_PRODUCT, MB_OK | MB_ICONERROR);
    return 1;
  }

  INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_PROGRESS_CLASS};
  InitCommonControlsEx(&controls);

  if (g_silent) {
    std::wstring error;
    const bool ok = DoInstall(&error);
    if (!ok) {
      MessageBoxW(nullptr, error.c_str(), ZX_PRODUCT, MB_OK | MB_ICONERROR);
    }
    return ok ? 0 : 1;
  }

  DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_MAIN), nullptr, DialogProc, 0);
  return 0;
}
