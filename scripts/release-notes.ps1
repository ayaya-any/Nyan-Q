[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Tag,
    [Parameter(Mandatory)][string]$Version,
    [string]$PreviousTag = "",
    [Parameter(Mandatory)][string[]]$Assets,
    [Parameter(Mandatory)][string]$ChecksumsFile,
    [Parameter(Mandatory)][string]$OutputFile,
    [string]$Repository = $env:GITHUB_REPOSITORY
)

$ErrorActionPreference = "Stop"

$assetItems = @()
foreach ($asset in $Assets) {
    if (-not (Test-Path -LiteralPath $asset)) {
        throw "Release asset not found: $asset"
    }
    $assetItems += Get-Item -LiteralPath $asset
}

$checksums = @()
foreach ($item in $assetItems) {
    $hash = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $checksums += [pscustomobject]@{
        Name = $item.Name
        Size = $item.Length
        Hash = $hash
    }
}

$checksumDir = Split-Path -Parent $ChecksumsFile
if ($checksumDir) {
    New-Item -ItemType Directory -Path $checksumDir -Force | Out-Null
}
$checksumLines = $checksums | ForEach-Object { "$($_.Hash)  $($_.Name)" }
[IO.File]::WriteAllLines(
    $ChecksumsFile,
    [string[]]$checksumLines,
    (New-Object System.Text.UTF8Encoding($false))
)

$range = if ($PreviousTag) { "$PreviousTag..HEAD" } else { "HEAD" }
$logLines = @(& git log $range --no-merges --pretty=format:"%h%x09%s" 2>$null)
if ($LASTEXITCODE -ne 0) {
    throw "Unable to read git log for $range."
}

$commits = @()
foreach ($line in $logLines) {
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    $parts = $line -split "`t", 2
    if ($parts.Count -eq 2) {
        $commits += [pscustomobject]@{ Hash = $parts[0]; Subject = $parts[1] }
    }
}

$groupOrder = @("新增功能", "修复", "重构", "文档", "构建与发布", "其他变更")
$groups = @{}
foreach ($group in $groupOrder) {
    $groups[$group] = New-Object System.Collections.Generic.List[string]
}
foreach ($commit in $commits) {
    $group = "其他变更"
    if ($commit.Subject -match '^feat(\([^)]+\))?!?:') {
        $group = "新增功能"
    } elseif ($commit.Subject -match '^fix(\([^)]+\))?!?:') {
        $group = "修复"
    } elseif ($commit.Subject -match '^refactor(\([^)]+\))?!?:') {
        $group = "重构"
    } elseif ($commit.Subject -match '^docs(\([^)]+\))?!?:') {
        $group = "文档"
    } elseif ($commit.Subject -match '^(build|ci)(\([^)]+\))?!?:') {
        $group = "构建与发布"
    }
    $groups[$group].Add("- $($commit.Subject) (``$($commit.Hash)``)")
}

$commitHash = (& git rev-parse --short HEAD 2>$null).Trim()
if ($LASTEXITCODE -ne 0 -or -not $commitHash) {
    $commitHash = "unknown"
}
$builtAt = (Get-Date).ToUniversalTime().ToString("yyyy-MM-dd HH:mm 'UTC'")

$repoUrl = if ($Repository) { "https://github.com/$Repository" } else { "" }
$changesUrl = if ($PreviousTag -and $repoUrl) {
    "$repoUrl/compare/$PreviousTag...$Tag"
} elseif ($repoUrl) {
    "$repoUrl/commits/$Tag"
} else {
    ""
}

