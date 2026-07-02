param(
    [string]$Configuration = "Release",
    [string]$Version = (Get-Date -Format "yyyyMMdd"),
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"

$buildDir = Join-Path $ProjectRoot "build"
$releaseDir = Join-Path $buildDir $Configuration
$exe = Join-Path $releaseDir "HaoRender-GI.exe"
if (-not (Test-Path -LiteralPath $exe)) {
    throw "Release executable not found: $exe"
}

$distRoot = Join-Path $ProjectRoot "dist"
$packageName = "HAORENDER-AI-$Version-win64"
$packageDir = Join-Path $distRoot $packageName
$zipPath = Join-Path $distRoot "$packageName.zip"

if (Test-Path -LiteralPath $packageDir) {
    Remove-Item -LiteralPath $packageDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $packageDir | Out-Null

$runtimeFiles = @(
    "HaoRender-GI.exe",
    "Qt5Core_conda.dll",
    "Qt5Gui_conda.dll",
    "Qt5Widgets_conda.dll",
    "Qt5OpenGL_conda.dll",
    "Qt5Network_conda.dll",
    "assimp-vc143-mtd.dll",
    "dxcompiler.dll",
    "dxil.dll"
)

foreach ($file in $runtimeFiles) {
    $source = Join-Path $releaseDir $file
    if (Test-Path -LiteralPath $source) {
        Copy-Item -LiteralPath $source -Destination (Join-Path $packageDir $file) -Force
    }
}

foreach ($pluginDir in @("platforms", "imageformats")) {
    $sourceDir = Join-Path $releaseDir $pluginDir
    if (Test-Path -LiteralPath $sourceDir) {
        Copy-Item -LiteralPath $sourceDir -Destination (Join-Path $packageDir $pluginDir) -Recurse -Force
    }
}

foreach ($contentDir in @("docs", "StylePresets")) {
    $sourceDir = Join-Path $ProjectRoot $contentDir
    if (Test-Path -LiteralPath $sourceDir) {
        Copy-Item -LiteralPath $sourceDir -Destination (Join-Path $packageDir $contentDir) -Recurse -Force
    }
}

foreach ($file in @("README.md", "RELEASE_NOTES.md", "llm.env.example", "LICENSE")) {
    $source = Join-Path $ProjectRoot $file
    if (Test-Path -LiteralPath $source) {
        Copy-Item -LiteralPath $source -Destination (Join-Path $packageDir $file) -Force
    }
}

if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
Compress-Archive -Path (Join-Path $packageDir "*") -DestinationPath $zipPath -Force

Write-Host "Package created: $zipPath"
