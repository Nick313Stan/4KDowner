# Build portable folder only → ../yCompiled/4KDowner-<ver>-windows-x64/
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File .\scripts\Windows\build-portable.ps1
#
# Optional:
#   -EnsureYtDown   refresh packages/ytdown portable Python/yt-dlp
#   -OutRoot        output root (default: ../yCompiled)
#   -AppVersion     version segment in folder name (default: from CMakeLists.txt)
#   -PortableRoot   override full portable folder path

param(
    [switch]$EnsureYtDown,
    [string]$OutRoot = "",
    [string]$AppVersion = "",
    [string]$PortableRoot = ""
)

$ErrorActionPreference = "Stop"

$ScriptsDir = $PSScriptRoot
. (Join-Path $ScriptsDir "ProjectVersion.ps1")

$ProjectRoot = Split-Path (Split-Path $ScriptsDir -Parent) -Parent
$CodingRoot = Split-Path $ProjectRoot -Parent
$BuildDir = Join-Path $ProjectRoot "build-windows"
$PackagesRoot = Join-Path $CodingRoot "packages"

if ([string]::IsNullOrWhiteSpace($AppVersion)) {
    $AppVersion = Get-ProjectAppVersion -ProjectRoot $ProjectRoot
}

$ReleaseName = "4KDowner-$AppVersion-windows-x64"

if ([string]::IsNullOrWhiteSpace($OutRoot)) {
    $OutRoot = Join-Path $CodingRoot "yCompiled"
}
if ([string]::IsNullOrWhiteSpace($PortableRoot)) {
    $PortableRoot = Join-Path $OutRoot $ReleaseName
}

Write-Host "=== 4KDowner portable folder ==="
Write-Host "Project:  $ProjectRoot"
Write-Host "Release:  $ReleaseName"
Write-Host "Output:   $PortableRoot"
Write-Host ""

New-Item -ItemType Directory -Path $OutRoot -Force | Out-Null

$env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" +
    [System.Environment]::GetEnvironmentVariable("Path", "User")

if (-not (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
    Write-Host "Configuring CMake build-windows/ ..."
    cmake -S $ProjectRoot -B $BuildDir
    if ($LASTEXITCODE -ne 0) {
        throw "cmake configure failed."
    }
}

Write-Host "Setting package output path..."
cmake -S $ProjectRoot -B $BuildDir "-D4KDOWNER_PACKAGE_DIR=$PortableRoot"
if ($LASTEXITCODE -ne 0) {
    throw "cmake path update failed."
}

$PythonExe = Join-Path $PackagesRoot "ytdown\python\python.exe"
if ($EnsureYtDown -or -not (Test-Path $PythonExe)) {
    if (-not (Test-Path $PythonExe)) {
        Write-Host "Portable Python/yt-dlp missing; running setup-ytdown-portable.ps1 ..."
    } else {
        Write-Host "EnsureYtDown: refreshing portable Python/yt-dlp ..."
    }
    & (Join-Path $ScriptsDir "setup-ytdown-portable.ps1")
    if ($LASTEXITCODE -ne 0) {
        throw "setup-ytdown-portable.ps1 failed."
    }
}

Write-Host "Building Release + portable..."
cmake --build $BuildDir --config Release --target package-portable
if ($LASTEXITCODE -ne 0) {
    throw "package-portable failed."
}

if (-not (Test-Path (Join-Path $PortableRoot "4KDowner.exe"))) {
    throw "Portable exe missing: $PortableRoot\4KDowner.exe"
}

# Do not ship a generated README in the portable tree.
Remove-Item -LiteralPath (Join-Path $PortableRoot "README.txt") -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path $PortableRoot "README.md") -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "Done: $PortableRoot"
