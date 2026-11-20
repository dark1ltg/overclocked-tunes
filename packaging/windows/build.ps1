param(
    [string]$BuildDir = "build/windows",
    [string]$DistDir = "dist"
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "../..")
New-Item -ItemType Directory -Force -Path (Join-Path $Root $BuildDir) | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $Root $DistDir) | Out-Null

$cmakeArgs = @(
    "-S", $Root,
    "-B", (Join-Path $Root $BuildDir),
    "-DCMAKE_BUILD_TYPE=Release",
    "-DOCR_REQUIRE_TORRENT=ON"
)
$generator = $env:CMAKE_GENERATOR
if (-not $generator -or $generator -match "Visual Studio") {
    $cmakeArgs += "-A", "x64"
}
if ($env:QT_ROOT_DIR) {
    $cmakeArgs += "-DCMAKE_PREFIX_PATH=$($env:QT_ROOT_DIR)"
}
if ($env:VCPKG_ROOT) {
    $toolchain = Join-Path $env:VCPKG_ROOT "scripts/buildsystems/vcpkg.cmake"
    $cmakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
    $cmakeArgs += "-DVCPKG_TARGET_TRIPLET=x64-windows"
}
cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
cmake --build (Join-Path $Root $BuildDir) --config Release
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

$QtDir = $env:QT_ROOT_DIR
if (-not $QtDir) { $QtDir = $env:Qt6_DIR }
if ($QtDir -and (Test-Path (Join-Path $QtDir "bin"))) {
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

function Find-VcpkgBin {
    foreach ($p in @(
            (Join-Path $Root "$BuildDir/vcpkg_installed/x64-windows/bin"),
            (Join-Path $Root "vcpkg_installed/x64-windows/bin"),
            ($(if ($env:VCPKG_ROOT) { Join-Path $env:VCPKG_ROOT "installed/x64-windows/bin" } else { $null }))
        )) {
        if ($p -and (Test-Path $p)) { return $p }
    }
    return $null
}
$vcpkgBin = Find-VcpkgBin
if (-not $vcpkgBin) {
    throw "vcpkg libtorrent runtime DLLs not found. Set VCPKG_ROOT and install the libtorrent port."
}
Copy-Item (Join-Path $vcpkgBin "*.dll") $stage
$torrentDll = Get-ChildItem $vcpkgBin -Filter "*torrent*.dll" | Select-Object -First 1
if (-not $torrentDll) {
    throw "libtorrent DLL not found in $vcpkgBin"
}
Write-Host "Copied libtorrent runtime DLLs from $vcpkgBin ($($torrentDll.Name))"
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

function Find-Makensis {
    foreach ($p in @(
            (Get-Command makensis -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source),
            "${env:ProgramFiles(x86)}\NSIS\makensis.exe",
            "${env:ProgramFiles}\NSIS\makensis.exe",
            "C:\Program Files (x86)\NSIS\makensis.exe"
        )) {
        if ($p -and (Test-Path $p)) { return $p }
    }
    return $null
}
$makensis = Find-Makensis
if (-not $makensis) {
    throw "makensis not found. Install NSIS so the self-contained setup .exe can be built."
}
$nsi = Join-Path $PSScriptRoot "installer.nsi"
& $makensis "/DSTAGE=$stage" "/DOUT=$(Join-Path $Root $DistDir)" $nsi
if ($LASTEXITCODE -ne 0) {
    throw "makensis failed with exit code $LASTEXITCODE"
}

Write-Host "Windows artifacts in $DistDir"
