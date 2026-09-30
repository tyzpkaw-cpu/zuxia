[CmdletBinding()]
param(
    # Skip straight to packaging when dist\Zuxia is already staged.
    [switch]$SkipBuild,
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    # Sign every shipped binary. Certificate configuration comes from the
    # environment -- see scripts\sign-file.ps1. Without a certificate an
    # installer cannot be promised to run on every Windows device.
    [switch]$Sign
)

# Produces ZuxiaSetup.exe: a single 32-bit installer carrying the x64 and x86
# text services, the Rime data and the uninstaller as an embedded cabinet.
#
# Everything it needs is generated here, so the repository holds no absolute
# paths and no build products.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$SetupDir = $PSScriptRoot
$Root = (Resolve-Path (Join-Path $SetupDir '..\..')).Path
$Stage = Join-Path $Root 'dist\Zuxia'
$Out = Join-Path $Root 'dist'

# Signing has to happen at three points, in this order: the text
# services before the cabinet is built, the uninstaller before it is
# embedded in the setup resources (build-installer.bat calls back into
# this path through ZX_SIGN_CMD), and the setup itself last.
$signScript = Join-Path $Root 'scripts\sign-file.ps1'
if ($Sign) {
    if (-not (Test-Path $signScript)) { throw "Missing $signScript." }
    $env:ZX_SIGN_CMD = $signScript
} else {
    Remove-Item Env:\ZX_SIGN_CMD -ErrorAction SilentlyContinue
}

