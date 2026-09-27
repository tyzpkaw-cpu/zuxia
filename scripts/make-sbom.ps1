<#
.SYNOPSIS
    为已暂存的载荷生成 SPDX 2.3 JSON 格式的 SBOM。

.DESCRIPTION
    第三方审计与企业采购都会要这份东西：发布物里到底有哪些第三方组件、各自什么许可、
    每个文件的哈希是多少。应物的工程方案 §7.3 里写了「发布前固定源文件哈希并生成 SBOM」，
    但没有实现——这里补上。

    输出是确定性的：文件按路径排序、时间戳取自 -Created（默认固定值），
    所以同一份载荷每次生成的 SBOM 逐字节相同，可以和二进制一起进版本库。

.PARAMETER Stage
    已暂存的载荷目录，默认 dist\Zuxia。

.PARAMETER Version
    版本号，默认读 VERSION 文件。

.PARAMETER Out
    输出路径，默认 dist\Zuxia-<版本>.spdx.json。

.EXAMPLE
    pwsh scripts/make-sbom.ps1
#>
[CmdletBinding()]
param(
    [string]$Stage = '',
    [string]$Version = '',
    [string]$Out = '',
    [string]$Created = '2020-01-01T00:00:00Z'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Root = Split-Path -Parent $PSScriptRoot
if (-not $Stage)   { $Stage = Join-Path $Root 'dist\Zuxia' }
if (-not $Version) { $Version = (Get-Content (Join-Path $Root 'VERSION') -Raw).Trim() }
if (-not $Out)     { $Out = Join-Path $Root ("dist\Zuxia-{0}.spdx.json" -f $Version) }

if (-not (Test-Path $Stage)) { throw "Nothing staged at $Stage. Run scripts\build.ps1 first." }

$ProductName = '应物音形足下输入法 (Zuxia IME)'
$Namespace   = "https://spdx.invalid/zuxia-ime/$Version"

# 第三方组件。许可依据 THIRD_PARTY_NOTICES.md 与 licenses\ 下的原文。
# match 决定载荷里的文件归属哪个组件；其余归产品自身（自研代码，MIT）。
$components = @(
    [pscustomobject]@{
        Id = 'SPDXRef-Package-librime'; Name = 'librime'; Version = '1.17.0'
        License = 'BSD-3-Clause'
        Supplier = 'Organization: Rime community'
        Homepage = 'https://github.com/rime/librime'
        Comment = '官方 Windows MSVC SDK（commit 33e7814）随源码包附带；运行库 rime.dll'
        Match = 'rime\.dll$'
    },
    [pscustomobject]@{
        Id = 'SPDXRef-Package-rime-ice'; Name = 'rime-ice (雾凇拼音) 数据'; Version = 'n/a'
        License = 'GPL-3.0-only'
        Supplier = 'Organization: rime-ice contributors'
        Homepage = 'https://github.com/iDvel/rime-ice'
        Comment = '单字拼音与固定权重派生自该数据；data\*.dict.yaml 为其转换产物'
        Match = 'data/zuxia(_char_codes)?\.dict\.yaml$'
    },
    [pscustomobject]@{
        Id = 'SPDXRef-Package-makemeahanzi'; Name = 'Make Me a Hanzi'; Version = 'n/a'
        License = 'LGPL-3.0-or-later'
        Supplier = 'Person: Shaunak Kishore'
        Homepage = 'https://github.com/skishore/makemeahanzi'
        Comment = '汉字结构、部件与读音元数据；经生成器转换为编码数据'
        Match = '^$'
    },
    [pscustomobject]@{
        Id = 'SPDXRef-Package-cjkvi-ids'; Name = 'CJKVI IDS'; Version = 'n/a'
        License = 'NOASSERTION'
        Supplier = 'Organization: cjkvi'
        Homepage = 'https://github.com/cjkvi/cjkvi-ids'
        Comment = 'IDS 结构描述；子数据来源条款需逐项复核'
        Match = '^$'
    },
    [pscustomobject]@{
        Id = 'SPDXRef-Package-msft-tsf-sample'; Name = 'Microsoft TSF sample'; Version = 'textservice-step06-3'
        License = 'MIT'
        Supplier = 'Organization: Microsoft Corporation'
        Homepage = 'https://github.com/microsoft/Windows-classic-samples'
        Comment = 'COM/TSF 框架起点'
        Match = '^$'
    },
    [pscustomobject]@{
        Id = 'SPDXRef-Package-source-han-sans'; Name = 'Source Han Sans (思源黑体)'; Version = 'n/a'
        License = 'OFL-1.1'
        Supplier = 'Organization: Adobe'
        Homepage = 'https://github.com/adobe-fonts/source-han-sans'
        Comment = '图标字形渲染来源；渲染结果已嵌入 DLL，字体本身不再分发'
        Match = '^$'
    }
)

# ------------------------------------------------------------------ 文件 --
$files = Get-ChildItem $Stage -Recurse -File | Sort-Object FullName
$fileEntries = New-Object System.Collections.ArrayList
$verificationHashes = New-Object System.Collections.ArrayList

foreach ($file in $files) {
    $relative = $file.FullName.Substring($Stage.Length).TrimStart('\') -replace '\\', '/'
    $sha256 = (Get-FileHash $file.FullName -Algorithm SHA256).Hash.ToLower()
    $sha1   = (Get-FileHash $file.FullName -Algorithm SHA1).Hash.ToLower()
    [void]$verificationHashes.Add($sha1)

    $owner = 'SPDXRef-Package-zuxia'
    foreach ($component in $components) {
        if ($relative -match $component.Match) { $owner = $component.Id; break }
    }

    $license = 'MIT'
    if ($owner -ne 'SPDXRef-Package-zuxia') {
        $license = ($components | Where-Object { $_.Id -eq $owner }).License
    }
    if ($relative -like 'docs/licenses/*' -or $relative -eq 'docs/THIRD_PARTY_NOTICES.md') {
        $license = 'NOASSERTION'      # 许可证原文本身，不主张许可
    }
    if ($relative -eq 'MANIFEST.sha256') { $license = 'CC0-1.0' }

    [void]$fileEntries.Add([pscustomobject]@{
        SPDXID           = 'SPDXRef-File-' + ($relative -replace '[^A-Za-z0-9\.\-]', '-')
        fileName         = './' + $relative
        checksums        = @([pscustomobject]@{ algorithm = 'SHA256'; checksumValue = $sha256 },
                             [pscustomobject]@{ algorithm = 'SHA1';   checksumValue = $sha1 })
        licenseConcluded = $license
        licenseInfoInFiles = @($license)
        copyrightText    = 'NOASSERTION'
    })
}

# SPDX 包校验码：各文件 SHA1 的十六进制串排序后拼接，再取 SHA1。
$joined = (($verificationHashes | Sort-Object) -join '')
$sha1OfJoins = [System.Security.Cryptography.SHA1]::Create()
$verificationCode = ([BitConverter]::ToString(
    $sha1OfJoins.ComputeHash([Text.Encoding]::ASCII.GetBytes($joined)))).Replace('-', '').ToLower()

# ------------------------------------------------------------------ 组装 --
$packages = New-Object System.Collections.ArrayList
[void]$packages.Add([pscustomobject]@{
    SPDXID            = 'SPDXRef-Package-zuxia'
    name              = $ProductName
    versionInfo       = $Version
    downloadLocation  = 'NOASSERTION'
    filesAnalyzed     = $true
    packageVerificationCode = [pscustomobject]@{ packageVerificationCodeValue = $verificationCode }
    licenseConcluded  = 'MIT'
    licenseDeclared   = 'MIT'
    copyrightText     = 'NOASSERTION'
    supplier          = 'Organization: 应物音形足下输入法'
    primaryPackagePurpose = 'APPLICATION'
    comment           = 'Windows TSF 输入法；自研代码 MIT，数据与运行库许可见 THIRD_PARTY_NOTICES.md'
})
foreach ($component in $components) {
    [void]$packages.Add([pscustomobject]@{
        SPDXID           = $component.Id
        name             = $component.Name
        versionInfo      = $component.Version
        downloadLocation = $component.Homepage
        filesAnalyzed    = $false
        licenseConcluded = $component.License
        licenseDeclared  = $component.License
        copyrightText    = 'NOASSERTION'
        supplier         = $component.Supplier
        comment          = $component.Comment
    })
}

$relationships = New-Object System.Collections.ArrayList
[void]$relationships.Add([pscustomobject]@{ spdxElementId = 'SPDXRef-DOCUMENT'; relationshipType = 'DESCRIBES'; relatedSpdxElement = 'SPDXRef-Package-zuxia' })
foreach ($component in $components) {
    [void]$relationships.Add([pscustomobject]@{ spdxElementId = 'SPDXRef-Package-zuxia'; relationshipType = 'CONTAINS'; relatedSpdxElement = $component.Id })
}
# 数据文件是外部数据的转换产物，按 CONTAINS 之外的 GENERATED_FROM 关系如实登记。
[void]$relationships.Add([pscustomobject]@{ spdxElementId = 'SPDXRef-Package-zuxia'; relationshipType = 'GENERATED_FROM'; relatedSpdxElement = 'SPDXRef-Package-rime-ice' })

$document = [pscustomobject]@{
    spdxVersion       = 'SPDX-2.3'
    dataLicense       = 'CC0-1.0'
    SPDXID            = 'SPDXRef-DOCUMENT'
    name              = "Zuxia-IME-$Version"
    documentNamespace = $Namespace
    creationInfo      = [pscustomobject]@{
        created  = $Created
        creators = @('Tool: scripts/make-sbom.ps1')
        comment  = '确定性输出：同一份载荷每次生成的 SBOM 逐字节相同'
    }
    packages      = $packages
    files         = $fileEntries
    relationships = $relationships
}

New-Item -ItemType Directory -Path (Split-Path -Parent $Out) -Force | Out-Null
$document | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Out -Encoding UTF8

Write-Host ''
Write-Host ("SBOM: {0}" -f $Out) -ForegroundColor Green
Write-Host ("  {0} 个文件、{1} 个包、包校验码 {2}" -f $fileEntries.Count, $packages.Count, $verificationCode)
Write-Host ("  SHA-256 {0}" -f (Get-FileHash $Out -Algorithm SHA256).Hash.ToLower())
