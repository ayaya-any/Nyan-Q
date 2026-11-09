#requires -Version 5.1
<#
.SYNOPSIS
    Downloads the pinned build and runtime dependencies for Nyan-Q.

.DESCRIPTION
    The repository intentionally does not track the Mihomo core, Wintun,
    libyaml, Nyan Setup or the VC++ redistributable.
    This script fetches the pinned versions into tools/ and third_party/
    (both ignored by Git) and verifies package checksums. QuickJS-ng is
    fetched separately by CMake FetchContent.

    Downloaded archives are cached under .cache/deps so repeated builds do
    not need to hit the network again. Pass -Force to redownload and
    reinstall everything.

    No Node.js installation is required.

.PARAMETER Installer
    Also fetch the dependencies used to build the installer: Nyan Setup
    and the Microsoft VC++ x64 redistributable.

.PARAMETER Force
    Redownload archives and reinstall all dependencies.

.EXAMPLE
    .\scripts\fetch-deps.ps1

.EXAMPLE
    .\scripts\fetch-deps.ps1 -Installer
#>
[CmdletBinding()]
param(
    [switch]$Installer,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ToolsDir = Join-Path $Root "tools"
$ThirdPartyDir = Join-Path $Root "third_party"
$CacheDir = Join-Path $Root ".cache\deps"

# Keep these versions in sync with THIRD_PARTY_NOTICES.md.
$MihomoVersion = "1.19.33"
$MihomoUrl = "https://github.com/MetaCubeX/mihomo/releases/download/v$MihomoVersion/mihomo-windows-amd64-compatible-v$MihomoVersion.zip"
$MihomoZipSha256 = "7AAF636A4F2172DCD8EC34AC4F52797B059195E5AFD6409508C963E545F8864D"
$MihomoExeSha256 = "926BFEA2CE48A247D6A7CE4CF641980E416202D5E40FE4EE5C949089ACE0E197"

$WintunVersion = "0.14.1"
$WintunUrl = "https://www.wintun.net/builds/wintun-$WintunVersion.zip"
$WintunZipSha256 = "07C256185D6EE3652E09FA55C0B673E2624B565E02C4B9091C79CA7D2F24EF51"
$WintunDllSha256 = "E5DA8447DC2C320EDC0FC52FA01885C103DE8C118481F683643CACC3220DAFCE"

# libyaml backs the order-preserving Mihomo configuration DOM used by the
# JavaScript override pipeline. QuickJS itself is fetched by CMake FetchContent.
$LibYamlVersion = "0.2.5"
$LibYamlUrl = "https://codeload.github.com/yaml/libyaml/zip/refs/tags/$LibYamlVersion"
$LibYamlZipSha256 = "14605BAA6DFC0C4D3AB943A46A627413C0388736E453B67FE4E90C9683C8CBC8"

$NyanSetupVersion = "0.1.0"
$NyanSetupUrl = "https://github.com/ayaya-any/Nyan-setup/releases/download/v$NyanSetupVersion/nyan-setup.exe"
$NyanSetupSha256 = "AEE1EC2B0275FA100F46A88F71AE496AB2FF179A867C88470FC2542485F0EE72"

$VCRedistUrl = "https://download.visualstudio.microsoft.com/download/pr/990c720a-46ce-4dc7-87b9-c2eb615d34b9/CC0FF0EB1DC3F5188AE6300FAEF32BF5BEEBA4BDD6E8E445A9184072096B713B/VC_redist.x64.exe"
$VCRedistSha256 = "CC0FF0EB1DC3F5188AE6300FAEF32BF5BEEBA4BDD6E8E445A9184072096B713B"

function Write-Step([string]$Message)
{
    Write-Host ""
    Write-Host "== $Message ==" -ForegroundColor Cyan
}

function Test-Sha256([string]$Path, [string]$Expected)
{
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    if ([string]::IsNullOrWhiteSpace($Expected)) { return $true }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -eq $Expected
}

function Get-DependencyFile([string]$Url, [string]$FileName, [string]$Sha256)
{
    New-Item -ItemType Directory -Path $CacheDir -Force | Out-Null
    $destination = Join-Path $CacheDir $FileName

    if (-not $Force -and (Test-Sha256 $destination $Sha256)) {
        Write-Host "  using cached $FileName"
        return $destination
    }

    if (Test-Path -LiteralPath $destination) {
        Remove-Item -LiteralPath $destination -Force
    }

    for ($attempt = 1; $attempt -le 3; $attempt++) {
        try {
            Write-Host "  downloading $Url"
            Invoke-WebRequest -Uri $Url -OutFile $destination -UseBasicParsing
            break
        } catch {
            if ($attempt -eq 3) {
                throw "Failed to download $Url after $attempt attempts: $($_.Exception.Message)"
            }
            Start-Sleep -Seconds (2 * $attempt)
        }
    }

    if (-not (Test-Path -LiteralPath $destination)) {
        throw "Download did not produce $destination"
    }
    if ($Sha256 -and -not (Test-Sha256 $destination $Sha256)) {
        $actual = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
        Remove-Item -LiteralPath $destination -Force
        throw "SHA256 mismatch for $FileName. Expected $Sha256, got $actual."
    }
    return $destination
}

function Expand-ZipEntry([string]$ZipPath, [string]$EntryName, [string]$Destination)
{
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        $entry = $zip.Entries | Where-Object { $_.FullName -eq $EntryName } |
            Select-Object -First 1
        if (-not $entry) {
            $sample = ($zip.Entries | Select-Object -First 8 -ExpandProperty FullName) -join ", "
            throw "Entry '$EntryName' not found in $ZipPath (first entries: $sample)"
        }
        $parent = Split-Path -Parent $Destination
        if ($parent) {
            New-Item -ItemType Directory -Path $parent -Force | Out-Null
        }
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile(
            $entry, $Destination, $true)
    } finally {
        $zip.Dispose()
    }
}

