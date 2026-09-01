param(
    [switch]$RebuildPortable,
    [string]$OutRoot = "",
    [string]$AppVersion = "",
    [string]$PortableRoot = "",
    [string]$MsiOut = ""
)

$ErrorActionPreference = "Stop"

# Layout: 4KDowner/scripts/Windows → project root, sibling ../yCompiled
$ScriptsDir = $PSScriptRoot
. (Join-Path $ScriptsDir "ProjectVersion.ps1")

$ProjectRoot = Split-Path (Split-Path $ScriptsDir -Parent) -Parent
$CodingRoot = Split-Path $ProjectRoot -Parent

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
if ([string]::IsNullOrWhiteSpace($MsiOut)) {
    $MsiOut = Join-Path $OutRoot "$ReleaseName.msi"
}

$MsiBuild = Join-Path $OutRoot "$ReleaseName.build.msi"
$MsiDir = Join-Path $ScriptsDir "msi"
$BrandingDir = Join-Path $MsiDir "branding"
$AssetsDir = Join-Path $ProjectRoot "assets"

New-Item -ItemType Directory -Path $OutRoot -Force | Out-Null

$env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" +
    [System.Environment]::GetEnvironmentVariable("Path", "User")

if (-not (Get-Command wix -ErrorAction SilentlyContinue)) {
    throw "WiX CLI not found. Install with: dotnet tool install --global wix --version 5.0.2"
}

if ($RebuildPortable -or -not (Test-Path (Join-Path $PortableRoot "4KDowner.exe"))) {
    Write-Host "Building portable via build-portable.ps1..."
    & (Join-Path $ScriptsDir "build-portable.ps1") `
        -OutRoot $OutRoot `
        -AppVersion $AppVersion `
        -PortableRoot $PortableRoot
    if ($LASTEXITCODE -ne 0) {
        throw "build-portable.ps1 failed."
    }
}

if (-not (Test-Path (Join-Path $PortableRoot "4KDowner.exe"))) {
    throw "Portable build not found: $PortableRoot"
}

Write-Host "Generating installer branding..."
& (Join-Path $MsiDir "generate-branding.ps1")

foreach ($required in @(
        (Join-Path $MsiDir "4KDowner.wxs"),
        (Join-Path $MsiDir "WixUI_InstallDir_NoLicense.wxs"),
        (Join-Path $MsiDir "ReinstallReadyDlg.wxs"),
        (Join-Path $BrandingDir "banner.bmp"),
        (Join-Path $BrandingDir "dialog.bmp"),
        (Join-Path $AssetsDir "logo\logo.ico")
    )) {
    if (-not (Test-Path $required)) {
        throw "Missing required file: $required"
    }
}

if (Test-Path $MsiBuild) {
    Remove-Item $MsiBuild -Force
}

Write-Host "Building MSI (version $AppVersion) from $PortableRoot ..."
wix build `
    (Join-Path $MsiDir "4KDowner.wxs") `
    (Join-Path $MsiDir "WixUI_InstallDir_NoLicense.wxs") `
    (Join-Path $MsiDir "ReinstallReadyDlg.wxs") `
    -ext WixToolset.UI.wixext `
    -culture en-us `
    -loc (Join-Path $MsiDir "en-us.wxl") `
    -arch x64 `
    -d "ProductVersion=$AppVersion" `
    -b "Portable=$PortableRoot" `
    -b "Branding=$BrandingDir" `
    -b "Assets=$AssetsDir" `
    -o $MsiBuild

if ($LASTEXITCODE -ne 0) {
    throw "wix build failed."
}

try {
    if (Test-Path $MsiOut) {
        Remove-Item $MsiOut -Force
    }
    Move-Item $MsiBuild $MsiOut -Force
} catch {
    $MsiOut = $MsiBuild
    Write-Host "Note: could not replace locked MSI, left as $MsiOut"
}

$WixPdb = [System.IO.Path]::ChangeExtension($MsiBuild, ".wixpdb")
if (Test-Path $WixPdb) {
    Remove-Item $WixPdb -Force
}

$SizeMb = [math]::Round((Get-Item $MsiOut).Length / 1MB, 1)
Write-Host "Done: $MsiOut ($SizeMb MB)"
Write-Host "Wizard: Welcome → Destination Folder → Ready to Install"
