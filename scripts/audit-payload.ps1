[CmdletBinding()]
param()

# 出货前的硬门禁。
#
# CI 里原先这一步只是 Get-Content + Measure-Object：打印一下清单行数，然后
# 无论对不对都放行。外部复审据此判定「发布门禁形同虚设」——而且这不是理论
# 问题：用户实际装到的 0.2.0 并不是当时 master 编出来的，包里也没有任何
# 东西能证明它来自哪一次提交。
#
# 这里每一条都是断言，不满足就让这一次构建红掉。
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Stage = Join-Path $Root 'dist\Zuxia'
$Out = Join-Path $Root 'dist'
$version = (Get-Content (Join-Path $Root 'VERSION') -Raw).Trim()

$script:failures = New-Object System.Collections.Generic.List[string]
function Check {
    param([string]$What, [bool]$Ok, [string]$Detail = '')
    if ($Ok) {
        Write-Host "  ok   $What"
    } else {
        Write-Host "  FAIL $What $Detail"
        $script:failures.Add($What)
    }
}
function Git-Value {
    param([string[]]$GitArgs)
    try {
        $text = & git -C $Root @GitArgs 2>$null
        if ($LASTEXITCODE -ne 0) { return $null }
        if ($null -eq $text) { return '' }
        return (($text -join "`n")).Trim()
    } catch {
        return $null
    }
}

Write-Host "载荷门禁（版本 $version）"
Check '暂存目录存在' (Test-Path $Stage) $Stage
if (-not (Test-Path $Stage)) { throw "缺 $Stage" }

# --- 1. 出货件能自证来自哪一次提交 -----------------------------------------
$infoPath = Join-Path $Stage 'BUILD-INFO.txt'
Check 'BUILD-INFO.txt 在载荷里' (Test-Path $infoPath)
$info = @{}
if (Test-Path $infoPath) {
    foreach ($line in (Get-Content $infoPath)) {
        $at = $line.IndexOf('=')
        if ($at -gt 0) { $info[$line.Substring(0, $at)] = $line.Substring($at + 1) }
    }
}
Check 'BUILD-INFO 的版本号与 VERSION 相同' ($info['version'] -eq $version) `
      "实际 $($info['version'])"
$head = Git-Value @('rev-parse', 'HEAD')
Check 'BUILD-INFO 的 commit 就是当前 HEAD' ($head -and $info['commit'] -eq $head) `
      "BUILD-INFO=$($info['commit']) HEAD=$head"