function Reset-ToolDirectory([string]$Directory)
{
    $target = [System.IO.Path]::GetFullPath($Directory)
    $allowed = @(
        (Join-Path $ToolsDir "nyan-setup")
    )
    if ($target -notin $allowed) {
        throw "Refusing to clean an unexpected dependency directory: $target"
    }
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    New-Item -ItemType Directory -Path $target -Force | Out-Null
}

function Install-Mihomo
{
    $directory = Join-Path $ThirdPartyDir "mihomo"
    $executable = Join-Path $directory "mihomo.exe"
    if (-not $Force -and (Test-Sha256 $executable $MihomoExeSha256)) {
        Write-Host "  mihomo $MihomoVersion (cached)"
        return
    }

    Write-Step "Mihomo $MihomoVersion"
    $archive = Get-DependencyFile -Url $MihomoUrl `
        -FileName "mihomo-windows-amd64-compatible-v$MihomoVersion.zip" `
        -Sha256 $MihomoZipSha256
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    Expand-ZipEntry -ZipPath $archive `
        -EntryName "mihomo-windows-amd64-compatible.exe" -Destination $executable
    if (-not (Test-Sha256 $executable $MihomoExeSha256)) {
        throw "mihomo.exe failed SHA256 verification after extraction."
    }
    Write-Host "  installed $executable"
}

function Install-Wintun
{
    $directory = Join-Path $ThirdPartyDir "mihomo"
    $library = Join-Path $directory "wintun.dll"
    if (-not $Force -and (Test-Sha256 $library $WintunDllSha256)) {
        Write-Host "  wintun $WintunVersion (cached)"
        return
    }

    Write-Step "Wintun $WintunVersion"
    $archive = Get-DependencyFile -Url $WintunUrl `
        -FileName "wintun-$WintunVersion.zip" -Sha256 $WintunZipSha256
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    Expand-ZipEntry -ZipPath $archive -EntryName "wintun/bin/amd64/wintun.dll" `
        -Destination $library
    if (-not (Test-Sha256 $library $WintunDllSha256)) {
        throw "wintun.dll failed SHA256 verification after extraction."
    }
    Write-Host "  installed $library"
}

function Install-LibYaml
{
    $directory = Join-Path $ThirdPartyDir "libyaml"
    $header = Join-Path $directory "include\yaml.h"
    $scanner = Join-Path $directory "src\scanner.c"
    if (-not $Force -and (Test-Path -LiteralPath $header) -and
        (Test-Path -LiteralPath $scanner)) {
        Write-Host "  libyaml $LibYamlVersion (cached)"
        return
    }

    Write-Step "libyaml $LibYamlVersion"
    $archive = Get-DependencyFile -Url $LibYamlUrl `
        -FileName "libyaml-$LibYamlVersion.zip" -Sha256 $LibYamlZipSha256
    $extractRoot = Join-Path $CacheDir "libyaml-$LibYamlVersion"
    if (Test-Path -LiteralPath $extractRoot) {
        Remove-Item -LiteralPath $extractRoot -Recurse -Force
    }
    Expand-Archive -LiteralPath $archive -DestinationPath $extractRoot -Force
    $source = Get-ChildItem -LiteralPath $extractRoot -Directory |
        Select-Object -First 1
    if (-not $source) {
        throw "libyaml archive did not contain a source directory."
    }

    $includeDir = Join-Path $directory "include"
    $srcDir = Join-Path $directory "src"
    New-Item -ItemType Directory -Path $includeDir -Force | Out-Null
    New-Item -ItemType Directory -Path $srcDir -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $source.FullName "include\yaml.h") `
        -Destination $includeDir -Force
    Copy-Item -Path (Join-Path $source.FullName "src\*.c") -Destination $srcDir -Force
    Copy-Item -LiteralPath (Join-Path $source.FullName "src\yaml_private.h") `
        -Destination $srcDir -Force
    Copy-Item -LiteralPath (Join-Path $source.FullName "License") `
        -Destination (Join-Path $directory "LICENSE.txt") -Force
    Copy-Item -LiteralPath (Join-Path $source.FullName "ReadMe.md") `
        -Destination (Join-Path $directory "README.md") -Force

    foreach ($required in @($header, $scanner,
                           (Join-Path $srcDir "yaml_private.h"))) {
        if (-not (Test-Path -LiteralPath $required)) {
            throw "libyaml installation is incomplete: $required"
        }
    }
    Write-Host "  installed $directory"
}

