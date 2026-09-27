<#
.SYNOPSIS
    在任意一台 Windows 设备上验证应物音形足下输入法装得对不对、能不能打字。

.DESCRIPTION
    「在任何 Windows 设备上都能安装」这句话没法靠断言成立，只能靠证据。这个脚本
    把证据收齐：系统与架构、文件是否落地、COM 与 TSF 注册是否写入两个视图、
    「应用和功能」条目、载荷哈希、用户数据目录、运行日志里的错误、以及（可选）
    在临时目录里真跑一次引擎。

    只读为主：不改注册表、不写安装目录。只有 -EngineTestExe 会在 %TEMP% 下建一个
    沙盒副本跑引擎，跑完删掉。

    PowerShell 5.1 与 7 都能跑；不需要管理员（读 HKLM 不需要提权）。

.PARAMETER InstallRoot
    安装目录。默认从注册表 SOFTWARE\Zuxia 的 InstallPath 读，读不到退到
    %ProgramFiles%\Zuxia。

.PARAMETER EngineTestExe
    tools\engine-test.exe 的路径。给了就在临时沙盒里真跑一遍引擎（最强的一项检查）。

.PARAMETER JsonPath
    把结果写成 JSON，便于在多台设备之间汇总比对。

.EXAMPLE
    pwsh scripts/verify-install.ps1 -JsonPath C:\temp\zuxia-win11-x64.json

.EXAMPLE
    # 在干净机器上装完后，连引擎一起验
    powershell -ExecutionPolicy Bypass -File verify-install.ps1 -EngineTestExe .\engine-test.exe
#>
[CmdletBinding()]
param(
    [string]$InstallRoot = '',
    [string]$EngineTestExe = '',
    [string]$JsonPath = ''
)

$ErrorActionPreference = 'Continue'

# ---------------------------------------------------------------- 身份常量 --
$Clsid      = '{A0073A11-FF52-4185-A655-D0C9171B7850}'
$ProfileGuid = '{699B0EC1-3FDB-415D-89C0-0E55AA2EFFAF}'
$LangId     = '0x00000804'          # zh-CN
$ProductName = '应物音形足下输入法'
$TsfDll     = 'ZuxiaTSF.dll'
$ArpKey     = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\ZuxiaIME'
$ProductKey = 'HKLM:\SOFTWARE\Zuxia'

# 兄弟产品：装足下不该把它弄坏
$SiblingClsid = '{57A9A4DA-60E5-426D-A220-5C162C976CAA}'
$SiblingDll   = 'YingwuTSF.dll'

$results = New-Object System.Collections.ArrayList

function Add-Result {
    param(
        [string]$Group,
        [string]$Item,
        [ValidateSet('PASS', 'FAIL', 'WARN', 'SKIP')][string]$Status,
        [string]$Detail = ''
    )
    [void]$results.Add([pscustomobject]@{
        Group  = $Group
        Item   = $Item
        Status = $Status
        Detail = $Detail
    })
}

function Get-RegValue {
    param([string]$Path, [string]$Name = '')
    try {
        if (-not (Test-Path -LiteralPath $Path)) { return $null }
        if ($Name -eq '') {
            return (Get-ItemProperty -LiteralPath $Path -ErrorAction Stop).'(default)'
        }
        return (Get-ItemProperty -LiteralPath $Path -Name $Name -ErrorAction Stop).$Name
    } catch {
        return $null
    }
}

function Test-RegKey {
    param([string]$Path)
    return (Test-Path -LiteralPath $Path)
}

Write-Host ''
Write-Host "应物音形足下输入法 —— 装机验证" -ForegroundColor Cyan
Write-Host "产品身份：$ProductName  $Clsid"
Write-Host ('=' * 74)

# ------------------------------------------------------------------ 1 环境 --
$osCaption = ''
$osBuild = ''
try {
    $os = Get-CimInstance -ClassName Win32_OperatingSystem -ErrorAction Stop
    $osCaption = $os.Caption
    $osBuild = $os.BuildNumber
} catch {
    $osCaption = [System.Environment]::OSVersion.VersionString
    $osBuild = [System.Environment]::OSVersion.Version.Build
}
$is64Os = [System.Environment]::Is64BitOperatingSystem
$is64Ps = [System.Environment]::Is64BitProcess
$arch = $env:PROCESSOR_ARCHITECTURE
$archWow = $env:PROCESSOR_ARCHITEW6432

