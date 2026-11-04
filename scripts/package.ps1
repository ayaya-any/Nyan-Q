#requires -Version 5.1
<#
.SYNOPSIS
    Builds, tests and packages Clash-Q for Windows x64.

.EXAMPLE
    .\scripts\package.ps1 -QtDir "D:\Qt\6.12.0\msvc2022_64"
#>
[CmdletBinding()]
param(
    [string]$QtDir = $(if ($env:QT_ROOT) { $env:QT_ROOT } else { "D:\Qt\6.12.0\msvc2022_64" }),
    [string]$BuildDir = "",
    [switch]$SkipTests,
    [switch]$Installer
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $Root "build"
}
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$DistRoot = Join-Path $Root "dist"
$OutDir = Join-Path $DistRoot "Clash-Q"
$versionMatch = Select-String -Path (Join-Path $Root "CMakeLists.txt") `
    -Pattern "project\(ClashQ VERSION ([0-9]+\.[0-9]+\.[0-9]+)"
if (-not $versionMatch) { throw "Could not determine project version from CMakeLists.txt." }
$Version = $versionMatch.Matches[0].Groups[1].Value
$ZipPath = Join-Path $DistRoot "Clash-Q-$Version-win64.zip"

if (-not (Test-Path (Join-Path $QtDir "bin\windeployqt.exe"))) {
    throw "Qt 6.12.0 not found at '$QtDir'. Pass -QtDir <path-to-msvc2022_64>."
}
$QtDir = (Resolve-Path $QtDir).Path

function Resolve-CMake {
    $command = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $candidates = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { return $candidate }
    }
    throw "cmake.exe not found. Install CMake or Visual Studio's CMake tools."
}

function Resolve-Iscc {
    $candidates = @(
        (Join-Path $Root "tools\innosetup\app\ISCC.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"),
        (Join-Path $env:ProgramFiles "Inno Setup 6\ISCC.exe")
    )
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path $candidate)) { return $candidate }
    }
    throw "ISCC.exe not found. Install Inno Setup 6 or place a portable copy under tools\innosetup."
}

function Find-VisualStudio {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        # Prefer an installation that actually carries the MSVC redistributable
        # CRT files; vswhere -latest can point at a newer VS without them.
        $installations = & $vswhere -all -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath
        foreach ($path in $installations) {
            $trimmed = $path.Trim()
            if (Test-Path (Join-Path $trimmed "VC\Redist\MSVC")) {
                return $trimmed
            }
        }
        $latest = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath
        if ($latest) { return $latest.Trim() }
    }
    $fallbacks = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise",
        "C:\Program Files\Microsoft Visual Studio\18\Community"
    )
    foreach ($candidate in $fallbacks) {
        if (Test-Path (Join-Path $candidate "VC\Auxiliary\Build\vcvars64.bat")) {
            return $candidate
        }
    }
    return $null
}

$CMake = Resolve-CMake
$CTest = Join-Path (Split-Path $CMake -Parent) "ctest.exe"
$BuildApp = Join-Path $BuildDir "Release\Clash-Q.exe"
$VSInstall = Find-VisualStudio
if ($VSInstall) {
    $env:VCINSTALLDIR = Join-Path $VSInstall "VC\"
} else {
    Write-Warning "Visual Studio installation not found; MSVC runtime may not be bundled."
}

Write-Host ""
Write-Host "== Clash-Q / configure ==" -ForegroundColor Cyan
& $CMake -S $Root -B $BuildDir -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_PREFIX_PATH="$QtDir" -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

Write-Host ""
Write-Host "== Clash-Q / build ==" -ForegroundColor Cyan
$jobs = if ($env:NUMBER_OF_PROCESSORS) { [int]$env:NUMBER_OF_PROCESSORS } else { 8 }
& $CMake --build $BuildDir --config Release --parallel $jobs
if ($LASTEXITCODE -ne 0) { throw "Build failed." }

if (-not $SkipTests) {
    Write-Host ""
    Write-Host "== Clash-Q / tests ==" -ForegroundColor Cyan
    & $CTest --test-dir $BuildDir -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Tests failed." }
}

if (-not (Test-Path $BuildApp)) { throw "Build output not found: $BuildApp" }

Write-Host ""
Write-Host "== Clash-Q / package ==" -ForegroundColor Cyan
if (-not $OutDir.StartsWith($DistRoot + [System.IO.Path]::DirectorySeparatorChar,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to clean an unexpected path: $OutDir"
}
if (Test-Path $OutDir) { Remove-Item -LiteralPath $OutDir -Recurse -Force }
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

Copy-Item $BuildApp (Join-Path $OutDir "Clash-Q.exe") -Force
Copy-Item (Join-Path $Root "third_party\mihomo\mihomo.exe") $OutDir -Force
Copy-Item (Join-Path $Root "third_party\mihomo\wintun.dll") $OutDir -Force
Copy-Item (Join-Path $Root "README.md") $OutDir -Force
Copy-Item (Join-Path $Root "THIRD_PARTY_NOTICES.md") $OutDir -Force
Copy-Item (Join-Path $Root "lsp-servers.example.json") $OutDir -Force
Copy-Item (Join-Path $Root "LICENSE") $OutDir -Force
Copy-Item (Join-Path $Root "licenses") $OutDir -Recurse -Force
if (Test-Path (Join-Path $Root "docs")) {
    Copy-Item (Join-Path $Root "docs") $OutDir -Recurse -Force
}

# Bundle the open-source YAML language server and a portable Node runtime so
# the editor's LSP features work without a machine-wide Node installation.
$LspSource = Join-Path $Root "tools\yaml-language-server"
if (Test-Path (Join-Path $LspSource "node_modules\yaml-language-server\out\src\server.js")) {
    $LspOut = Join-Path $OutDir "lsp"
    $LspServerOut = Join-Path $LspOut "yaml-language-server"
    New-Item -ItemType Directory -Path $LspServerOut -Force | Out-Null
    Copy-Item (Join-Path $LspSource "*") $LspServerOut -Recurse -Force
    Copy-Item (Join-Path $Root "resources\clash-config.schema.json") $LspOut -Force

    $NodeExe = (Get-Command node.exe -ErrorAction SilentlyContinue).Source
    if (-not $NodeExe) {
        $NodeExe = Join-Path $env:ProgramFiles "nodejs\node.exe"
    }
    if (-not (Test-Path $NodeExe)) {
        throw "Node.js runtime not found; cannot bundle the YAML language server."
    }
    Copy-Item $NodeExe (Join-Path $LspOut "node.exe") -Force
    $NodeLicense = Join-Path (Split-Path $NodeExe -Parent) "LICENSE"
    if (Test-Path $NodeLicense) {
        Copy-Item $NodeLicense (Join-Path $OutDir "licenses\Node-LICENSE.txt") -Force
    }
    $YamlLicense = Join-Path $LspSource "node_modules\yaml-language-server\LICENSE"
    if (Test-Path $YamlLicense) {
        Copy-Item $YamlLicense (Join-Path $OutDir "licenses\Yaml-Language-Server-LICENSE.txt") `
            -Force
    }
    @"