Check 'BUILD-INFO 记的是干净工作区' ($info['tree'] -eq 'clean') `
      "实际 $($info['tree'])"
# pull_request 事件下 checkout 出来的是试合并提交，与 GITHUB_SHA 不必相同，
# 所以只在 push 事件上比这一条。
if ($env:GITHUB_SHA -and $env:GITHUB_EVENT_NAME -eq 'push') {
    Check 'BUILD-INFO 的 commit 与 GITHUB_SHA 相同' `
          ($info['commit'] -eq $env:GITHUB_SHA) `
          "BUILD-INFO=$($info['commit']) GITHUB_SHA=$($env:GITHUB_SHA)"
}

# --- 2. 清单逐条重算，不是只数行数 -----------------------------------------
$manifestPath = Join-Path $Stage 'MANIFEST.sha256'
Check 'MANIFEST.sha256 在载荷里' (Test-Path $manifestPath)
$declared = @{}
if (Test-Path $manifestPath) {
    foreach ($line in (Get-Content $manifestPath)) {
        if (-not $line.Trim()) { continue }
        $parts = $line -split ' \*', 2
        if ($parts.Count -eq 2) { $declared[$parts[1]] = $parts[0].ToLower() }
    }
}
$actual = @{}
foreach ($file in (Get-ChildItem $Stage -Recurse -File |
                   Where-Object { $_.Extension -ne '.cab' -and
                                  $_.Name -ne 'MANIFEST.sha256' })) {
    $rel = $file.FullName.Substring($Stage.Length).TrimStart('\')
    $actual[$rel] = (Get-FileHash $file.FullName -Algorithm SHA256).Hash.ToLower()
}
Check '清单不是空的' ($declared.Count -gt 0) "$($declared.Count) 条"
$onlyDeclared = @($declared.Keys | Where-Object { -not $actual.ContainsKey($_) })
$onlyActual = @($actual.Keys | Where-Object { -not $declared.ContainsKey($_) })
Check '清单里列的文件都在' ($onlyDeclared.Count -eq 0) ($onlyDeclared -join ', ')
Check '载荷里没有清单外的文件' ($onlyActual.Count -eq 0) ($onlyActual -join ', ')
$mismatch = @($declared.Keys | Where-Object {
    $actual.ContainsKey($_) -and $actual[$_] -ne $declared[$_] })
Check '每一条哈希都重算对得上' ($mismatch.Count -eq 0) ($mismatch -join ', ')

# --- 3. 该有的东西一件不少 --------------------------------------------------
foreach ($member in @('BUILD-INFO.txt',
                      'x64\ZuxiaTSF.dll', 'x86\ZuxiaTSF.dll',
                      'x64\rime.dll', 'x86\rime.dll',
                      'x64\ZuxiaSettings.exe',
                      'data\zuxia.schema.yaml', 'data\zuxia.dict.yaml',
                      'data\zuxia.extended.dict.yaml', 'data\zuxia.decoder.tsv',
                      'data\default.yaml')) {
    Check "载荷含 $member" ($actual.ContainsKey($member))
}

# --- 4. 不该进包的东西一件不多 ----------------------------------------------
# CI 为了跑引擎断言会把 engine-test.exe 拷进 dist\Zuxia\x64\，它一度就这么
# 被装进了用户的 Program Files。
$strays = @($actual.Keys | Where-Object {
    $_ -match '(?i)(test.*\.exe$|\.pdb$|\.ilk$|\.exp$|\.log$|\.tmp$|\.new$)' })
Check '载荷里没有测试程序、调试符号或日志' ($strays.Count -eq 0) ($strays -join ', ')

# --- 5. 出货的数据文件就是仓库里的那一份 ------------------------------------
# 这里比的是暂存目录里的数据文件与仓库工作区里的那一份，逐字节（SHA-256）。
# 「工作区就是 HEAD」由上面那条 tree=clean 保证，两条合起来等于「出货的数据
# 就是这一次提交里的数据」。不直接跟 git blob 比，是因为 blob 号会受行尾
# 转换（core.autocrlf）影响，比出来的差异跟正确性无关。
$sourceData = @(Get-ChildItem (Join-Path $Root 'data') -File |
                Where-Object { $_.Extension -in @('.yaml', '.tsv') })
Check '仓库 data/ 下有数据文件' ($sourceData.Count -gt 0) "$($sourceData.Count) 个"
foreach ($file in $sourceData) {
    $staged = Join-Path $Stage "data\$($file.Name)"
    if (-not (Test-Path $staged)) {
        Check "出货的 data\$($file.Name) 存在" $false '载荷里没有这个文件'
        continue
    }
    $ok = (Get-FileHash $staged -Algorithm SHA256).Hash -eq
          (Get-FileHash $file.FullName -Algorithm SHA256).Hash
    Check "出货的 data\$($file.Name) 与仓库里那一份逐字节相同" $ok
}
# 反过来也要查：载荷里不许有仓库 data/ 下没有的数据文件（上一次构建的残留）。
$sourceNames = @($sourceData | ForEach-Object { $_.Name })
$extraData = @($actual.Keys | Where-Object { $_ -like 'data\*' } |
               ForEach-Object { Split-Path $_ -Leaf } |
               Where-Object { $sourceNames -notcontains $_ })
Check '载荷里没有仓库里不存在的数据文件' ($extraData.Count -eq 0) ($extraData -join ', ')

# --- 6. 出货的 DLL 真的带着 HEAD 的代码 -------------------------------------
# 复审判定「出货的 0.2.0 不是 HEAD 编的」，用的就是这个办法：在 DLL 里找
# 只有新代码才会有的宽字符串。把它变成断言，下次就不用靠人去翻。
$markers = @('apply-failed', 'edit-session-refused', 'commit-failed',
             'end-composition-failed', 'composition-dropped',
             'process-key-threw',
    'caps-lock-passthrough')
foreach ($arch in @('x64', 'x86')) {
    $dll = Join-Path $Stage "$arch\ZuxiaTSF.dll"
    if (-not (Test-Path $dll)) { continue }
    $bytes = [IO.File]::ReadAllBytes($dll)
    $wide = [Text.Encoding]::Unicode.GetString($bytes) +
            [Text.Encoding]::Unicode.GetString($bytes, 1, $bytes.Length - 1)
    $absent = @($markers | Where-Object { -not $wide.Contains($_) })
    Check "$arch\ZuxiaTSF.dll 里有 HEAD 才有的诊断串" ($absent.Count -eq 0) `
          ("找不到 " + ($absent -join ', '))
}

# --- 7. 安装包与它的校验文件对得上 ------------------------------------------
$exe = Join-Path $Out "ZuxiaSetup-$version.exe"
Check "dist\ZuxiaSetup-$version.exe 存在" (Test-Path $exe)
$sumFile = "$exe.sha256"
Check "dist\ZuxiaSetup-$version.exe.sha256 存在" (Test-Path $sumFile)
if ((Test-Path $exe) -and (Test-Path $sumFile)) {
    $line = (Get-Content $sumFile -Raw).Trim()
    $want = ($line -split '\s+')[0].ToLower()
    $got = (Get-FileHash $exe -Algorithm SHA256).Hash.ToLower()
    Check '安装包的 SHA-256 与随包校验文件相同' ($want -eq $got) "文件写的 $want，实际 $got"
    Check '校验文件里的文件名就是安装包的名字' `
          ($line -match [regex]::Escape((Split-Path $exe -Leaf)))
}

# --- 8. 安装器编进去的载荷计数与清单一致 ------------------------------------
$infoHeader = Join-Path $Root 'installer\setup\payload_info.h'
if (Test-Path $infoHeader) {
    $text = Get-Content $infoHeader -Raw
    $m = [regex]::Match($text, 'ZX_PAYLOAD_FILES\s+(\d+)')
    # 清单本身也在 cab 里，所以载荷件数比清单行数多一。
    Check '安装器编进去的载荷件数与清单一致' `
          ($m.Success -and [int]$m.Groups[1].Value -eq ($declared.Count + 1)) `
          "payload_info.h=$($m.Groups[1].Value) 清单=$($declared.Count)"
}

Write-Host ''
if ($script:failures.Count -gt 0) {
    throw ("载荷门禁没过，{0} 条：{1}" -f $script:failures.Count,
           ($script:failures -join '; '))
}
Write-Host "载荷门禁全过（$($declared.Count) 个文件）"
