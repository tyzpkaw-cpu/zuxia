<# : batch part. cmd runs the lines below up to "exit /b"; for PowerShell this is a comment.
@echo off
setlocal
set "ZX_SELF=%~f0"
set "ZX_PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "ZX_PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
"%ZX_PS%" -NoProfile -ExecutionPolicy Bypass -Command "iex ([IO.File]::ReadAllText($env:ZX_SELF, [Text.Encoding]::UTF8))"
exit /b
#>
# ---------------------------------------------------------------------------
# 刷新足下输入法 —— 双击运行，不用重启电脑就能让新装的版本生效
#
# 装新版的时候，已经开着的程序（Edge、微信、资源管理器……）手里还是旧版输入法：
# Windows 不会替正在运行的程序换掉已经加载的文件。这个脚本挨个看每个程序里
# 装着的 ZuxiaTSF.dll 是不是磁盘上这一版（比的是文件头里的时间戳，每次编译都
# 不一样），把还在用旧版的找出来，问过你之后关掉再重新打开：
#   · 资源管理器、Edge、Office 这类登记过自动重开的，会自己回来
#   · 没登记的（比如微信）要你自己重新打开
#   · 不肯关的（有没保存的内容、在后台跑），会再问一次要不要强制关
# 关和重开用的是 Windows 自带的 Restart Manager（装软件时提示「以下程序需要
# 关闭」用的就是它）。不需要管理员权限，不改任何文件和设置。
# ---------------------------------------------------------------------------

$ErrorActionPreference = 'Stop'
try { $Host.UI.RawUI.WindowTitle = '刷新足下输入法' } catch { }

# C# 5 的写法：Windows PowerShell 5.1 的 Add-Type 用的是 .NET Framework 自带的编译器。
$RmTypeSource = @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using FILETIME = System.Runtime.InteropServices.ComTypes.FILETIME;

public static class ZuxiaRm
{
    [StructLayout(LayoutKind.Sequential)]
    public struct RM_UNIQUE_PROCESS
    {
        public int dwProcessId;
        public FILETIME ProcessStartTime;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct RM_PROCESS_INFO
    {
        public RM_UNIQUE_PROCESS Process;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string strAppName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string strServiceShortName;
        public int ApplicationType;
        public uint AppStatus;
        public uint TSSessionId;
        [MarshalAs(UnmanagedType.Bool)]
        public bool bRestartable;
    }

    // ---- 哪个进程里装着哪一版 ------------------------------------------------

    private const uint PROCESS_VM_READ = 0x0010;
    private const uint PROCESS_QUERY_INFORMATION = 0x0400;
    private const uint PROCESS_QUERY_LIMITED_INFORMATION = 0x1000;
    private const uint LIST_MODULES_ALL = 0x03;

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr OpenProcess(uint dwDesiredAccess, bool bInheritHandle, int dwProcessId);

    [DllImport("kernel32.dll")]
    private static extern bool CloseHandle(IntPtr hObject);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool ReadProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress,
        [Out] byte[] lpBuffer, IntPtr nSize, out IntPtr lpNumberOfBytesRead);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GetProcessTimes(IntPtr hProcess, out FILETIME lpCreationTime,
        out FILETIME lpExitTime, out FILETIME lpKernelTime, out FILETIME lpUserTime);

    [DllImport("psapi.dll", SetLastError = true)]
    private static extern bool EnumProcessModulesEx(IntPtr hProcess, [Out] IntPtr[] lphModule,
        int cb, out int lpcbNeeded, uint dwFilterFlag);

    [DllImport("psapi.dll", CharSet = CharSet.Unicode)]
    private static extern uint GetModuleBaseNameW(IntPtr hProcess, IntPtr hModule,
        StringBuilder lpBaseName, int nSize);

    // 磁盘上一个 PE 文件的 TimeDateStamp。
    public static uint FileStamp(string path)
    {
        byte[] head = new byte[4096];
        int got;
        using (FileStream file = new FileStream(path, FileMode.Open, FileAccess.Read,
                                                FileShare.ReadWrite | FileShare.Delete))
        {
            got = file.Read(head, 0, head.Length);
        }
        if (got < 0x40) throw new InvalidDataException(path);
        int pe = BitConverter.ToInt32(head, 0x3C);
        if (pe < 0 || pe + 12 > got) throw new InvalidDataException(path);
        return BitConverter.ToUInt32(head, pe + 8);
    }

