# One-shot Windows release packager:
#   portable folder + zip archive + MSI → ../yCompiled/4KDownerCompiled/
#
# Usage (from repo root or anywhere):
#   powershell -ExecutionPolicy Bypass -File .\scripts\Windows\main.ps1
#
# Optional:
#   -SkipMsi          skip WiX MSI
#   -SkipArchive      skip zip
#   -EnsureYtDown     run setup-ytdown-portable.ps1 if packages/ytdown/python is missing
#
# Single stages:
#   .\scripts\Windows\build-portable.ps1
#   .\scripts\Windows\build-msi.ps1

param(
    [switch]$SkipMsi,
    [switch]$SkipArchive,
    [switch]$EnsureYtDown,
    [string]$OutRoot = "",
    [string]$AppVersion = "1.0.0"
)

$ErrorActionPreference = "Stop"

$ScriptsDir = $PSScriptRoot
$ProjectRoot = Split-Path (Split-Path $ScriptsDir -Parent) -Parent
$CodingRoot = Split-Path $ProjectRoot -Parent
$BuildDir = Join-Path $ProjectRoot "build"

$ReleaseName = "4KDowner-$AppVersion-windows-x64"

if ([string]::IsNullOrWhiteSpace($OutRoot)) {
    $OutRoot = Join-Path $CodingRoot "yCompiled\4KDownerCompiled"
}

$PortableRoot = Join-Path $OutRoot $ReleaseName
$ArchivePath = Join-Path $OutRoot "$ReleaseName.zip"
$MsiPath = Join-Path $OutRoot "$ReleaseName.msi"

Write-Host "=== 4KDowner Windows package ==="
Write-Host "Project:  $ProjectRoot"
Write-Host "Release:  $ReleaseName"
Write-Host "Output:   $OutRoot"
Write-Host ""

$portableArgs = @{
    OutRoot      = $OutRoot
    AppVersion   = $AppVersion
    PortableRoot = $PortableRoot
}
if ($EnsureYtDown) {
    $portableArgs.EnsureYtDown = $true
}

& (Join-Path $ScriptsDir "build-portable.ps1") @portableArgs
if ($LASTEXITCODE -ne 0) {
    throw "build-portable.ps1 failed."
}

if (-not $SkipArchive) {
    Write-Host "Setting archive output path..."
    cmake -S $ProjectRoot -B $BuildDir `
        "-D4KDOWNER_PACKAGE_DIR=$PortableRoot" `
        "-D4KDOWNER_ARCHIVE_PATH=$ArchivePath"
    if ($LASTEXITCODE -ne 0) {
        throw "cmake path update failed."
    }

    Write-Host "Building portable archive..."
    cmake --build $BuildDir --config Release --target package-archive
    if ($LASTEXITCODE -ne 0) {
        throw "package-archive failed."
    }
    if (-not (Test-Path $ArchivePath)) {
        throw "Archive missing: $ArchivePath"
    }
}

if (-not $SkipMsi) {
    Write-Host "Building MSI..."
    & (Join-Path $ScriptsDir "build-msi.ps1") `
        -OutRoot $OutRoot `
        -AppVersion $AppVersion `
        -PortableRoot $PortableRoot `
        -MsiOut $MsiPath
    if ($LASTEXITCODE -ne 0) {
        throw "build-msi.ps1 failed."
    }
}

Write-Host ""
Write-Host "=== Done ==="
Write-Host "Portable:  $PortableRoot"
if (-not $SkipArchive) {
    $zipMb = [math]::Round((Get-Item $ArchivePath).Length / 1MB, 1)
    Write-Host "Archive:   $ArchivePath ($zipMb MB)"
}
if (-not $SkipMsi) {
    if (Test-Path $MsiPath) {
        $msiMb = [math]::Round((Get-Item $MsiPath).Length / 1MB, 1)
        Write-Host "MSI:       $MsiPath ($msiMb MB)"
    }
}
