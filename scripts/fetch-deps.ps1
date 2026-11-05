#requires -Version 5.1
<#
.SYNOPSIS
    Downloads the pinned build and runtime dependencies for Nyan-Q.

.DESCRIPTION
    The repository intentionally does not track the Mihomo core, Wintun,
    the YAML language server, Inno Setup or the VC++ redistributable.
    This script fetches the pinned versions into tools/ and third_party/
    (both ignored by Git) and verifies package checksums.

    Downloaded archives are cached under .cache/deps so repeated builds do
    not need to hit the network again. Pass -Force to redownload and
    reinstall everything.

    Node.js 22 with npm is required to install the YAML language server.

.PARAMETER Installer
    Also fetch the dependencies used to build the Inno Setup installer:
    Inno Setup itself and the Microsoft VC++ x64 redistributable.

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

$InnoVersion = "6.7.4"
$InnoSetupUrl = "https://github.com/jrsoftware/issrc/releases/download/is-6_7_4/innosetup-$InnoVersion.exe"
$InnoSetupSha256 = "A549929319F9120716677C685535CA4671E4B937B5FDC87E2585F32D8EB30E8C"
$InnoLanguageUrl = "https://raw.githubusercontent.com/jrsoftware/issrc/is-6_7_4/Files/Languages/Unofficial/ChineseSimplified.isl"
$InnoLanguageSha256 = "7D544B9BB1D142CFA11F2E5D3CC8ABE2E55F8E066C5124E3772675AA236E1278"

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
        (Join-Path $ToolsDir "yaml-language-server"),
        (Join-Path $ToolsDir "innosetup")
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

function Install-YamlLanguageServer
{
    $directory = Join-Path $ToolsDir "yaml-language-server"
    $server = Join-Path $directory "node_modules\yaml-language-server\out\src\server.js"
    $manifestDirectory = Join-Path $PSScriptRoot "lsp"
    $packageJson = Join-Path $manifestDirectory "package.json"
    $packageLock = Join-Path $manifestDirectory "package-lock.json"
    $marker = Join-Path $directory ".lock-sha256"

    if (-not (Test-Path -LiteralPath $packageJson) -or
        -not (Test-Path -LiteralPath $packageLock)) {
        throw "YAML language server manifests are missing from $manifestDirectory"
    }

    $lockHash = (Get-FileHash -LiteralPath $packageLock -Algorithm SHA256).Hash
    $installedLockHash = ""
    if (Test-Path -LiteralPath $marker) {
        $installedLockHash = (Get-Content -LiteralPath $marker -Raw).Trim()
    }
    if (-not $Force -and (Test-Path -LiteralPath $server) -and
        $installedLockHash -eq $lockHash) {
        Write-Host "  yaml-language-server (cached)"
        return
    }

    $npm = Get-Command npm.cmd -ErrorAction SilentlyContinue
    if (-not $npm) {
        $npm = Get-Command npm -ErrorAction SilentlyContinue
    }
    if (-not $npm) {
        throw "npm was not found. Install Node.js 22 (including npm) to fetch yaml-language-server."
    }

    Write-Step "YAML language server"
    Reset-ToolDirectory $directory
    Copy-Item -LiteralPath $packageJson -Destination $directory
    Copy-Item -LiteralPath $packageLock -Destination $directory

    Push-Location $directory
    try {
        & $npm.Source ci --omit=dev --ignore-scripts --no-audit --no-fund
        if ($LASTEXITCODE -ne 0) {
            throw "npm ci failed with exit code $LASTEXITCODE."
        }
    } finally {
        Pop-Location
    }

    if (-not (Test-Path -LiteralPath $server)) {
        throw "yaml-language-server was not installed correctly: $server"
    }
    [System.IO.File]::WriteAllText($marker, $lockHash)
    Write-Host "  installed $server"
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

function Install-InnoSetup
{
    $directory = Join-Path $ToolsDir "innosetup"
    $compiler = Join-Path $directory "ISCC.exe"
    $marker = Join-Path $directory ".version"
    $installedVersion = ""
    if (Test-Path -LiteralPath $marker) {
        $installedVersion = (Get-Content -LiteralPath $marker -Raw).Trim()
    }
    if (-not $Force -and (Test-Path -LiteralPath $compiler) -and
        $installedVersion -eq $InnoVersion -and
        (Test-Sha256 (Join-Path $directory "Languages\ChineseSimplified.isl") $InnoLanguageSha256)) {
        Write-Host "  Inno Setup $InnoVersion (cached)"
        return
    }

    Write-Step "Inno Setup $InnoVersion"
    $setup = Get-DependencyFile -Url $InnoSetupUrl `
        -FileName "innosetup-$InnoVersion.exe" -Sha256 $InnoSetupSha256
    $language = Get-DependencyFile -Url $InnoLanguageUrl `
        -FileName "ChineseSimplified-$InnoVersion.isl" -Sha256 $InnoLanguageSha256

    Reset-ToolDirectory $directory

    $log = Join-Path $CacheDir "innosetup-install.log"
    $arguments = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- ' +
        '/PORTABLE=1 /CURRENTUSER /DIR="' + $directory + '" /LOG="' + $log + '"'
    $process = Start-Process -FilePath $setup -ArgumentList $arguments `
        -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0) {
        throw "Inno Setup portable installation failed with exit code $($process.ExitCode). See $log"
    }
    if (-not (Test-Path -LiteralPath $compiler)) {
        throw "Inno Setup was not installed correctly: $compiler"
    }

    $languages = Join-Path $directory "Languages"
    New-Item -ItemType Directory -Path $languages -Force | Out-Null
    Copy-Item -LiteralPath $language `
        -Destination (Join-Path $languages "ChineseSimplified.isl") -Force
    [System.IO.File]::WriteAllText($marker, $InnoVersion)
    Write-Host "  installed $compiler"
}

Write-Host "Nyan-Q dependencies"
Write-Host "  cache: $CacheDir"

Install-Mihomo
Install-Wintun
Install-YamlLanguageServer

if ($Installer) {
    Install-VCRedist
    Install-InnoSetup
}

Write-Host ""
Write-Host "Dependencies ready." -ForegroundColor Green
