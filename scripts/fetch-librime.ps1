[CmdletBinding()]
param([switch]$Force)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Root = Split-Path -Parent $PSScriptRoot
$Version = '1.17.0'
$Commit = '33e7814'
$Assets = @{
    x64 = @{
        Url = "https://github.com/rime/librime/releases/download/$Version/rime-$Commit-Windows-msvc-x64.7z"
        Sha256 = '7478c7caa4ff6b37de86daba1f7ce4a994a4f5ba24872a820fb2b3a9b01fed15'
    }
    x86 = @{
        Url = "https://github.com/rime/librime/releases/download/$Version/rime-$Commit-Windows-msvc-x86.7z"
        Sha256 = 'af235c26c06152ce09ceb8fe9d9ab9fba7ab43ce30aa952b40174d806f5cc3d9'
    }
}

$sevenZip = @(
    (Get-Command 7z.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -ErrorAction SilentlyContinue),
    "$env:ProgramFiles\7-Zip\7z.exe",
    "${env:ProgramFiles(x86)}\7-Zip\7z.exe"
) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $sevenZip) {
    throw '7-Zip was not found. Install 7-Zip, then rerun this script.'
}

foreach ($arch in @('x64', 'x86')) {
    $target = Join-Path $Root "third_party\librime\$arch"
    $archive = Join-Path $env:TEMP "zuxia-librime-$arch.7z"
    if ($Force -or -not (Test-Path (Join-Path $target 'dist\lib\rime.dll'))) {
        Write-Host "Downloading librime $Version $arch..."
        Invoke-WebRequest -Uri $Assets[$arch].Url -OutFile $archive
        $actual = (Get-FileHash -Algorithm SHA256 $archive).Hash.ToLowerInvariant()
        if ($actual -ne $Assets[$arch].Sha256) {
            Remove-Item $archive -Force
            throw "SHA-256 mismatch for librime $arch."
        }
        if (Test-Path $target) { Remove-Item $target -Recurse -Force }
        New-Item -ItemType Directory -Path $target -Force | Out-Null
        & $sevenZip x -y "-o$target" $archive | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "7-Zip extraction failed for $arch." }
        Remove-Item $archive -Force
    }
}
Write-Host 'librime SDKs are ready.'
