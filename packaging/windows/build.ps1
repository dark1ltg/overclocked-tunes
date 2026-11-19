param(
    [string]$BuildDir = "build/windows",
    [string]$DistDir = "dist"
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "../..")
New-Item -ItemType Directory -Force -Path (Join-Path $Root $BuildDir) | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $Root $DistDir) | Out-Null

cmake -S $Root -B (Join-Path $Root $BuildDir) -DCMAKE_BUILD_TYPE=Release
cmake --build (Join-Path $Root $BuildDir) --config Release

$QtDir = $env:Qt6_DIR
if (-not $QtDir) {
    $qmake = Get-Command windeployqt -ErrorAction SilentlyContinue
} else {
    $env:PATH = "$(Join-Path $QtDir 'bin');$env:PATH"
}

$exe = Join-Path $Root "$BuildDir/Release/OCRCatalog.exe"
if (-not (Test-Path $exe)) {
    $exe = Join-Path $Root "$BuildDir/OCRCatalog.exe"
}

$stage = Join-Path $Root "$BuildDir/stage"
Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item $exe $stage
windeployqt --release --compiler-runtime (Join-Path $stage "OCRCatalog.exe")
$sqlPlugin = $null
if ($QtDir) {
    $sqlPlugin = Join-Path $QtDir "plugins/sqldrivers"
}
if (-not $sqlPlugin -or -not (Test-Path $sqlPlugin)) {
    $qmake = Get-Command qmake -ErrorAction SilentlyContinue
    if ($qmake) {
        $qtPrefix = & $qmake.Source -query QT_INSTALL_PLUGINS
        $sqlPlugin = Join-Path $qtPrefix "sqldrivers"
    }
}
if ($sqlPlugin -and (Test-Path $sqlPlugin)) {
    New-Item -ItemType Directory -Force -Path (Join-Path $stage "sqldrivers") | Out-Null
    Copy-Item (Join-Path $sqlPlugin "qsqlite*") (Join-Path $stage "sqldrivers") -ErrorAction SilentlyContinue
}

$zip = Join-Path $Root "$DistDir/OCRCatalog-windows.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip

if (Get-Command makensis -ErrorAction SilentlyContinue) {
    $nsi = Join-Path $PSScriptRoot "installer.nsi"
    makensis "/DSTAGE=$stage" "/DOUT=$(Join-Path $Root $DistDir)" $nsi
}

Write-Host "Windows artifacts in $DistDir"