Add-Result '环境' '操作系统' 'PASS' ("{0} (build {1})" -f $osCaption, $osBuild)
if ($is64Os) {
    $nativeArch = if ($archWow) { $archWow } else { $arch }
    Add-Result '环境' '体系结构' 'PASS' ("64 位系统，原生架构 $nativeArch" + $(if ($archWow) { "（本进程为 $arch）" } else { '' }))
} else {
    Add-Result '环境' '体系结构' 'PASS' ("32 位系统，架构 $arch —— 只会安装 x86 文本服务")
}

$isArm64 = ($arch -eq 'ARM64') -or ($archWow -eq 'ARM64')
if ($isArm64) {
    Add-Result '环境' 'ARM64 Windows' 'WARN' '本版本只含 x64/x86 文本服务：x64 模拟运行的应用里可用，原生 ARM64 应用（Edge ARM64、系统外壳等）里不可用'
} else {
    Add-Result '环境' 'ARM64 Windows' 'PASS' '不适用'
}

if ($osBuild -and [int]$osBuild -lt 10240) {
    Add-Result '环境' '最低系统版本' 'FAIL' ("build {0} 低于 Windows 10：本产品按 Windows 10 及以上构建（WINVER=0x0A00）" -f $osBuild)
} else {
    Add-Result '环境' '最低系统版本' 'PASS' 'Windows 10 及以上'
}

