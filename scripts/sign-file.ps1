<#
.SYNOPSIS
    用 signtool 给一个出货二进制签名并回验。

.DESCRIPTION
    为什么必须签：未签名的安装包在 Windows 11 上可能被 Smart App Control 直接拦下
    （不是警告，是拒绝安装），在企业里会被 WDAC/AppLocker 挡掉，下载来的还会带
    Mark-of-the-Web 触发 SmartScreen「未知发布者」。**没有证书，「在任何 Windows
    设备上都能安装」这句话不成立**——这是采购问题，不是代码问题。

    证书就位后，这个脚本由 installer\setup\make-setup.ps1 -Sign 在三个位置调用，
    顺序是有讲究的：

      1. 两个 ZuxiaTSF.dll —— 必须在打 cabinet **之前**签，否则进包的还是未签名件；
      2. ZuxiaUninstall.exe —— 必须在嵌进 setup 资源**之前**签；它会被写进安装目录
         并挂在「应用和功能」的卸载入口上，一个未签名的 exe 在那里是最招杀软的；
      3. ZuxiaSetup.exe —— 最后签，它是用户双击的那个。

    证书来源二选一：

      * PFX 文件：  -Pfx C:\keys\zuxia.pfx   （口令从 ZX_SIGN_PFX_PASSWORD 或 -PfxPassword）
      * 证书存储：  -Thumbprint <指纹>        （含 EV 硬件令牌 / 云签名商的 CSP）

    口令尽量走环境变量，不要写在命令行上——命令行会进进程表和 shell 历史。

.PARAMETER Path
    要签的文件，可以给多个。

.PARAMETER TimestampUrl
    RFC3161 时间戳服务。**必须带时间戳**：否则证书一过期，已发布的签名就全部失效。

.EXAMPLE
    $env:ZX_SIGN_PFX = 'C:\keys\zuxia.pfx'
    $env:ZX_SIGN_PFX_PASSWORD = '...'
    pwsh installer\setup\make-setup.ps1 -Sign

.EXAMPLE
    # 单独签一个文件，证书在存储里
    pwsh scripts\sign-file.ps1 -Path dist\Zuxia\x64\ZuxiaTSF.dll -Thumbprint ABC123...
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string[]]$Path,

    [string]$Pfx = $env:ZX_SIGN_PFX,
    [string]$PfxPassword = $env:ZX_SIGN_PFX_PASSWORD,
    [string]$Thumbprint = $env:ZX_SIGN_THUMBPRINT,
    [string]$TimestampUrl = $(if ($env:ZX_SIGN_TIMESTAMP) { $env:ZX_SIGN_TIMESTAMP } else { 'http://timestamp.digicert.com' }),
    [switch]$NoVerify
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# A self-signed certificate cannot pass `verify /pa` -- there is no chain to
# trust, which is exactly why it must never be shipped. Setting
# ZX_SIGN_NOVERIFY=1 lets a maintainer still exercise the release plumbing
# (that the right files get signed at the right moments) without a purchased
# certificate. Never set it for a release.
if ($env:ZX_SIGN_NOVERIFY -eq '1') { $NoVerify = $true }

function Find-SignTool {
    $fromPath = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($fromPath) { return $fromPath.Source }

    $kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    if (Test-Path $kits) {
        $candidates = Get-ChildItem -Path $kits -Filter signtool.exe -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\x64\\' } |
            Sort-Object FullName -Descending
        if ($candidates) { return $candidates[0].FullName }
    }
    throw 'signtool.exe not found. Install the Windows SDK (Windows Kits\10\bin\x64\signtool.exe) or put it on PATH.'
}

if (-not $Pfx -and -not $Thumbprint) {
    throw @'
没有可用的签名证书。二选一：

  $env:ZX_SIGN_PFX          = 'C:\keys\zuxia.pfx'
  $env:ZX_SIGN_PFX_PASSWORD = '<口令>'
或
  $env:ZX_SIGN_THUMBPRINT   = '<证书指纹>'

证书需要购买：OV 代码签名证书（约千元级/年）足以消除 SmartScreen 的「未知发布者」
并在大多数企业环境通过；要让 SmartScreen 信誉立刻生效、或在 Smart App Control
下也稳定可用，选 EV（数千元/年，配硬件令牌或云签名）。自签名证书**不能**分发——
它只能用来验证这条流水线本身是通的。
'@
}

$signTool = Find-SignTool
$common = @('sign', '/fd', 'SHA256', '/tr', $TimestampUrl, '/td', 'SHA256', '/v')
if ($Pfx) {
    $common += @('/f', $Pfx)
    if ($PfxPassword) { $common += @('/p', $PfxPassword) }
} else {
    $common += @('/sha1', $Thumbprint)
}

$failed = 0
foreach ($item in $Path) {
    if (-not (Test-Path -LiteralPath $item)) {
        Write-Host ("  跳过（不存在）：{0}" -f $item) -ForegroundColor Yellow
        $failed++
        continue
    }
    $full = (Resolve-Path -LiteralPath $item).Path
    Write-Host ("  签名 {0}" -f (Split-Path $full -Leaf)) -ForegroundColor Cyan
    & $signTool @common $full | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host ("    signtool 失败，退出码 {0}" -f $LASTEXITCODE) -ForegroundColor Red
        $failed++
        continue
    }
    if (-not $NoVerify) {
        & $signTool @('verify', '/pa', '/v', $full) | Out-Null
        if ($LASTEXITCODE -ne 0) {
            Write-Host '    签名后回验失败' -ForegroundColor Red
            $failed++
            continue
        }
        Write-Host '    已签名并回验通过' -ForegroundColor Green
    }
}

if ($failed -gt 0) { exit 1 }
exit 0
