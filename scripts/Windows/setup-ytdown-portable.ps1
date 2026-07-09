$ErrorActionPreference = "Stop"

# Layout: 4KDowner/scripts/Windows → Coding/packages
$ScriptsDir = $PSScriptRoot
$ProjectRoot = Split-Path (Split-Path $ScriptsDir -Parent) -Parent
$CodingRoot = Split-Path $ProjectRoot -Parent
$PackagesRoot = Join-Path $CodingRoot "packages"
$PythonDir = Join-Path $PackagesRoot "ytdown\python"
$Version = "3.12.10"
$ZipName = "python-$Version-embed-amd64.zip"
$Url = "https://www.python.org/ftp/python/$Version/$ZipName"
$TempZip = Join-Path $env:TEMP $ZipName
$GetPipUrl = "https://bootstrap.pypa.io/get-pip.py"
$GetPipPath = Join-Path $env:TEMP "get-pip.py"

Write-Host "Setting up portable Python + yt-dlp in $PythonDir"

if (Test-Path $PythonDir) {
    Remove-Item $PythonDir -Recurse -Force
}
New-Item -ItemType Directory -Path $PythonDir -Force | Out-Null

Write-Host "Downloading $Url"
Invoke-WebRequest -Uri $Url -OutFile $TempZip -UseBasicParsing
Expand-Archive -Path $TempZip -DestinationPath $PythonDir -Force
Remove-Item $TempZip -Force

$PthFile = Get-ChildItem $PythonDir -Filter "python*._pth" | Select-Object -First 1
if (-not $PthFile) {
    throw "Could not find python*._pth in embed package."
}

$PthLines = @(
    "python312.zip",
    ".",
    "Lib\site-packages",
    "import site"
)
Set-Content -Path $PthFile.FullName -Value $PthLines -Encoding ascii

New-Item -ItemType Directory -Path (Join-Path $PythonDir "Lib\site-packages") -Force | Out-Null

Write-Host "Installing pip..."
Invoke-WebRequest -Uri $GetPipUrl -OutFile $GetPipPath -UseBasicParsing
$PythonExe = Join-Path $PythonDir "python.exe"
& $PythonExe $GetPipPath --no-warn-script-location
if ($LASTEXITCODE -ne 0) {
    throw "get-pip failed."
}

Write-Host "Installing yt-dlp..."
& $PythonExe -m pip install --upgrade yt-dlp --no-warn-script-location
if ($LASTEXITCODE -ne 0) {
    throw "pip install yt-dlp failed."
}

Write-Host "Verifying..."
& $PythonExe -m yt_dlp --version
if ($LASTEXITCODE -ne 0) {
    throw "yt-dlp verification failed."
}

Write-Host "Done."
