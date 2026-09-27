[CmdletBinding()]
param(
    [ValidateSet('x64', 'x86', 'all')]
    [string]$Arch = 'all',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [string]$InstallPrefix = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Root = Split-Path -Parent $PSScriptRoot
if (-not $InstallPrefix) {
    $InstallPrefix = Join-Path $Root 'dist\Zuxia'
}

$architectures = if ($Arch -eq 'all') { @('x64', 'x86') } else { @($Arch) }
foreach ($item in $architectures) {
    $generatorArch = if ($item -eq 'x64') { 'x64' } else { 'Win32' }
    $buildDir = Join-Path $Root ("build\{0}" -f $item)
    $rimeRoot = Join-Path $Root ("third_party\librime\{0}\dist" -f $item)

    Write-Host "Configuring Zuxia $item..."
    & cmake -S $Root -B $buildDir -A $generatorArch `
        "-DZUXIA_LIBRIME_ROOT=$rimeRoot"
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed for $item." }

    Write-Host "Building Zuxia $item ($Configuration)..."
    & cmake --build $buildDir --config $Configuration --parallel
    if ($LASTEXITCODE -ne 0) { throw "Build failed for $item." }

    & cmake --install $buildDir --config $Configuration --prefix $InstallPrefix
    if ($LASTEXITCODE -ne 0) { throw "Install staging failed for $item." }
}

Write-Host "Build output: $InstallPrefix"
