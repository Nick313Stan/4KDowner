# WiX branding BMPs in Blender style:
# - Welcome: blurred art panel left, white text area right
# - Banner: blurred background full-width, logo on the right (no square panel)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$ScriptsDir = $PSScriptRoot
$BrandingDir = Join-Path $ScriptsDir "branding"
# msi → Windows → scripts → 4KDowner
$ProjectRoot = Split-Path (Split-Path (Split-Path $ScriptsDir -Parent) -Parent) -Parent
$AssetsDir = Join-Path $ProjectRoot "assets"
$LogoPath = Join-Path $AssetsDir "logo\logo.png"
$BlurPath = Join-Path $AssetsDir "blured_background_msi.png"

if (-not (Test-Path $LogoPath)) {
    throw "Logo not found: $LogoPath"
}
if (-not (Test-Path $BlurPath)) {
    throw "Blur background not found: $BlurPath"
}

New-Item -ItemType Directory -Path $BrandingDir -Force | Out-Null

$logo = [System.Drawing.Image]::FromFile($LogoPath)
$blur = [System.Drawing.Image]::FromFile($BlurPath)

function Save-Bmp($bitmap, $path) {
    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Bmp)
    $bitmap.Dispose()
    Write-Host "Wrote $path"
}

function Draw-CoverImage($graphics, $image, $destX, $destY, $destW, $destH) {
    # Stretch-fill is enough for a soft blur background.
    $graphics.DrawImage($image, [int]$destX, [int]$destY, [int]$destW, [int]$destH)
}

function Draw-Logo($graphics, $x, $y, $maxWidth, $maxHeight) {
    $scale = [Math]::Min($maxWidth / $logo.Width, $maxHeight / $logo.Height)
    $drawW = [int]($logo.Width * $scale)
    $drawH = [int]($logo.Height * $scale)
    $drawX = $x + [int](($maxWidth - $drawW) / 2)
    $drawY = $y + [int](($maxHeight - $drawH) / 2)
    $graphics.DrawImage($logo, $drawX, $drawY, $drawW, $drawH)
}

# Welcome dialog bitmap (493x314). WixUI text starts at x≈135 on a 370px-wide dialog.
# Widen the art panel a bit so welcome text sits closer to the left (less white gap).
$dialogW = 493
$dialogH = 314
$sidebarEndOnDialog = 122.0
$sidebarW = [int]($dialogW * $sidebarEndOnDialog / 370.0)

$dialogBmp = New-Object System.Drawing.Bitmap $dialogW, $dialogH
$dialogG = [System.Drawing.Graphics]::FromImage($dialogBmp)
$dialogG.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
$dialogG.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$dialogG.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
$dialogG.Clear([System.Drawing.Color]::White)

Draw-CoverImage $dialogG $blur 0 0 $sidebarW $dialogH
Draw-Logo $dialogG 10 40 ($sidebarW - 20) ($dialogH - 80)
$dialogG.Dispose()
Save-Bmp $dialogBmp (Join-Path $BrandingDir "dialog.bmp")

# Top banner (493x58). Logo padded equally on top, bottom, and right.
$bannerW = 493
$bannerH = 58
$logoPad = 4
$logoMaxH = $bannerH - (2 * $logoPad)
$logoScale = [Math]::Min(1.0, $logoMaxH / [double]$logo.Height)
$logoDrawW = [int]($logo.Width * $logoScale)
$logoDrawH = [int]($logo.Height * $logoScale)
$logoDrawX = $bannerW - $logoPad - $logoDrawW
$logoDrawY = $logoPad + [int](($logoMaxH - $logoDrawH) / 2)

$bannerBmp = New-Object System.Drawing.Bitmap $bannerW, $bannerH
$bannerG = [System.Drawing.Graphics]::FromImage($bannerBmp)
$bannerG.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
$bannerG.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$bannerG.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality

Draw-CoverImage $bannerG $blur 0 0 $bannerW $bannerH

# Soft light wash on the left so black WiX title text stays readable.
$washBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush (
    (New-Object System.Drawing.Point 0, 0),
    (New-Object System.Drawing.Point 300, 0),
    [System.Drawing.Color]::FromArgb(170, 245, 242, 235),
    [System.Drawing.Color]::FromArgb(0, 245, 242, 235))
$bannerG.FillRectangle($washBrush, 0, 0, 300, $bannerH)
$washBrush.Dispose()

$bannerG.DrawImage($logo, $logoDrawX, $logoDrawY, $logoDrawW, $logoDrawH)
$bannerG.Dispose()
Save-Bmp $bannerBmp (Join-Path $BrandingDir "banner.bmp")

$logo.Dispose()
$blur.Dispose()
Write-Host "Branding generated (blur background, logo without square)."