    private static bool ReadUInt32(IntPtr process, long address, out uint value)
    {
        byte[] buffer = new byte[4];
        IntPtr read;
        value = 0;
        if (!ReadProcessMemory(process, new IntPtr(address), buffer, new IntPtr(4), out read)) return false;
        if (read.ToInt64() != 4) return false;
        value = BitConverter.ToUInt32(buffer, 0);
        return true;
    }

    // 进程里每一个叫 moduleName 的模块的 TimeDateStamp（读内存里的文件头）。
    // 看不了这个进程（系统进程、以管理员身份运行的程序）就返回 null。
    public static uint[] LoadedStamps(int pid, string moduleName)
    {
        IntPtr process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, false, pid);
        if (process == IntPtr.Zero) return null;
        try
        {
            int size = IntPtr.Size;
            IntPtr[] modules = new IntPtr[1024];
            int needed;
            if (!EnumProcessModulesEx(process, modules, modules.Length * size, out needed, LIST_MODULES_ALL)) return null;
            if (needed > modules.Length * size)
            {
                modules = new IntPtr[needed / size + 64];
                if (!EnumProcessModulesEx(process, modules, modules.Length * size, out needed, LIST_MODULES_ALL)) return null;
            }
            int count = Math.Min(needed / size, modules.Length);
            List<uint> stamps = new List<uint>();
            StringBuilder name = new StringBuilder(260);
            for (int i = 0; i < count; ++i)
            {
                name.Length = 0;
                if (GetModuleBaseNameW(process, modules[i], name, 260) == 0) continue;
                if (!string.Equals(name.ToString(), moduleName, StringComparison.OrdinalIgnoreCase)) continue;
                long module = modules[i].ToInt64();
                uint pe, stamp;
                if (!ReadUInt32(process, module + 0x3C, out pe)) continue;
                if (!ReadUInt32(process, module + pe + 8, out stamp)) continue;
                stamps.Add(stamp);
            }
            return stamps.ToArray();
        }
        finally
        {
            CloseHandle(process);
        }
    }

    // Restart Manager 认进程要「进程号 + 启动时间」，防止进程号被重用。
    public static RM_UNIQUE_PROCESS Identify(int pid)
    {
        IntPtr process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, false, pid);
        if (process == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
        try
        {
            FILETIME created, exited, kernel, user;
            if (!GetProcessTimes(process, out created, out exited, out kernel, out user))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            RM_UNIQUE_PROCESS id = new RM_UNIQUE_PROCESS();
            id.dwProcessId = pid;
            id.ProcessStartTime = created;
            return id;
        }
        finally
        {
            CloseHandle(process);
        }
    }

    // ---- Restart Manager -----------------------------------------------------

    [DllImport("rstrtmgr.dll", CharSet = CharSet.Unicode)]
    private static extern int RmStartSession(out uint pSessionHandle, int dwSessionFlags,
                                             StringBuilder strSessionKey);

    [DllImport("rstrtmgr.dll")]
    private static extern int RmEndSession(uint pSessionHandle);

    [DllImport("rstrtmgr.dll", CharSet = CharSet.Unicode)]
    private static extern int RmRegisterResources(uint pSessionHandle, uint nFiles,
        string[] rgsFilenames, uint nApplications, [In] RM_UNIQUE_PROCESS[] rgApplications,
        uint nServices, string[] rgsServiceNames);

    [DllImport("rstrtmgr.dll")]
    private static extern int RmGetList(uint dwSessionHandle, out uint pnProcInfoNeeded,
        ref uint pnProcInfo, [In, Out] RM_PROCESS_INFO[] rgAffectedApps,
        ref uint lpdwRebootReasons);

    [DllImport("rstrtmgr.dll")]
    private static extern int RmShutdown(uint pSessionHandle, uint lActionFlags, IntPtr fnStatus);

    [DllImport("rstrtmgr.dll")]
    private static extern int RmRestart(uint pSessionHandle, int dwRestartFlags, IntPtr fnStatus);

    public sealed class Session : IDisposable
    {
        private uint handle;
        private bool open;

        public Session()
        {
            StringBuilder key = new StringBuilder(64);
            int rc = RmStartSession(out handle, 0, key);
            if (rc != 0) throw new Win32Exception(rc);
            open = true;
        }

        public void AddProcesses(RM_UNIQUE_PROCESS[] processes)
        {
            int rc = RmRegisterResources(handle, 0, null, (uint)processes.Length, processes, 0, null);
            if (rc != 0) throw new Win32Exception(rc);
        }

        public RM_PROCESS_INFO[] List()
        {
            uint needed = 0, count = 0, reasons = 0;
            int rc = RmGetList(handle, out needed, ref count, null, ref reasons);
            // ERROR_MORE_DATA：两次调用之间名单可能变长，多试几轮
            for (int round = 0; rc == 234 && round < 8; ++round)
            {
                RM_PROCESS_INFO[] buffer = new RM_PROCESS_INFO[needed];
                count = needed;
                rc = RmGetList(handle, out needed, ref count, buffer, ref reasons);
                if (rc == 0)
                {
                    Array.Resize(ref buffer, (int)count);
                    return buffer;
                }
            }
            if (rc != 0) throw new Win32Exception(rc);
            return new RM_PROCESS_INFO[0];
        }

        // 0 = 全关掉了；351 = ERROR_FAIL_SHUTDOWN，有的没关掉（看 List 的 AppStatus）
        public int Shutdown(bool force)
        {
            return RmShutdown(handle, force ? 1u : 0u, IntPtr.Zero);
        }

        // 0 = 全重开了；352 = ERROR_FAIL_RESTART，有的没重开
        public int Restart()
        {
            return RmRestart(handle, 0, IntPtr.Zero);
        }

        public void Dispose()
        {
            if (open)
            {
                RmEndSession(handle);
                open = false;
            }
        }
    }
}
'@

