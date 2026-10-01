# scripts/New-ZuxiaRelease.ps1
# 用法（PowerShell 5.1）：
#   $env:GH_TOKEN = "ghp_xxxx"
#   pwsh scripts\New-ZuxiaRelease.ps1
#
# 会自动：读 VERSION 文件、建 tag、建 Pre-release，触发 CI 构建安装包。
# 不需要打开浏览器，不需要点任何按钮。

param(
    [string]$Token = $env:GH_TOKEN
)
if (-not $Token) { throw "请先设置 `$env:GH_TOKEN 或通过 -Token 传入 GitHub PAT" }

$repo  = "tyzpkaw-cpu/zuxia"
$ver   = (Get-Content "$PSScriptRoot\..\VERSION" -Raw).Trim()
$tag   = "v$ver"
$hdrs  = @{ Authorization = "token $Token"; "Content-Type" = "application/json" }
$base  = "https://api.github.com/repos/$repo"

Write-Host "版本: $tag"

# 1. 取 master 最新 SHA
$sha = (Invoke-RestMethod "$base/git/ref/heads/master" -Headers $hdrs).object.sha
Write-Host "master: $($sha.Substring(0,12))"

# 2. 建 tag（已存在则跳过）
try {
    Invoke-RestMethod "$base/git/refs" -Method POST -Headers $hdrs `
        -Body (ConvertTo-Json @{ ref = "refs/tags/$tag"; sha = $sha }) | Out-Null
    Write-Host "tag 已创建: $tag"
} catch {
    if ($_.Exception.Response.StatusCode.value__ -eq 422) {
        Write-Host "tag 已存在，跳过"
    } else { throw }
}

# 3. 建 Release
$rel = Invoke-RestMethod "$base/releases" -Method POST -Headers $hdrs `
    -Body (ConvertTo-Json @{
        tag_name         = $tag
        target_commitish = $sha
        name             = $tag
        prerelease       = $true
        draft            = $false
        body             = ""
    })
Write-Host "Release 已创建: $($rel.html_url)"
Write-Host "CI 约 8 分钟后把安装包挂上去。"
