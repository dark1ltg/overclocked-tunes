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
function Copy-QtPluginDir($name) {
    $src = $null
    if ($QtDir) {
        $src = Join-Path $QtDir "plugins/$name"
    }
    if (-not $src -or -not (Test-Path $src)) {
        $qmakeCmd = Get-Command qmake6 -ErrorAction SilentlyContinue
        if (-not $qmakeCmd) { $qmakeCmd = Get-Command qmake -ErrorAction SilentlyContinue }
        if ($qmakeCmd) {
            $qtPrefix = & $qmakeCmd.Source -query QT_INSTALL_PLUGINS
            $src = Join-Path $qtPrefix $name
        }
    }
    if ($src -and (Test-Path $src)) {
        New-Item -ItemType Directory -Force -Path (Join-Path $stage $name) | Out-Null
        Copy-Item (Join-Path $src "*") (Join-Path $stage $name) -ErrorAction SilentlyContinue
    }
}
Copy-QtPluginDir "sqldrivers"
Copy-QtPluginDir "tls"

$zip = Join-Path $Root "$DistDir/OCRCatalog-windows.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip

if (Get-Command makensis -ErrorAction SilentlyContinue) {
    $nsi = Join-Path $PSScriptRoot "installer.nsi"
    makensis "/DSTAGE=$stage" "/DOUT=$(Join-Path $Root $DistDir)" $nsi
}

Write-Host "Windows artifacts in $DistDir"