function Get-RelativePath {
    param([string]$From, [string]$To)
    # [IO.Path]::GetRelativePath only exists on PowerShell 7 / .NET Core.
    if ([IO.Path].GetMethod('GetRelativePath', [Type[]]@([string], [string]))) {
        return [IO.Path]::GetRelativePath($From, $To)
    }
    $fromUri = New-Object System.Uri(($From.TrimEnd('\') + '\'))
    $toUri = New-Object System.Uri(($To.TrimEnd('\') + '\'))
    $relative = [Uri]::UnescapeDataString(
        $fromUri.MakeRelativeUri($toUri).ToString()).Replace('/', '\')
    return $relative.TrimEnd('\')
}

function Find-VcVars {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $install = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath
        if ($install) {
            $candidate = Join-Path $install 'VC\Auxiliary\Build\vcvars32.bat'
            if (Test-Path $candidate) { return $candidate }
        }
    }
    throw 'Visual Studio C++ build tools were not found. Install the "Desktop development with C++" workload.'
}

if (-not $SkipBuild) {
    Write-Host '[1/5] Building the text services...'
    & (Join-Path $Root 'scripts\build.ps1') -Arch all -Configuration $Configuration
}
if (-not (Test-Path (Join-Path $Stage 'x64\ZuxiaTSF.dll'))) {
    throw "Missing staged build at $Stage. Run scripts\build.ps1 first."
}

$version = (Get-Content (Join-Path $Root 'VERSION') -Raw).Trim()

# 版本号写在好几个地方。打包前先对一遍：出货件上印着一个号、安装界面显示
# 另一个，是最难追的那一类问题，而 setup.rc 里那一行手写的标题以前谁都不查。
foreach ($spot in @(
        @{ Path = 'installer\setup\zxcommon.h'; Pattern = 'ZX_VERSION\s+L"([^"]+)"' },
        @{ Path = 'installer\setup\setup.rc';   Pattern = '足下输入法 ([0-9][0-9.]*)' })) {
    $text = [IO.File]::ReadAllText((Join-Path $Root $spot.Path), [Text.Encoding]::UTF8)
    $found = [regex]::Match($text, $spot.Pattern)
    if (-not $found.Success) { throw "$($spot.Path) 里找不到版本号。" }
    if ($found.Groups[1].Value -ne $version) {
        throw "$($spot.Path) 写的是 $($found.Groups[1].Value)，VERSION 是 $version。"
    }
}

# 暂存目录是上一次构建留下来的，里面可能有不属于载荷的东西。CI 为了跑引擎
# 断言会把 engine-test.exe 拷进 dist\Zuxia\x64\，它一度就这么被装进了用户的
# Program Files。打包前清掉这一类文件；scripts\audit-payload.ps1 还会再查一遍。
$strays = @(Get-ChildItem $Stage -Recurse -File | Where-Object {
    $_.Name -match '(?i)(test.*\.exe$|\.pdb$|\.ilk$|\.exp$|\.log$|\.tmp$|\.new$)' })
foreach ($stray in $strays) {
    Write-Host ("      dropping non-payload file: " +
                $stray.FullName.Substring($Stage.Length).TrimStart('\'))
    Remove-Item $stray.FullName -Force
}

if ($Sign) {
    Write-Host '[1b/5] Signing the text services...'
    foreach ($arch in @('x64', 'x86')) {
        $dll = Join-Path $Stage "$arch\ZuxiaTSF.dll"
        if (-not (Test-Path $dll)) { continue }
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $signScript $dll
        if ($LASTEXITCODE -ne 0) { throw "Signing failed: $dll" }
    }
}

Write-Host '[2/5] Writing the build stamp and cabinet directive file...'

# 出货件必须能自证来自哪一次提交。少了这一条，任何人拿到 exe 都没法判断它
# 是不是当前代码编出来的 —— 外部复审就是在这里卡住的：他们分析的那个 0.2.0
# 少了一批已经进了 master 的修复，而包里没有任何线索。
#
# 只记确定性的事实（提交号、提交时间、工作区是否干净），不记构建时刻，
# 这样同一次提交重编出来的 cab 仍然逐字节相同。
function Get-GitFact {
    param([string[]]$GitArgs, [string]$Fallback = 'unknown')
    try {
        $text = & git -C $Root @GitArgs 2>$null
        if ($LASTEXITCODE -eq 0) {
            if ($null -eq $text) { return '' }
            return (($text -join "`n")).Trim()
        }
    } catch { }
    return $Fallback
}
$commit = Get-GitFact @('rev-parse', 'HEAD')
$commitUtc = Get-GitFact @('show', '-s', '--format=%cI', 'HEAD')
# --untracked-files=no：生成物（词组码表、解码器 tsv）本来就不进 git，
# 它们的存在不算「工作区脏」。被改过的源码才算。
$modified = Get-GitFact @('status', '--porcelain', '--untracked-files=no') ''
$buildInfo = @(
    'zuxia-build-info=1',
    "version=$version",
    "commit=$commit",
    "commit-utc=$commitUtc",
    ("tree=" + $(if ($modified) { 'dirty' } else { 'clean' }))
) -join "`n"
[IO.File]::WriteAllText((Join-Path $Stage 'BUILD-INFO.txt'), $buildInfo + "`n",
    [Text.Encoding]::ASCII)
Write-Host "      commit $commit ($(if ($modified) { 'dirty tree' } else { 'clean tree' }))"
# A manifest of the payload, written before the cabinet so that it
# travels inside it. It describes the files as staged, which are
# exactly the files the installer extracts -- so any device can prove
# afterwards that every one landed intact (scripts/verify-install.ps1).
# A stale manifest from an earlier run is dropped first: it must not be
# hashed into itself.
$manifestPath = Join-Path $Stage 'MANIFEST.sha256'
if (Test-Path $manifestPath) { Remove-Item $manifestPath -Force }
$files = Get-ChildItem $Stage -Recurse -File |
    Where-Object { $_.Extension -ne '.cab' -and $_.Name -ne 'MANIFEST.sha256' } |
    Sort-Object FullName
if (-not $files) { throw "No files staged under $Stage." }

# makecab reads the directive file in the system ANSI code page, so a
# non-ASCII member name reaches it as "????" and the cabinet fails to
# build -- with an error that says nothing about the real cause. Catch it
# here instead, and keep the payload locale-independent: the file names
# inside the cabinet must be ASCII whatever the build machine's code page.
$nonAscii = @($files | Where-Object {
    $_.FullName.Substring($Stage.Length) -match '[^\x00-\x7F]' })
if ($nonAscii.Count -gt 0) {
    $names = ($nonAscii | ForEach-Object { $_.FullName.Substring($Stage.Length) }) -join ', '
    throw ("Payload file names must be ASCII; makecab cannot package these: " + $names)
}

# The cabinet stores each file's own timestamp, so /Brepro on the
# linkers is not enough by itself: without a fixed stamp here, two
# builds of identical source still produce different cabinets. Every
# staged file gets one constant; the manifest gets it after writing.
$stamp = [datetime]::new(2020, 1, 1, 0, 0, 0, [DateTimeKind]::Utc)
foreach ($file in $files) { $file.LastWriteTimeUtc = $stamp }

$manifestLines = foreach ($file in $files) {
    $relative = $file.FullName.Substring($Stage.Length).TrimStart('\')
    $hash = (Get-FileHash $file.FullName -Algorithm SHA256).Hash.ToLower()
    "$hash *$relative"
}
# LF only: a trailing CR makes the name unreadable to sha256sum -c.
[IO.File]::WriteAllText($manifestPath, ($manifestLines -join "`n") + "`n",
    [Text.Encoding]::ASCII)
(Get-Item $manifestPath).LastWriteTimeUtc = $stamp

# Re-read so the manifest itself travels in the cabinet.
$files = Get-ChildItem $Stage -Recurse -File |
    Where-Object { $_.Extension -ne '.cab' } |
    Sort-Object FullName

# makecab parses the directive file in the system ANSI code page, so an
# absolute path containing non-ASCII characters (a Chinese user name, say)
# comes back mangled. Every path here is therefore relative to the staging
# directory, which makecab is run from, and stays pure ASCII.
$setupFromStage = Get-RelativePath -From $Stage -To $SetupDir

$ddf = New-Object System.Text.StringBuilder
[void]$ddf.AppendLine('.OPTION EXPLICIT')
[void]$ddf.AppendLine('.Set CabinetNameTemplate=payload.cab')
[void]$ddf.AppendLine(".Set DiskDirectory1=$setupFromStage")
[void]$ddf.AppendLine('.Set MaxDiskSize=0')
[void]$ddf.AppendLine('.Set CompressionType=LZX')
[void]$ddf.AppendLine('.Set CompressionMemory=21')
[void]$ddf.AppendLine('.Set Cabinet=on')
[void]$ddf.AppendLine('.Set Compress=on')
[void]$ddf.AppendLine('.Set UniqueFiles=off')
[void]$ddf.AppendLine('.Set InfFileName=nul')
[void]$ddf.AppendLine('.Set RptFileName=nul')
foreach ($file in $files) {
    $relative = $file.FullName.Substring($Stage.Length).TrimStart('\')
    [void]$ddf.AppendLine('"' + $relative + '" "' + $relative + '"')
}
$ddfPath = Join-Path $SetupDir 'payload.ddf'
[IO.File]::WriteAllText($ddfPath, $ddf.ToString(), [Text.Encoding]::ASCII)

Write-Host '[3/5] Compressing the payload...'
Push-Location $Stage
try {
    & makecab.exe /F (Join-Path $setupFromStage 'payload.ddf') |
        Select-Object -Last 4
    if ($LASTEXITCODE -ne 0) { throw "makecab failed with exit code $LASTEXITCODE." }
} finally { Pop-Location }

# The installer reports progress against the uncompressed payload size.
$total = ($files | Measure-Object -Property Length -Sum).Sum
$info = @(
    '// Generated by make-setup.ps1 - do not edit.',
    '#pragma once',
    ("#define ZX_PAYLOAD_FILES {0}" -f $files.Count),
    ("#define ZX_PAYLOAD_BYTES {0}ULL" -f $total)
) -join "`n"
[IO.File]::WriteAllText((Join-Path $SetupDir 'payload_info.h'), $info + "`n")
Write-Host ("      {0} files, {1:N0} bytes uncompressed" -f $files.Count, $total)

Write-Host '[4/5] Preparing the resource script...'
# rc.exe reads the Chinese literals reliably only from UTF-16.
$rcText = [IO.File]::ReadAllText((Join-Path $SetupDir 'setup.rc'), [Text.Encoding]::UTF8)
[IO.File]::WriteAllText((Join-Path $SetupDir 'setup.u16.rc'), $rcText,
    [Text.UnicodeEncoding]::new($false, $true))

Write-Host '[5/5] Compiling the installer...'
$vcvars = Find-VcVars
$batch = Join-Path $SetupDir 'build-installer.bat'
& cmd.exe /c "`"$vcvars`" >nul 2>&1 && `"$batch`""
if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }

New-Item -ItemType Directory -Path $Out -Force | Out-Null
$final = Join-Path $Out "ZuxiaSetup-$version.exe"
Copy-Item (Join-Path $SetupDir 'ZuxiaSetup.exe') $final -Force
$hash = (Get-FileHash $final -Algorithm SHA256).Hash.ToLower()
# LF only: a trailing CR makes the file name unreadable to `sha256sum -c`.
[IO.File]::WriteAllText("$final.sha256",
    "$hash *$(Split-Path $final -Leaf)`n", [Text.Encoding]::ASCII)

Write-Host ''
Write-Host ("Installer: {0} ({1:N2} MB)" -f $final, ((Get-Item $final).Length / 1MB))
Write-Host "SHA-256  : $hash"