YAML Language Server is bundled for the Clash-Q configuration editor.
It is started by Clash-Q as a stdio LSP server; it is not linked into the app.
See licenses/Yaml-Language-Server-LICENSE.txt and THIRD_PARTY_NOTICES.md.
"@ | Set-Content -LiteralPath (Join-Path $LspOut "README.txt") -Encoding UTF8
}

$WinDeployQt = Join-Path $QtDir "bin\windeployqt.exe"
Write-Host "Deploying Qt runtime from $QtDir" -ForegroundColor DarkGray
& $WinDeployQt --release --no-translations --no-opengl-sw --compiler-runtime `
    --dir $OutDir (Join-Path $OutDir "Clash-Q.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed." }

# windeployqt only finds the VC redist when VCINSTALLDIR is available. Copy
# the x64 CRT explicitly as a fallback so the package stays self-contained.
if ($VSInstall) {
    $redistRoot = Join-Path $VSInstall "VC\Redist\MSVC"
    if (Test-Path $redistRoot) {
        $versionDir = Get-ChildItem $redistRoot -Directory |
            Where-Object { Test-Path (Join-Path $_.FullName "x64\Microsoft.VC143.CRT") } |
            Sort-Object Name -Descending | Select-Object -First 1
        if ($versionDir) {
            $crtDir = Join-Path $versionDir.FullName "x64\Microsoft.VC143.CRT"
            Copy-Item (Join-Path $crtDir "*.dll") $OutDir -Force
            $stagedVCRedist = Join-Path $OutDir "vc_redist.x64.exe"
            if (Test-Path $stagedVCRedist) {
                Remove-Item -LiteralPath $stagedVCRedist -Force
            }
        }
    }
}
$missingCrt = @("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll") |
    Where-Object { -not (Test-Path (Join-Path $OutDir $_)) }
if ($missingCrt.Count -gt 0) {
    throw "MSVC runtime DLLs were not bundled: $($missingCrt -join ', ')."
}

# Smoke-run the packaged binary with no Qt bin directory on PATH. Local DLLs
# must satisfy the runtime dependencies for the package to be considered good.
Write-Host "Verifying packaged executable..." -ForegroundColor DarkGray
$savedPath = $env:Path
$preview = Join-Path $OutDir "package-check.png"
try {
    if (Test-Path $preview) { Remove-Item -LiteralPath $preview -Force }
    $env:Path = "$env:SystemRoot\System32;$env:SystemRoot"
    $process = Start-Process -FilePath (Join-Path $OutDir "Clash-Q.exe") `
        -ArgumentList @("--demo", "--screenshot", "`"$preview`"", "--page", "dashboard") `
        -Wait -PassThru
    if ($process.ExitCode -ne 0 -or -not (Test-Path $preview)) {
        throw "Packaged executable failed to render a screenshot (exit $($process.ExitCode))."
    }

    $editorPreview = Join-Path $OutDir "package-editor-check.png"
    $editorProcess = Start-Process -FilePath (Join-Path $OutDir "Clash-Q.exe") `
        -ArgumentList @("--screenshot-editor", "`"$editorPreview`"") `
        -Wait -PassThru
    if ($editorProcess.ExitCode -ne 0 -or -not (Test-Path $editorPreview)) {
        throw "Packaged editor/LSP check failed (exit $($editorProcess.ExitCode))."
    }
    Remove-Item -LiteralPath $editorPreview -Force -ErrorAction SilentlyContinue
}
finally {
    $env:Path = $savedPath
    if (Test-Path $preview) { Remove-Item -LiteralPath $preview -Force }
}