# Restart Manager 的常量
$RmExplorer = 4
$StatusRunning = 0x1
$StatusStopped = 0x2
$StatusStoppedOther = 0x4
$StatusRestarted = 0x8
$StatusErrorOnStop = 0x10
$StatusErrorOnRestart = 0x20

# 运行这个脚本的终端：关了它，脚本也跟着没了，还来不及把别的程序重新打开。
$Terminals = @('conhost', 'OpenConsole', 'WindowsTerminal')
# 系统自己的界面进程：直接结束就行，Windows 会马上自己把它们拉起来。
$ShellHosts = @('SearchHost', 'SearchApp', 'SearchUI', 'StartMenuExperienceHost',
                'ShellExperienceHost', 'TextInputHost')
# 关不得的系统进程
$SystemProcs = @('csrss', 'winlogon', 'LogonUI', 'dwm', 'fontdrvhost', 'sihost', 'svchost',
                 'ctfmon', 'services', 'lsass', 'smss', 'wininit')

function Get-RegString([string]$Path, [string]$Name) {
    try { return [string](Get-ItemProperty -LiteralPath $Path -Name $Name -ErrorAction Stop).$Name }
    catch { return '' }
}

function Wait-Explorer([int]$SessionId) {
    for ($i = 0; $i -lt 40; $i++) {
        $shell = @(Get-Process -Name explorer -ErrorAction SilentlyContinue |
                   Where-Object { $_.SessionId -eq $SessionId })
        if ($shell.Count -gt 0) { return }
        Start-Sleep -Milliseconds 250
    }
    # 十秒还没回来就自己拉起来：不能让人没了任务栏
    Start-Process -FilePath (Join-Path $env:SystemRoot 'explorer.exe')
}