$elevated = $false
try {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $elevated = (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
} catch { }
$psv = $PSVersionTable.PSVersion.ToString()

# ------------------------------------------------------------------ 2 路径 --
if (-not $InstallRoot) {
    $InstallRoot = Get-RegValue $ProductKey 'InstallPath'
    if (-not $InstallRoot) { $InstallRoot = Join-Path $env:ProgramFiles 'Zuxia' }
}
Add-Result '路径' '安装目录' 'PASS' $InstallRoot
Add-Result '路径' 'ProgramFiles' 'PASS' ("{0}   ProgramW6432={1}" -f $env:ProgramFiles, $(if ($env:ProgramW6432) { $env:ProgramW6432 } else { '（不存在，32 位系统）' }))

if ($InstallRoot -match '[^\x00-\x7F]') {
    Add-Result '路径' '目录含非 ASCII 字符' 'WARN' '安装器与引擎都用宽字符 API，理论无碍；但建议在一台这样的设备上实测一次'
} else {
    Add-Result '路径' '目录含非 ASCII 字符' 'PASS' '不适用'
}

# ------------------------------------------------------------------ 3 文件 --
$rootExists = Test-Path -LiteralPath $InstallRoot
if ($rootExists) {
    Add-Result '文件' '安装目录存在' 'PASS' $InstallRoot
} else {
    Add-Result '文件' '安装目录存在' 'FAIL' ("未找到 {0} —— 未安装或装到了别处" -f $InstallRoot)
}

$archDirs = @('x64', 'x86')
if (-not $is64Os) { $archDirs = @('x86') }

foreach ($a in $archDirs) {
    $dir = Join-Path $InstallRoot $a
    $dll = Join-Path $dir $TsfDll
    $rime = Join-Path $dir 'rime.dll'
    $ok = (Test-Path -LiteralPath $dll) -and (Test-Path -LiteralPath $rime)
    Add-Result '文件' ("{0}\ 文本服务与引擎" -f $a) $(if ($ok) { 'PASS' } else { 'FAIL' }) (
        "ZuxiaTSF.dll={0}  rime.dll={1}" -f (Test-Path -LiteralPath $dll), (Test-Path -LiteralPath $rime))
}
if ($is64Os -and -not (Test-Path -LiteralPath (Join-Path $InstallRoot 'x86\'))) {
    Add-Result '文件' 'x86 目录' 'WARN' '64 位系统上缺少 x86 文本服务：32 位应用（旧版 Office 等）里无法输入'
}

$dataFiles = @('default.yaml', 'zuxia.schema.yaml', 'zuxia.dict.yaml', 'zuxia_char_codes.dict.yaml')
foreach ($f in $dataFiles) {
    $p = Join-Path (Join-Path $InstallRoot 'data') $f
    Add-Result '文件' ("data\{0}" -f $f) $(if (Test-Path -LiteralPath $p) { 'PASS' } else { 'FAIL' }) ''
}

# 载荷哈希清单：安装目录里若有 MANIFEST.sha256，逐条核对
$manifest = Join-Path $InstallRoot 'MANIFEST.sha256'
if (Test-Path -LiteralPath $manifest) {
    $bad = 0; $checked = 0; $missing = 0
    foreach ($line in (Get-Content -LiteralPath $manifest)) {
        if ($line -notmatch '^([0-9a-fA-F]{64})\s+\*?(.+)$') { continue }
        $want = $Matches[1].ToLower()
        $rel = $Matches[2].Trim()
        $file = Join-Path $InstallRoot ($rel -replace '/', '\')
        if (-not (Test-Path -LiteralPath $file)) { $missing++; continue }
        $got = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLower()
        $checked++
        if ($got -ne $want) {
            $bad++
            Add-Result '载荷' ("哈希不符 {0}" -f $rel) 'FAIL' ("期望 {0}，实际 {1}" -f $want.Substring(0, 16), $got.Substring(0, 16))
        }
    }
    if ($bad -eq 0 -and $missing -eq 0) {
        Add-Result '载荷' 'MANIFEST.sha256 全部相符' 'PASS' ("{0} 个文件" -f $checked)
    } elseif ($missing -gt 0) {
        Add-Result '载荷' 'MANIFEST.sha256 有缺失文件' 'FAIL' ("{0} 个文件缺失" -f $missing)
    }
} else {
    Add-Result '载荷' 'MANIFEST.sha256' 'SKIP' '未随包提供（发布版应带上，便于在任何设备上验证落地完整性）'
}

# ------------------------------------------------------------------ 4 注册 --
# 两个视图分开读，不用 HKCR\CLSID —— 它的视图取决于当前进程位数
$com64 = "HKLM:\SOFTWARE\Classes\CLSID\$Clsid\InProcServer32"
$com32 = "HKLM:\SOFTWARE\Classes\WOW6432Node\CLSID\$Clsid\InProcServer32"

if ($is64Os) {
    $v = Get-RegValue $com64
    if ($v) {
        $expect = Join-Path $InstallRoot ('x64\' + $TsfDll)
        $same = ($v -ieq $expect)
        Add-Result '注册' 'COM 64 位视图' $(if ($same -and (Test-Path -LiteralPath $v)) { 'PASS' } else { 'FAIL' }) $v
    } else {
        Add-Result '注册' 'COM 64 位视图' 'FAIL' ("缺少 {0}" -f $com64)
    }
}
$v32 = Get-RegValue $com32
if ($v32) {
    $expect32 = Join-Path $InstallRoot ('x86\' + $TsfDll)
    $same32 = ($v32 -ieq $expect32)
    Add-Result '注册' 'COM 32 位视图' $(if ($same32 -and (Test-Path -LiteralPath $v32)) { 'PASS' } else { 'FAIL' }) $v32
} else {
    Add-Result '注册' 'COM 32 位视图' 'FAIL' ("缺少 {0}" -f $com32)
}

$tipBase64 = "HKLM:\SOFTWARE\Microsoft\CTF\TIP\$Clsid"
$prof64 = Join-Path (Join-Path $tipBase64 'LanguageProfile') (Join-Path $LangId $ProfileGuid)
if (Test-RegKey $prof64) {
    Add-Result '注册' 'TSF 语言配置文件（64 位视图）' 'PASS' $prof64
} else {
    Add-Result '注册' 'TSF 语言配置文件（64 位视图）' 'FAIL' '未注册 —— Win+Space 列表里不会出现本输入法'
}
$tipBase32 = "HKLM:\SOFTWARE\WOW6432Node\Microsoft\CTF\TIP\$Clsid"
$prof32 = Join-Path (Join-Path $tipBase32 'LanguageProfile') (Join-Path $LangId $ProfileGuid)
Add-Result '注册' 'TSF 语言配置文件（32 位视图）' $(if (Test-RegKey $prof32) { 'PASS' } else { 'WARN' }) '缺少它时 32 位应用里不可用'

$arpName = Get-RegValue $ArpKey 'DisplayName'
if ($arpName) {
    $arpVer = Get-RegValue $ArpKey 'DisplayVersion'
    $arpLoc = Get-RegValue $ArpKey 'InstallLocation'
    $arpUn = Get-RegValue $ArpKey 'UninstallString'
    $verdict = 'PASS'
    $note = ("{0} {1} -> {2}" -f $arpName, $arpVer, $arpLoc)
    if ($arpLoc -and $arpLoc.TrimEnd('\') -ine $InstallRoot.TrimEnd('\')) {
        $verdict = 'WARN'; $note += '（与当前安装目录不一致）'
    }
    if ($arpName -notlike "*$ProductName*") {
        $verdict = 'WARN'; $note += '（DisplayName 与本产品不符）'
    }
    Add-Result '注册' '应用和功能条目' $verdict $note
    if ($arpUn) {
        $unPath = $arpUn.Trim('"').Split(' ')[0]
        Add-Result '注册' '卸载程序存在' $(if (Test-Path -LiteralPath $unPath) { 'PASS' } else { 'FAIL' }) $unPath
    }
} else {
    Add-Result '注册' '应用和功能条目' 'WARN' '没有 ARP 条目：脚本安装（Install-Zuxia.ps1）本来就不写，安装包安装则会写'
}

$pKeyVer = Get-RegValue $ProductKey 'Version'
Add-Result '注册' 'SOFTWARE\Zuxia' $(if ($pKeyVer) { 'PASS' } else { 'WARN' }) ("Version={0}" -f $pKeyVer)

# --------------------------------------------------------- 5 兄弟产品完好 --
$sibRoot = Join-Path $env:ProgramFiles 'Yingwu'
$sibCom = "HKLM:\SOFTWARE\Classes\WOW6432Node\CLSID\$SiblingClsid\InProcServer32"
$sibCom64 = "HKLM:\SOFTWARE\Classes\CLSID\$SiblingClsid\InProcServer32"
if ((Test-Path -LiteralPath $sibRoot) -or (Get-RegValue $sibCom64) -or (Get-RegValue $sibCom)) {
    $sibDll64 = Get-RegValue $sibCom64
    $sibOk = $true; $why = @()
    if (-not (Test-Path -LiteralPath $sibRoot)) { $sibOk = $false; $why += '没找到安装目录' } else {
        if (-not (Test-Path -LiteralPath (Join-Path $sibRoot "x64\$SiblingDll"))) { $sibOk = $false; $why += 'x64 DLL 不在' }
        if (-not (Test-Path -LiteralPath (Join-Path $sibRoot "x86\$SiblingDll"))) { $sibOk = $false; $why += 'x86 DLL 不在' }
    }
    if ($is64Os -and -not $sibDll64) { $sibOk = $false; $why += '64 位注册缺失' }
    Add-Result '并存' '应物输入法（兄弟产品）完好' $(if ($sibOk) { 'PASS' } else { 'FAIL' }) ($(if ($sibOk) { '注册与文件都在' } else { $why -join '；' }))
} else {
    Add-Result '并存' '应物输入法（兄弟产品）' 'SKIP' '本机未安装应物'
}

# ------------------------------------------------------------ 6 用户数据 --
$userRoot = Join-Path $env:LOCALAPPDATA 'Zuxia'
$rimeDir = Join-Path $userRoot 'Rime'
if (Test-Path -LiteralPath $userRoot) {
    $writable = $true
    try {
        $probe = Join-Path $userRoot ('.write-probe-' + [guid]::NewGuid().ToString('n'))
        [IO.File]::WriteAllText($probe, 'x'); Remove-Item -LiteralPath $probe -Force
    } catch { $writable = $false }
    Add-Result '用户数据' '目录可写' $(if ($writable) { 'PASS' } else { 'FAIL' }) $userRoot

    $bins = @()
    $buildDir = Join-Path $rimeDir 'build'
    if (Test-Path -LiteralPath $buildDir) { $bins = @(Get-ChildItem -LiteralPath $buildDir -Filter '*.bin' -ErrorAction SilentlyContinue) }
    Add-Result '用户数据' '词库已编译' $(if ($bins.Count -ge 2) { 'PASS' } else { 'WARN' }) (
        $(if ($bins.Count -ge 2) { "{0} 个 .bin（首次输入不会再等编译）" -f $bins.Count } else { '尚无 .bin：第一次切换本输入法时会现编译词库，可能等数秒' }))
} else {
    Add-Result '用户数据' '目录' 'SKIP' '尚未创建（第一次实际输入时才会建）'
}

$log = Join-Path $userRoot 'zuxia.log'
if (Test-Path -LiteralPath $log) {
    $bad = @(Select-String -LiteralPath $log -Pattern 'engine-failed|activate-failed|exception|failed|not-found' -ErrorAction SilentlyContinue)
    Add-Result '运行日志' '无失败事件' $(if ($bad.Count -eq 0) { 'PASS' } else { 'FAIL' }) (
        $(if ($bad.Count -eq 0) { $log } else { ("{0} 条失败记录，例如：{1}" -f $bad.Count, $bad[-1].Line.Trim()) }))
} else {
    Add-Result '运行日志' '日志文件' 'SKIP' '还没有日志（本输入法尚未被激活过）'
}

# ------------------------------------------------------------ 7 引擎自检 --
if ($EngineTestExe -and (Test-Path -LiteralPath $EngineTestExe)) {
    $sandbox = Join-Path $env:TEMP ('zuxia-selftest-' + [guid]::NewGuid().ToString('n').Substring(0, 8))
    try {
        New-Item -ItemType Directory -Force -Path $sandbox | Out-Null
        foreach ($a in $archDirs) {
            $src = Join-Path $InstallRoot $a
            if (-not (Test-Path -LiteralPath $src)) { continue }
            New-Item -ItemType Directory -Force -Path (Join-Path $sandbox $a) | Out-Null
            Copy-Item -LiteralPath (Join-Path $src '*') -Destination (Join-Path $sandbox $a) -Recurse -Force
        }
        Copy-Item -LiteralPath (Join-Path $InstallRoot 'data') -Destination (Join-Path $sandbox 'data') -Recurse -Force
        $target = if ($is64Os) { Join-Path $sandbox 'x64' } else { Join-Path $sandbox 'x86' }
        $exe = Join-Path $target 'engine-test.exe'
        Copy-Item -LiteralPath $EngineTestExe -Destination $exe -Force
        Push-Location $target
        $out = & $exe 2>&1
        $code = $LASTEXITCODE
        Pop-Location
        $tail = ($out | Select-Object -Last 1)
        Add-Result '引擎' '无界面端到端自检' $(if ($code -eq 0) { 'PASS' } else { 'FAIL' }) ("退出码 {0}；{1}" -f $code, $tail)
    } catch {
        Add-Result '引擎' '无界面端到端自检' 'FAIL' $_.Exception.Message
    } finally {
        Remove-Item -LiteralPath $sandbox -Recurse -Force -ErrorAction SilentlyContinue
    }
} else {
    Add-Result '引擎' '无界面端到端自检' 'SKIP' '未提供 -EngineTestExe（在源码树里用 pwsh tools\build-engine-test.ps1 生成）'
}

# ------------------------------------------------------------------ 汇总 --
Write-Host ''
foreach ($group in ($results | Select-Object -ExpandProperty Group -Unique)) {
    Write-Host ("[{0}]" -f $group) -ForegroundColor White
    foreach ($r in ($results | Where-Object { $_.Group -eq $group })) {
        $color = 'Gray'
        switch ($r.Status) {
            'PASS' { $color = 'Green' }
            'FAIL' { $color = 'Red' }
            'WARN' { $color = 'Yellow' }
            'SKIP' { $color = 'DarkGray' }
        }
        $line = "  {0,-4} {1,-34} {2}" -f $r.Status, $r.Item, $r.Detail
        Write-Host $line -ForegroundColor $color
    }
}

$fails = @($results | Where-Object { $_.Status -eq 'FAIL' })
$warns = @($results | Where-Object { $_.Status -eq 'WARN' })
Write-Host ''
Write-Host ('=' * 74)
$summary = "结论：{0}  失败 {1}  警告 {2}  共 {3} 项" -f $(if ($fails.Count -eq 0) { '本机可安装可用' } else { '本机存在问题' }), $fails.Count, $warns.Count, $results.Count
Write-Host $summary -ForegroundColor $(if ($fails.Count -eq 0) { 'Green' } else { 'Red' })

if ($JsonPath) {
    $report = [pscustomobject]@{
        product     = $ProductName
        clsid       = $Clsid
        machine     = $env:COMPUTERNAME
        os          = $osCaption
        osBuild     = $osBuild
        osArch      = $(if ($archWow) { $archWow } else { $arch })
        is64BitOs   = $is64Os
        isArm64     = $isArm64
        processArch = $arch
        powershell  = $psv
        elevated    = $elevated
        installRoot = $InstallRoot
        timestamp   = (Get-Date).ToString('s')
        failures    = $fails.Count
        warnings    = $warns.Count
        checks      = $results
    }
    $report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $JsonPath -Encoding UTF8
    Write-Host ("JSON 报告：{0}" -f $JsonPath) -ForegroundColor Cyan
}

if ($fails.Count -gt 0) { exit 1 }
exit 0
