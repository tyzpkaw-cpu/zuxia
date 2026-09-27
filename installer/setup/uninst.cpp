// Zuxia IME uninstaller. Ships inside the setup binary and is written to
// the install directory, so it has to relocate itself before deleting that
// directory.
#include "zxcommon.h"

#include <shellapi.h>
#include <string>

namespace {

bool g_silent = false;
bool g_purge = false;

void Note(const std::wstring& text, UINT icon) {
  if (g_silent) return;
  MessageBoxW(nullptr, text.c_str(), ZX_PRODUCT, MB_OK | icon);
}

int RunUninstall(const std::wstring& root) {
  const std::wstring x86 = root + L"\\x86\\" ZX_TSF_DLL;
  const std::wstring x64 = root + L"\\x64\\" ZX_TSF_DLL;

  // Unregister before the files go away; each call is idempotent.
  if (zx::FileExists(x64)) zx::RegisterDll(zx::Regsvr32For64(), x64, true);
  if (zx::FileExists(x86)) zx::RegisterDll(zx::Regsvr32For32(), x86, true);

  zx::RemoveKey(ZX_ARP_KEY);
  zx::RemoveKey(ZX_PRODUCT_KEY);

  const bool removed = zx::DeleteTree(root);

  if (g_purge) {
    const std::wstring local = zx::Env(L"LOCALAPPDATA");
    if (!local.empty()) zx::DeleteTree(local + L"\\" ZX_DIRNAME);
  }

  std::wstring message =
      L"应物音形足下输入法已卸载。";
  if (!removed) {
    message +=
        L"\n\n部分文件仍被运行中的程序占用，"
        L"将在下次重启后清除。";
  }
  if (!g_purge) {
    message +=
        L"\n\n用户词库缓存已保留在 "
        L"%LOCALAPPDATA%\\Zuxia。";
  }
  Note(message, MB_ICONINFORMATION);
  return 0;
}

// Copies this binary out of the install tree and hands the work to the copy.
int Relaunch(const std::wstring& root) {
  const std::wstring temp = zx::TempDir() + L"zuxia-uninstall-run.exe";
  DeleteFileW(temp.c_str());
  if (!CopyFileW(zx::SelfPath().c_str(), temp.c_str(), FALSE)) {
    // Falling back to an in-place uninstall still works; only the uninstaller
    // itself will be left behind for the reboot sweep.
    return RunUninstall(root);
  }

  std::wstring args = L"/run \"" + root + L"\"";
  if (g_silent) args += L" /silent";
  if (g_purge) args += L" /purge";

  SHELLEXECUTEINFOW info = {};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_NOCLOSEPROCESS;
  info.lpVerb = L"open";
  info.lpFile = temp.c_str();
  info.lpParameters = args.c_str();
  info.nShow = SW_SHOWNORMAL;
  if (!ShellExecuteExW(&info)) {
    DeleteFileW(temp.c_str());
    return RunUninstall(root);
  }
  if (info.hProcess) CloseHandle(info.hProcess);
  return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
  std::wstring run_root;
  bool run_mode = false;

  int count = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &count);
  for (int i = 1; argv && i < count; ++i) {
    const std::wstring arg = argv[i];
    if (_wcsicmp(arg.c_str(), L"/silent") == 0 ||
        _wcsicmp(arg.c_str(), L"/S") == 0) {
      g_silent = true;
    } else if (_wcsicmp(arg.c_str(), L"/purge") == 0) {
      g_purge = true;
    } else if (_wcsicmp(arg.c_str(), L"/run") == 0) {
      run_mode = true;
      if (i + 1 < count) run_root = argv[++i];
    }
  }
  if (argv) LocalFree(argv);

  if (!zx::IsElevated()) {
    Note(L"卸载需要管理员权限。", MB_ICONERROR);
    return 1;
  }

  if (run_mode) {
    if (run_root.empty()) run_root = zx::ReadInstallRoot();
    if (run_root.empty()) return 1;
    // The copy in %TEMP% cannot delete itself while running.
    MoveFileExW(zx::SelfPath().c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    return RunUninstall(run_root);
  }

  std::wstring root = zx::ReadInstallRoot();
  if (root.empty()) {
    const std::wstring self = zx::SelfPath();
    const size_t slash = self.find_last_of(L'\\');
    if (slash != std::wstring::npos) root = self.substr(0, slash);
  }
  if (root.empty() || !zx::DirExists(root)) {
    Note(L"未找到已安装的应物音形足下输入法。",
         MB_ICONWARNING);
    return 1;
  }

  if (!g_silent) {
    if (MessageBoxW(nullptr,
                    L"确定要卸载应物音形足下输入法吗？\n\n"
                    L"将注销 64 位与 32 位文本服务"
                    L"并删除程序文件。",
                    ZX_PRODUCT,
                    MB_YESNO | MB_ICONQUESTION) != IDYES) {
      return 1;
    }
    if (MessageBoxW(nullptr,
                    L"是否同时删除用户词库缓存"
                    L"（%LOCALAPPDATA%\\Zuxia）？\n\n"
                    L"选择「否」将保留，便于重装后"
                    L"免去重新编译词库。",
                    ZX_PRODUCT,
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
      g_purge = true;
    }
  }

  return Relaunch(root);
}