function Invoke-Refresh {
    Write-Host '刷新足下输入法' -ForegroundColor Cyan
    Write-Host '装了新版以后，已经开着的程序手里还是旧版输入法。这里把它们找出来，关掉再重新打开，不用重启电脑。'
    Write-Host ''

    # 1. 安装目录：和 scripts/verify-install.ps1 同一个找法
    $arp = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\ZuxiaIME'
    $root = Get-RegString 'HKLM:\SOFTWARE\Zuxia' 'InstallPath'
    if (-not $root) { $root = Get-RegString $arp 'InstallLocation' }
    if (-not $root) {
        $pf = $env:ProgramW6432
        if (-not $pf) { $pf = $env:ProgramFiles }
        $root = Join-Path $pf 'Zuxia'
    }
    $version = Get-RegString $arp 'DisplayVersion'
    if ($version) { Write-Host "已安装：足下输入法 $version" }
    Write-Host "安装目录：$root"

    if (-not ('ZuxiaRm' -as [type])) { Add-Type -TypeDefinition $RmTypeSource -IgnoreWarnings }

    # 2. 磁盘上这一版的时间戳（64 位、32 位各一个）
    $current = @()
    foreach ($arch in @('x64', 'x86')) {
        $dll = Join-Path (Join-Path $root $arch) 'ZuxiaTSF.dll'
        if (Test-Path -LiteralPath $dll) { $current += [ZuxiaRm]::FileStamp($dll) }
    }
    if ($current.Count -eq 0) {
        Write-Host ''
        Write-Host '没找到足下输入法（ZuxiaTSF.dll）。先装好再运行这个脚本。' -ForegroundColor Yellow
        return
    }

    # 3. 挨个看：哪个程序里装着的不是这一版
    Write-Host '正在查看每个程序里的输入法版本……'
    $mySession = (Get-Process -Id $PID).SessionId
    $targets = New-Object System.Collections.ArrayList   # 交给 Restart Manager 关、重开
    $shells = New-Object System.Collections.ArrayList    # 系统界面，直接结束
    $kept = New-Object System.Collections.ArrayList      # 不动
    foreach ($p in @(Get-Process | Where-Object { $_.SessionId -eq $mySession -and $_.Id -ne $PID })) {
        $stamps = $null
        try { $stamps = [ZuxiaRm]::LoadedStamps($p.Id, 'ZuxiaTSF.dll') } catch { $stamps = $null }
        if ($null -eq $stamps) { continue }
        $stale = @($stamps | Where-Object { $current -notcontains $_ })
        if ($stale.Count -eq 0) { continue }
        $name = $p.ProcessName
        if ($Terminals -contains $name) {
            [void]$kept.Add('终端窗口（这个窗口就开在它里面）：用完把它关掉重开即可')
        } elseif ($SystemProcs -contains $name) {
            [void]$kept.Add($name + '：系统进程，这里不能关，下次重启电脑时自然换成新版')
        } elseif ($ShellHosts -contains $name) {
            [void]$shells.Add($p)
        } else {
            try { [void]$targets.Add([ZuxiaRm]::Identify($p.Id)) } catch { }
        }
    }

    if ($targets.Count -eq 0 -and $shells.Count -eq 0) {
        Write-Host ''
        if ($kept.Count -eq 0) {
            Write-Host '所有程序都已经是新版，什么都不用做。' -ForegroundColor Green
        } else {
            Write-Host '下面这些还在用旧版，但这里不能替你关：' -ForegroundColor Yellow
            foreach ($line in $kept) { Write-Host ('  · ' + $line) }
        }
        return
    }

    # 4. 列出来，问一声
    $session = New-Object 'ZuxiaRm+Session'
    $final = @()
    $hadExplorer = $false
    try {
        $apps = @()
        if ($targets.Count -gt 0) {
            $session.AddProcesses([ZuxiaRm+RM_UNIQUE_PROCESS[]]$targets.ToArray())
            $apps = @($session.List())
        }
        $names = @{}
        foreach ($app in $apps) {
            $label = [string]$app.strAppName
            if (-not $label) {
                try { $label = (Get-Process -Id $app.Process.dwProcessId -ErrorAction Stop).ProcessName } catch { }
            }
            if (-not $label) { $label = '进程 ' + $app.Process.dwProcessId }
            $names[[int]$app.Process.dwProcessId] = $label
            if ($app.ApplicationType -eq $RmExplorer) { $hadExplorer = $true }
        }

        Write-Host ''
        Write-Host '下面这些程序还在用旧版：' -ForegroundColor Yellow
        $shown = @{}
        foreach ($app in $apps) {
            $back = if ($app.ApplicationType -eq $RmExplorer -or $app.bRestartable) { '会自动重新打开' } else { '关掉后要你自己重新打开' }
            $line = '  · {0}（{1}）' -f $names[[int]$app.Process.dwProcessId], $back
            if (-not $shown.ContainsKey($line)) { $shown[$line] = $true; Write-Host $line }
        }
        foreach ($n in @($shells | ForEach-Object { $_.ProcessName } | Select-Object -Unique)) {
            Write-Host ('  · {0}（Windows 自己的界面，会自动回来）' -f $n)
        }
        foreach ($line in @($kept | Select-Object -Unique)) { Write-Host ('  · ' + $line) -ForegroundColor DarkGray }
        Write-Host ''
        Write-Host '先把正在编辑的东西保存好，再按回车：上面这些会被关掉、再重新打开。'
        Write-Host '（Edge 重开后要是标签页没回来，按 Ctrl+Shift+T 就能找回。）'
        Write-Host '不想现在弄，直接关掉这个窗口就行。'
        [void](Read-Host)

        # 5. 关掉、重开
        foreach ($p in $shells) { try { Stop-Process -Id $p.Id -Force -ErrorAction Stop } catch { } }
        if ($apps.Count -gt 0) {
            Write-Host '正在关闭……'
            $rc = $session.Shutdown($false)
            if ($rc -ne 0) {
                $left = @($session.List() | Where-Object {
                    ($_.AppStatus -band ($StatusRunning -bor $StatusErrorOnStop)) -and
                    -not ($_.AppStatus -band ($StatusStopped -bor $StatusStoppedOther)) })
                if ($left.Count -gt 0) {
                    Write-Host ''
                    Write-Host '这几个没能自己关掉（可能有没保存的内容，或者是在后台运行）：' -ForegroundColor Yellow
                    foreach ($n in @($left | ForEach-Object { $names[[int]$_.Process.dwProcessId] } | Select-Object -Unique)) {
                        Write-Host ('  · ' + $n)
                    }
                    $answer = Read-Host '输入 Y 再按回车，强制关掉它们（没保存的内容会丢）；直接按回车就跳过'
                    if ($answer -match '^\s*[yY]') {
                        Write-Host '正在强制关闭……'
                        [void]$session.Shutdown($true)
                    }
                }
            }
            Write-Host '正在重新打开……'
            [void]$session.Restart()
            $final = @($session.List())
        }
    } finally {
        $session.Dispose()
        if ($hadExplorer) { Wait-Explorer $mySession }
    }

    # 6. 结果
    $reopened = New-Object System.Collections.ArrayList
    $manual = New-Object System.Collections.ArrayList
    $still = New-Object System.Collections.ArrayList
    foreach ($app in $final) {
        $label = $names[[int]$app.Process.dwProcessId]
        $status = [int]$app.AppStatus
        if ($status -band $StatusRestarted) {
            [void]$reopened.Add($label)
        } elseif ($status -band ($StatusStopped -bor $StatusStoppedOther -bor $StatusErrorOnRestart)) {
            [void]$manual.Add($label)
        } else {
            [void]$still.Add($label)
        }
    }
    Write-Host ''
    if ($reopened.Count -gt 0) { Write-Host ('已经重新打开：' + (@($reopened | Select-Object -Unique) -join '、')) -ForegroundColor Green }
    if ($manual.Count -gt 0) { Write-Host ('已经关掉，要你自己重新打开：' + (@($manual | Select-Object -Unique) -join '、')) -ForegroundColor Yellow }
    if ($still.Count -gt 0) { Write-Host ('还开着、还在用旧版（自己关掉重开，或者重启电脑）：' + (@($still | Select-Object -Unique) -join '、')) -ForegroundColor Yellow }
    Write-Host ''
    Write-Host '好了。现在打几个字试试：拆字窗的标题条上写着「拆字  某个字」，就是新版。'
}

try {
    Invoke-Refresh
} catch {
    Write-Host ''
    Write-Host ('出错了：' + $_.Exception.Message) -ForegroundColor Red
    Write-Host '把这个窗口截个图发给开发者就行。'
} finally {
    Write-Host ''
    [void](Read-Host '按回车关闭这个窗口')
}
