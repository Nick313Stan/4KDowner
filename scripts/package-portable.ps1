param(
    [switch]$NoZip
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path $PSScriptRoot -Parent
$PackagesRoot = Join-Path (Split-Path $ProjectRoot -Parent) "packages"
$CompiledRoot = Join-Path (Split-Path $ProjectRoot -Parent) "compiled"
$OutRoot = Join-Path $CompiledRoot "4KDowner"
$BuildDir = Join-Path $ProjectRoot "build"
if (-not (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
    throw "CMake build directory not found. Configure build/ first."
}
$PythonSource = Join-Path $PackagesRoot "ytdown\python"
$SetupScript = Join-Path $ProjectRoot "scripts\setup-ytdown-portable.ps1"

if (-not (Test-Path (Join-Path $PythonSource "python.exe"))) {
    Write-Host "Portable Python not found. Running setup-ytdown-portable.ps1 ..."
    & $SetupScript
    if ($LASTEXITCODE -ne 0) {
        throw "setup-ytdown-portable.ps1 failed."
    }
}

Write-Host "Building Release..."
cmake --build $BuildDir --config Release
if ($LASTEXITCODE -ne 0) {
    throw "Release build failed."
}

$ExeCandidates = @(
    (Join-Path $BuildDir "Release\4KDowner.exe"),
    (Join-Path $BuildDir "4KDowner\Release\4KDowner.exe")
)
$Exe = $ExeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $Exe) {
    throw "4KDowner.exe not found in Release build output."
}

Write-Host "Packaging to $OutRoot ..."
New-Item -ItemType Directory -Path $CompiledRoot -Force | Out-Null
if (Test-Path $OutRoot) {
    Remove-Item $OutRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $OutRoot | Out-Null

Copy-Item $Exe (Join-Path $OutRoot "4KDowner.exe")

$AssetsSource = Join-Path $ProjectRoot "assets"
if (Test-Path $AssetsSource) {
    Copy-Item $AssetsSource (Join-Path $OutRoot "assets") -Recurse
}

$FfmpegDest = Join-Path $OutRoot "packages\ffmpeg\bin"
New-Item -ItemType Directory -Path $FfmpegDest -Force | Out-Null
Copy-Item (Join-Path $PackagesRoot "ffmpeg\bin\ffmpeg.exe") $FfmpegDest
Copy-Item (Join-Path $PackagesRoot "ffmpeg\bin\ffprobe.exe") $FfmpegDest

$PythonDest = Join-Path $OutRoot "packages\ytdown\python"
if (-not (Test-Path (Join-Path $PythonSource "python.exe"))) {
    throw "Portable Python not found at $PythonSource"
}
Copy-Item $PythonSource $PythonDest -Recurse

@"
4KDowner Portable
=================

1. Unzip anywhere on Windows 10/11.
2. Run 4KDowner.exe.

YouTube downloads:
- Sign in to YouTube in Firefox, Edge, or Chrome on this PC.

Default save folder:
  %USERPROFILE%\Videos\4kDowner
"@ | Set-Content (Join-Path $OutRoot "README.txt") -Encoding UTF8

$SizeMb = [math]::Round(((Get-ChildItem $OutRoot -Recurse -File | Measure-Object -Property Length -Sum).Sum / 1MB), 1)
Write-Host "Done: $OutRoot ($SizeMb MB)"

if ($NoZip) {
    return
}

$ZipPath = Join-Path $CompiledRoot "4KDowner.zip"
if (Test-Path $ZipPath) {
    Remove-Item $ZipPath -Force
}
Compress-Archive -Path $OutRoot -DestinationPath $ZipPath -CompressionLevel Optimal
$ZipMb = [math]::Round((Get-Item $ZipPath).Length / 1MB, 1)
Write-Host "Zip: $ZipPath ($ZipMb MB)"
