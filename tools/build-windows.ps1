# Configure, build and test Qord on Windows with MSVC + Ninja.
#
# Expects (see docs/building.md):
#   - Visual Studio 2022/2026 with the "Desktop development with C++" workload
#   - Qt 6.7+ msvc2022_64 (default C:\Qt\6.10.2\msvc2022_64, override with -QtDir)
#   - deps\curl-impersonate and deps\ffmpeg (same prebuilts the CI uses)
#   - vendor\vcpkg bootstrapped
param(
    [string]$Config = "MinSizeRel",
    [string]$BuildDir = "build",
    [string]$QtDir = "C:\Qt\6.10.2\msvc2022_64",
    [switch]$ConfigureOnly,
    [switch]$NoTests,
    [string[]]$Targets = @(),
    [string[]]$ExtraArgs = @()
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

# Import the MSVC x64 developer environment into this PowerShell session
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
# -all: an instance waiting on a reboot reports isComplete=0 but its toolset works
$vs = & $vswhere -all -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
if (-not $vs) { throw "No Visual Studio installation with the C++ toolset was found" }
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
}

$curlDir = Join-Path $root "deps\curl-impersonate"
$ffmpegDir = Join-Path $root "deps\ffmpeg"
$build = Join-Path $root $BuildDir

$configure = @(
    "-S", $root, "-B", $build, "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=$Config",
    "-DVCPKG_TARGET_TRIPLET=x64-windows-static-md",
    "-DCMAKE_PREFIX_PATH=$QtDir;$ffmpegDir",
    "-DCURL_INCLUDE_DIR=$curlDir\include",
    "-DCURL_LIBRARY=$curlDir\lib\libcurl-impersonate_imp.lib",
    "-DBUILD_TESTS=ON"
) + $ExtraArgs
& cmake @configure
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
if ($ConfigureOnly) { return }

$targetArgs = @()
foreach ($t in $Targets) { $targetArgs += @("--target", $t) }
& cmake --build $build --parallel @targetArgs
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

# Runtime DLLs the tests and the app need next to the binaries
Copy-Item "$curlDir\lib\*.dll" $build -Force

# The Qt runtime next to the app, so build\acheron.exe also starts when
# double-clicked (without Qt on PATH)
$app = Join-Path $build "acheron.exe"
$deploy = Join-Path $QtDir "bin\windeployqt.exe"
if ((Test-Path $app) -and (Test-Path $deploy)) {
    & $deploy --no-translations --no-compiler-runtime --verbose 0 $app | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Warning "windeployqt failed; start the app with tools\run-mock.ps1 or Qt on PATH" }
    if (Test-Path "$ffmpegDir\bin") { Copy-Item "$ffmpegDir\bin\*.dll" $build -Force }
}

if (-not $NoTests) {
    $env:PATH = "$QtDir\bin;$ffmpegDir\bin;$env:PATH"
    $filter = @()
    if ($Targets) { $filter = @("-R", "plugin_") }
    & ctest --test-dir $build --output-on-failure --output-junit (Join-Path $build "ctest-results.xml") @filter
    if ($LASTEXITCODE -ne 0) { throw "Tests failed" }
}
