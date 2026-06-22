param(
    [string]$Configuration = "Release",
    [string]$Generator = "Ninja",
    [switch]$CreateInstaller
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
if (!(Test-Path $vcvars)) {
    $vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
}
if (!(Test-Path $vcvars)) {
    Write-Error "Visual Studio 2022 BuildTools or Community not found"
    exit 1
}

Write-Host "=== Building Syntetika Studio ($Configuration) ===" -ForegroundColor Cyan

# Setup VC env
& cmd /c """$vcvars"" x64 2>&1"

$buildDir = "build_$($Configuration.ToLower())"

# Configure
Write-Host "[1/3] Configuring CMake..." -ForegroundColor Yellow
if (!(Test-Path $buildDir)) {
    cmake -B $buildDir -G $Generator "-DCMAKE_BUILD_TYPE=$Configuration"
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
}

# Build
Write-Host "[2/3] Building..." -ForegroundColor Yellow
cmake --build $buildDir --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

# Verify output
$exePath = "$buildDir\Source\Syntetika_artefacts\$Configuration\Syntetika.exe"
if (!(Test-Path $exePath)) {
    Write-Error "Build output not found at $exePath"
    exit 1
}
$file = Get-Item $exePath
Write-Host "  Build OK: $($file.Length / 1MB -as [int]) MB" -ForegroundColor Green

# Installer
if ($CreateInstaller) {
    Write-Host "[3/3] Creating installer..." -ForegroundColor Yellow
    if (Test-Path "$env:WINDIR\System32\iexpress.exe") {
        & "$PSScriptRoot\..\syntetika_windows_installer\build_iexpress_installer.ps1" -ReleaseDir $exePath.Directory.Parent.Parent.FullName
    }
    else {
        Write-Warning "IExpress not found, skipping installer"
    }
}

Write-Host "`nDone!" -ForegroundColor Green
