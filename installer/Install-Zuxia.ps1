[CmdletBinding()]
param(
    [string]$SourceRoot = '',
    [string]$TargetRoot = "$env:ProgramFiles\Zuxia"
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-Administrator)) {
    $arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`""
    if ($SourceRoot) { $arguments += " -SourceRoot `"$SourceRoot`"" }
    if ($TargetRoot) { $arguments += " -TargetRoot `"$TargetRoot`"" }
    $process = Start-Process -FilePath "$PSHOME\powershell.exe" -Verb RunAs `
        -ArgumentList $arguments -Wait -PassThru
    exit $process.ExitCode
}

if (-not $SourceRoot) {
    $SourceRoot = Join-Path (Split-Path -Parent $PSScriptRoot) 'dist\Zuxia'
}
$SourceRoot = [IO.Path]::GetFullPath($SourceRoot)
$TargetRoot = [IO.Path]::GetFullPath($TargetRoot)

function Invoke-RegSvr32 {
    param([string]$Exe, [string]$Dll, [switch]$Unregister)
    if (-not (Test-Path $Exe) -or -not (Test-Path $Dll)) { return }
    $arguments = if ($Unregister) {
        "/u /s `"$Dll`""
    } else {
        "/s `"$Dll`""
    }
    $process = Start-Process -FilePath $Exe -ArgumentList $arguments `
        -Wait -PassThru
    if ($process.ExitCode -ne 0) {
        throw "regsvr32 failed with exit code $($process.ExitCode): $Dll"
    }
}

$required = @(
    (Join-Path $SourceRoot 'x64\ZuxiaTSF.dll'),
    (Join-Path $SourceRoot 'x64\rime.dll'),
    (Join-Path $SourceRoot 'data\zuxia.schema.yaml'),
    (Join-Path $SourceRoot 'data\zuxia.dict.yaml'),
    (Join-Path $SourceRoot 'data\zuxia_char_codes.dict.yaml')
)
foreach ($file in $required) {
    if (-not (Test-Path $file)) {
        throw "Missing build output: $file. Run scripts\build.ps1 first."
    }
}

$reg64 = Join-Path $env:WINDIR 'System32\regsvr32.exe'
$reg32 = Join-Path $env:WINDIR 'SysWOW64\regsvr32.exe'

# Unregister a previous copy before replacing files. Failures are non-fatal
# because the previous installation may be incomplete.
if (Test-Path (Join-Path $TargetRoot 'x86\ZuxiaTSF.dll')) {
    try { Invoke-RegSvr32 $reg32 (Join-Path $TargetRoot 'x86\ZuxiaTSF.dll') -Unregister } catch {}
}
if (Test-Path (Join-Path $TargetRoot 'x64\ZuxiaTSF.dll')) {
    try { Invoke-RegSvr32 $reg64 (Join-Path $TargetRoot 'x64\ZuxiaTSF.dll') -Unregister } catch {}
}

$staging = "$TargetRoot.new"
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Path $staging -Force | Out-Null
Copy-Item (Join-Path $SourceRoot '*') $staging -Recurse -Force

if (Test-Path $TargetRoot) { Remove-Item $TargetRoot -Recurse -Force }
Move-Item $staging $TargetRoot

try {
    # Register 32-bit first and 64-bit last so the shared profile icon points
    # to the native 64-bit DLL on a 64-bit system.
    $x86Dll = Join-Path $TargetRoot 'x86\ZuxiaTSF.dll'
    if (Test-Path $x86Dll) { Invoke-RegSvr32 $reg32 $x86Dll }
    Invoke-RegSvr32 $reg64 (Join-Path $TargetRoot 'x64\ZuxiaTSF.dll')
} catch {
    try { Invoke-RegSvr32 $reg32 (Join-Path $TargetRoot 'x86\ZuxiaTSF.dll') -Unregister } catch {}
    try { Invoke-RegSvr32 $reg64 (Join-Path $TargetRoot 'x64\ZuxiaTSF.dll') -Unregister } catch {}
    throw
}

Write-Host ''
Write-Host 'Zuxia IME was installed successfully.' -ForegroundColor Green
Write-Host 'Use Win+Space and select 应物音形足下输入法. The first activation may take a few seconds while dictionaries are compiled.'
Write-Host 'If it is not visible immediately, sign out of Windows and sign in again.'