$versionNotesPath = Join-Path (Split-Path $PSScriptRoot -Parent) "docs\release-notes\$Tag.md"
$versionNotes = @()
if (Test-Path -LiteralPath $versionNotesPath) {
    $versionNotes = @(Get-Content -LiteralPath $versionNotesPath -Encoding utf8)
}

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("# Nyan-Q $Tag")
$lines.Add("")
$lines.Add("## 版本信息")
$lines.Add("")
$lines.Add("- 版本：``$Version``")
$lines.Add("- 提交：``$commitHash``")
$lines.Add("- 构建时间：$builtAt")
$lines.Add("- 平台：Windows 10 / 11 x64")
$lines.Add("- 构建：Qt 6.12.0、MSVC 2022、C++20")
$lines.Add("- Mihomo：v1.19.33")
$lines.Add("- 安装界面：简体中文")
$lines.Add("")
if ($versionNotes.Count -gt 0) {
    foreach ($line in $versionNotes) {
        $lines.Add($line)
    }
    $lines.Add("")
}
$lines.Add("## 变更内容")
$lines.Add("")
$lines.Add("本版本包含 $($commits.Count) 个提交。")
$lines.Add("")
if ($commits.Count -eq 0) {
    $lines.Add("- 无提交记录。")
    $lines.Add("")
} else {
    foreach ($group in $groupOrder) {
        if ($groups[$group].Count -eq 0) { continue }
        $lines.Add("### $group")
        $lines.Add("")
        foreach ($entry in $groups[$group]) {
            $lines.Add($entry)
        }
        $lines.Add("")
    }
}

$lines.Add("## 发布产物")
$lines.Add("")
$lines.Add("| 文件 | 大小 | SHA-256 |")
$lines.Add("| --- | ---: | --- |")
foreach ($item in $checksums) {
    $sizeMb = [math]::Round($item.Size / 1MB, 1)
    $lines.Add("| ``$($item.Name)`` | $sizeMb MB | ``$($item.Hash)`` |")
}
$lines.Add("")
$lines.Add("SHA-256 校验文件：``SHA256SUMS.txt``。")
$lines.Add("")
$lines.Add("## 安装与使用")
$lines.Add("")
$lines.Add("### 安装包")
$lines.Add("")
$lines.Add("- 运行 ``Nyan-Q-$Version-win64-setup.exe``。")
$lines.Add("- 安装向导为简体中文，默认按当前用户安装到 ``%LOCALAPPDATA%\Programs\Nyan-Q``。")
$lines.Add("- 安装前会检查 MSVC x64 运行库；缺失或版本过旧时先安装随包运行库。")
$lines.Add("- 卸载默认保留 ``%LOCALAPPDATA%\Nyan-Q`` 中的配置、订阅和日志。")
$lines.Add("")
$lines.Add("### 便携包")
$lines.Add("")
$lines.Add("- 解压 ``Nyan-Q-$Version-win64.zip`` 后直接运行 ``Nyan-Q.exe``。")
$lines.Add("- 解压后可直接运行，配置保存在数据目录或 ``portable.flag`` 指定的位置。")
$lines.Add("")
$lines.Add("## 兼容性与说明")
$lines.Add("")
$lines.Add("- 仅支持 Windows 10 / 11 x64。")
$lines.Add("- TUN 模式需要管理员权限；开启 TUN 或启动时检测到 TUN 已启用会请求 UAC 提权。")
$lines.Add("- 系统代理偏好会在应用重启、内核就绪后自动恢复。")
$lines.Add("- YAML 语言服务为独立 Rust 进程，无需 Node.js 运行时。")
$lines.Add("- 当前仅支持 Clash / Mihomo YAML 订阅，纯 Base64 节点列表不会自动转换。")
$lines.Add("- 当前构建未配置代码签名，Windows SmartScreen 可能显示提示。")
$lines.Add("- 发布资产请使用 ``SHA256SUMS.txt`` 校验完整性。")
$lines.Add("")
$lines.Add("## 完整变更记录")
$lines.Add("")
if ($changesUrl) {
    $lines.Add($changesUrl)
} else {
    $lines.Add("无可用仓库链接。")
}
$lines.Add("")
$lines.Add("---")
$lines.Add("")
$lines.Add("本发布说明由 GitHub Actions 根据 Git 提交记录和构建产物生成。")

$outputDir = Split-Path -Parent $OutputFile
if ($outputDir) {
    New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
}
[IO.File]::WriteAllText(
    $OutputFile,
    ($lines -join [Environment]::NewLine) + [Environment]::NewLine,
    (New-Object System.Text.UTF8Encoding($false))
)

Write-Host "Release notes written to $OutputFile"
Write-Host "Checksums written to $ChecksumsFile"
