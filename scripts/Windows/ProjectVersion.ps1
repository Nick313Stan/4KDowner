function Get-ProjectAppVersion {
    param([string]$ProjectRoot)

    $cmakeLists = Join-Path $ProjectRoot "CMakeLists.txt"
    if (-not (Test-Path $cmakeLists)) {
        return "1.2.0"
    }

    $text = Get-Content -LiteralPath $cmakeLists -Raw
    if ($text -match 'project\s*\(\s*\w+\s*\r?\n\s*VERSION\s+([0-9]+(?:\.[0-9]+)*)') {
        return $Matches[1]
    }
    if ($text -match 'VERSION\s+([0-9]+(?:\.[0-9]+)*)') {
        return $Matches[1]
    }
    return "1.2.0"
}
