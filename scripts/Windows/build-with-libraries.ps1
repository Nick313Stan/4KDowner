# Bundle source + sibling packages/ for offline dev setup:
#   4KDowner-with-libraries-<version>.zip
#     4KDowner/   repo sources (no build trees, no .git)
#     packages/   ffmpeg, nodejs, raylib, tinyfiledialogs, ytdown, ...
#
# Usage (from repo root or anywhere):
#   powershell -ExecutionPolicy Bypass -File .\scripts\Windows\build-with-libraries.ps1
#
# Optional:
#   -OutRoot        output root (default: ../yCompiled)
#   -AppVersion      version in archive name (default: from CMakeLists.txt)
#   -PackagesRoot   override ../packages
#   -KeepStaging     do not delete temporary staging folder

param(
    [string]$OutRoot = "",
    [string]$AppVersion = "",
    [string]$PackagesRoot = "",
    [switch]$KeepStaging
)

$ErrorActionPreference = "Stop"

function Invoke-RobocopyMirror {
    param(
        [string]$Source,
        [string]$Destination,
        [string[]]$ExcludeDirs = @(),
        [string[]]$ExcludeFiles = @()
    )

    if (-not (Test-Path $Source)) {
        throw "Source missing: $Source"
    }

    New-Item -ItemType Directory -Path $Destination -Force | Out-Null

    $args = @(
        $Source,
        $Destination,
        "/MIR",
        "/NFL",
        "/NDL",
        "/NJH",
        "/NJS",
        "/NC",
        "/NS"
    )
    if ($ExcludeDirs.Count -gt 0) {
        $args += "/XD"
        $args += $ExcludeDirs
    }
    if ($ExcludeFiles.Count -gt 0) {
        $args += "/XF"
        $args += $ExcludeFiles
    }

    & robocopy @args | Out-Null
    if ($LASTEXITCODE -ge 8) {
        throw "robocopy failed ($LASTEXITCODE): $Source -> $Destination"
    }
}

function Find-CMakeCommand {
    $cmake = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmake) {
        return $cmake.Source
    }

    $candidates = @(
        "C:\Program Files\CMake\bin\cmake.exe",
        "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    return $null
}

$ScriptsDir = $PSScriptRoot
. (Join-Path $ScriptsDir "ProjectVersion.ps1")

$ProjectRoot = Split-Path (Split-Path $ScriptsDir -Parent) -Parent
$CodingRoot = Split-Path $ProjectRoot -Parent

if ([string]::IsNullOrWhiteSpace($PackagesRoot)) {
    $PackagesRoot = Join-Path $CodingRoot "packages"
}
if ([string]::IsNullOrWhiteSpace($AppVersion)) {
    $AppVersion = Get-ProjectAppVersion -ProjectRoot $ProjectRoot
}

if ([string]::IsNullOrWhiteSpace($OutRoot)) {
    $OutRoot = Join-Path $CodingRoot "yCompiled"
}

$ArchiveName = "4KDowner-with-libraries-$AppVersion.zip"
$ArchivePath = Join-Path $OutRoot $ArchiveName
$StagingRoot = Join-Path $OutRoot "_staging-with-libraries-$AppVersion"
$StagingProject = Join-Path $StagingRoot "4KDowner"
$StagingPackages = Join-Path $StagingRoot "packages"

$RequiredPackages = @(
    "raylib",
    "tinyfiledialogs",
    "ffmpeg",
    "ytdown",
    "nodejs"
)

Write-Host "=== 4KDowner with-libraries archive ==="
Write-Host "Project:   $ProjectRoot"
Write-Host "Packages:  $PackagesRoot"
Write-Host "Release:   $AppVersion"
Write-Host "Archive:   $ArchivePath"
Write-Host ""

foreach ($pkg in $RequiredPackages) {
    $pkgPath = Join-Path $PackagesRoot $pkg
    if (-not (Test-Path $pkgPath)) {
        throw "Required package folder missing: $pkgPath"
    }
}

New-Item -ItemType Directory -Path $OutRoot -Force | Out-Null
if (Test-Path $StagingRoot) {
    Remove-Item -LiteralPath $StagingRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $StagingRoot -Force | Out-Null

Write-Host "Staging project sources..."
$excludeDirs = @(
    "build",
    "build-windows",
    "build-linux",
    "out",
    ".vs",
    ".git",
    ".cursor",
    ".cache",
    "cache",
    (Join-Path (Join-Path (Join-Path "scripts" "Windows") "msi") "branding")
)
Invoke-RobocopyMirror -Source $ProjectRoot -Destination $StagingProject -ExcludeDirs $excludeDirs `
    -ExcludeFiles @("compile_commands.json")

Write-Host "Staging packages..."
Invoke-RobocopyMirror -Source $PackagesRoot -Destination $StagingPackages

if (Test-Path $ArchivePath) {
    Remove-Item -LiteralPath $ArchivePath -Force
}

$cmakeExe = Find-CMakeCommand
if ($cmakeExe) {
    Write-Host "Creating zip via cmake..."
    & $cmakeExe -E chdir $StagingRoot tar cf $ArchivePath --format=zip "4KDowner" "packages"
    if ($LASTEXITCODE -ne 0) {
        throw "cmake tar failed."
    }
} else {
    Write-Host "cmake not found; using Compress-Archive..."
    $tempZipRoot = Join-Path $env:TEMP "4KDowner-with-libraries-$AppVersion"
    if (Test-Path $tempZipRoot) {
        Remove-Item -LiteralPath $tempZipRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Path $tempZipRoot -Force | Out-Null
    Copy-Item -LiteralPath $StagingProject -Destination (Join-Path $tempZipRoot "4KDowner") -Recurse -Force
    Copy-Item -LiteralPath $StagingPackages -Destination (Join-Path $tempZipRoot "packages") -Recurse -Force
    Compress-Archive -LiteralPath @(
        (Join-Path $tempZipRoot "4KDowner"),
        (Join-Path $tempZipRoot "packages")
    ) -DestinationPath $ArchivePath -CompressionLevel Optimal -Force
    Remove-Item -LiteralPath $tempZipRoot -Recurse -Force
}

if (-not (Test-Path $ArchivePath)) {
    throw "Archive missing: $ArchivePath"
}

if (-not $KeepStaging) {
    Remove-Item -LiteralPath $StagingRoot -Recurse -Force
}

$archiveMb = [math]::Round((Get-Item $ArchivePath).Length / 1MB, 1)
Write-Host ""
Write-Host "Done: $ArchivePath ($archiveMb MB)"