function Install-VCRedist
{
    $directory = Join-Path $ThirdPartyDir "vcredist"
    $executable = Join-Path $directory "vc_redist.x64.exe"
    if (-not $Force -and (Test-Sha256 $executable $VCRedistSha256)) {
        Write-Host "  vc_redist.x64.exe (cached)"
        return
    }

    Write-Step "Microsoft VC++ x64 redistributable"
    $download = Get-DependencyFile -Url $VCRedistUrl `
        -FileName "vc_redist.x64.exe" -Sha256 $VCRedistSha256
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    Copy-Item -LiteralPath $download -Destination $executable -Force
    if (-not (Test-Sha256 $executable $VCRedistSha256)) {
        throw "vc_redist.x64.exe failed SHA256 verification after copying."
    }
    Write-Host "  installed $executable"
}

function Install-NyanSetup
{
    $directory = Join-Path $ToolsDir "nyan-setup"
    $executable = Join-Path $directory "nyan-setup.exe"
    if (-not $Force -and (Test-Sha256 $executable $NyanSetupSha256)) {
        Write-Host "  Nyan Setup $NyanSetupVersion (cached)"
        return
    }

    Write-Step "Nyan Setup $NyanSetupVersion"
    $download = Get-DependencyFile -Url $NyanSetupUrl `
        -FileName "nyan-setup-v$NyanSetupVersion.exe" -Sha256 $NyanSetupSha256
    Reset-ToolDirectory $directory
    Copy-Item -LiteralPath $download -Destination $executable -Force
    if (-not (Test-Sha256 $executable $NyanSetupSha256)) {
        throw "nyan-setup.exe failed SHA256 verification after extraction."
    }
    Write-Host "  installed $executable"
}

Write-Host "Nyan-Q dependencies"
Write-Host "  cache: $CacheDir"

Install-Mihomo
Install-Wintun
Install-LibYaml

if ($Installer) {
    Install-VCRedist
    Install-NyanSetup
}

Write-Host ""
Write-Host "Dependencies ready." -ForegroundColor Green
