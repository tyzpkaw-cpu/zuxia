[CmdletBinding()]
param(
    [ValidateSet('x64', 'x86')]
    [string]$Arch = 'x64'
)

# Builds tools\engine-test.exe: the headless harness that drives the shipping
# RimeEngine against the real dictionaries, without a text service or a window.
#
# The result must be run from <root>\dist\Zuxia\x64 (or wherever it is copied
# beside rime.dll with a ..\data above it), because RimeEngine locates both
# from its own module path, exactly as the text service does.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Root = Split-Path -Parent $PSScriptRoot
$Tools = Join-Path $Root 'tools'
$ObjectDir = Join-Path $Root ("build\test-{0}" -f $Arch)

$vcvars = if ($Arch -eq 'x64') { 'vcvars64.bat' } else { 'vcvars32.bat' }
$hostArch = if ($Arch -eq 'x64') { 'Hostx64\x64' } else { 'Hostx64\x86' }

function Find-VcVars {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $install = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath
        if ($install) {
            $candidate = Join-Path $install "VC\Auxiliary\Build\$vcvars"
            if (Test-Path $candidate) { return $candidate }
        }
    }
    throw 'Visual Studio C++ build tools were not found. Install the "Desktop development with C++" workload.'
}

New-Item -ItemType Directory -Path $ObjectDir -Force | Out-Null

$vcvarsPath = Find-VcVars
$sources = @(
    'tools\engine-test.cpp',
    'src\RimeEngine.cpp',
    'src\Diagnostics.cpp',
    'src\Utf.cpp'
) | ForEach-Object { Join-Path $Root $_ }

# vcvars is a batch file because that is the only way MSVC ships its
# environment, but it is sourced into this process rather than wrapped in a
# generated .cmd: cmd.exe reads a batch file in the console's OEM code page, so
# an absolute path containing a Chinese user name (this repository's own path,
# say) comes back as question marks and cl.exe cannot find its sources.
# PowerShell passes arguments as UTF-16, which has no such problem.
$environment = & cmd.exe /c "call `"$vcvarsPath`" >nul 2>&1 && set"
foreach ($line in $environment) {
    $split = $line.IndexOf('=')
    if ($split -le 0) { continue }
    Set-Item -Path ("Env:" + $line.Substring(0, $split)) `
             -Value $line.Substring($split + 1)
}

$arguments = @(
    '/nologo', '/W4', '/std:c++17', '/EHsc', '/MT', '/O2', '/utf-8',
    '/DUNICODE', '/D_UNICODE',
    ('/I' + (Join-Path $Root 'src')),
    ('/I' + (Join-Path $Root "third_party\librime\$Arch\dist\include")),
    ('/Fo:' + $ObjectDir + '\')
) + $sources + @(
    ('/Fe:' + (Join-Path $Tools 'engine-test.exe')),
    '/link', '/SUBSYSTEM:CONSOLE', 'shell32.lib', 'user32.lib'
)

& cl.exe @arguments
if ($LASTEXITCODE -ne 0) { throw 'engine-test compilation failed.' }

Write-Host ''
Write-Host ("Built {0}" -f (Join-Path $Tools 'engine-test.exe'))
Write-Host 'Run it from a staged build directory, e.g.:'
Write-Host ("  cd `"{0}`"" -f (Join-Path $Root 'dist\Zuxia\x64'))
Write-Host '  ..\..\..\tools\engine-test.exe'