if (Test-Path $ZipPath) { Remove-Item -LiteralPath $ZipPath -Force }
Compress-Archive -Path $OutDir -DestinationPath $ZipPath -CompressionLevel Optimal

$sizeMb = [math]::Round((Get-Item $ZipPath).Length / 1MB, 1)
Write-Host ""
Write-Host "Package ready:" -ForegroundColor Green
Write-Host "  $OutDir"
Write-Host "  $ZipPath ($sizeMb MB)"

if ($Installer) {
    Write-Host ""
    Write-Host "== Clash-Q / installer ==" -ForegroundColor Cyan
    $Iscc = Resolve-Iscc
    $VCRedist = Join-Path $Root "third_party\vcredist\vc_redist.x64.exe"
    if (-not (Test-Path $VCRedist)) {
        throw "VC++ redistributable not found: $VCRedist"
    }
    $SetupPath = Join-Path $DistRoot "Clash-Q-$Version-win64-setup.exe"
    if (Test-Path $SetupPath) { Remove-Item -LiteralPath $SetupPath -Force }
    & $Iscc "/DAppVersion=$Version" "/DPayloadDir=$OutDir" `
        "/DOutputDir=$DistRoot" "/DVCRedistPath=$VCRedist" `
        (Join-Path $Root "scripts\installer.iss")
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed." }
    if (-not (Test-Path $SetupPath)) {
        throw "Installer was not produced: $SetupPath"
    }
    $setupSizeMb = [math]::Round((Get-Item $SetupPath).Length / 1MB, 1)
    Write-Host "  $SetupPath ($setupSizeMb MB)" -ForegroundColor Green
}
