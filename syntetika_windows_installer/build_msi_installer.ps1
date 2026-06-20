param(
    [string]$BuildDir = "..\build\Source\Syntetika_artefacts\Release",
    [string]$WixBinDir = "C:\Program Files (x86)\WiX Toolset v3.14\bin",
    [string]$OutputMsi = "Syntetika-Windows-x64.msi"
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path (Join-Path $scriptDir "..")
$releasePath = Resolve-Path (Join-Path $scriptDir $BuildDir)
$wixBin = Get-ChildItem -Path $WixBinDir -Filter "candle.exe" -ErrorAction SilentlyContinue |
          Select-Object -First 1 -ExpandProperty DirectoryName
if (-not $wixBin) {
    $wixBin = $WixBinDir
}

$exePath = Join-Path $releasePath "Syntetika.exe"
if (-not (Test-Path $exePath)) {
    throw "Syntetika.exe not found at $exePath. Build the Release target first."
}

$candle = Join-Path $wixBin "candle.exe"
$light = Join-Path $wixBin "light.exe"
$heat = Join-Path $wixBin "heat.exe"

foreach ($tool in @($candle, $light, $heat)) {
    if (-not (Test-Path $tool)) {
        throw "WiX Tool not found: $tool. Install WiX Toolset v3.11 from https://wixtoolset.org/"
    }
}

Push-Location $scriptDir
try {
    Write-Host "=== Syntetika MSI Installer Build ===" -ForegroundColor Cyan
    Write-Host "Release path: $releasePath" -ForegroundColor Gray
    Write-Host "WiX binaries: $wixBin" -ForegroundColor Gray

    $resourceDir = Join-Path $releasePath "resource"
    $pythonDir = Join-Path $releasePath "Python"

    if (-not (Test-Path $resourceDir)) {
        throw "Resource directory not found: $resourceDir"
    }
    if (-not (Test-Path $pythonDir)) {
        throw "Python directory not found: $pythonDir"
    }

    Write-Host "[1/4] Harvesting resource directory..." -ForegroundColor Yellow
    & $heat dir $resourceDir -o HarvestedResourceDir.wxs -scom -frag -srd -sreg -gg -cg SyntetikaResourceDir -dr RESOURCE_DIR_REF
    if ($LASTEXITCODE -ne 0) { throw "heat failed for resource directory" }

    Write-Host "[2/4] Harvesting Python directory..." -ForegroundColor Yellow
    & $heat dir $pythonDir -o HarvestedPythonDir.wxs -scom -frag -srd -sreg -gg -cg SyntetikaPythonDir -dr PYTHON_DIR_REF
    if ($LASTEXITCODE -ne 0) { throw "heat failed for Python directory" }

    Write-Host "[3/4] Compiling WiX sources..." -ForegroundColor Yellow
    & $candle Syntetika.wxs HarvestedResourceDir.wxs HarvestedPythonDir.wxs -arch x64
    if ($LASTEXITCODE -ne 0) { throw "candle compilation failed" }

    Write-Host "[4/4] Linking MSI package..." -ForegroundColor Yellow
    & $light Syntetika.wixobj HarvestedResourceDir.wixobj HarvestedPythonDir.wixobj `
        -b $resourceDir -b $pythonDir -out $OutputMsi -ext WixUIExtension
    if ($LASTEXITCODE -ne 0) { throw "light linking failed" }

    $msiPath = Join-Path $scriptDir $OutputMsi
    if (Test-Path $msiPath) {
        $file = Get-Item $msiPath
        Write-Host "`nSUCCESS: MSI created at $msiPath" -ForegroundColor Green
        Write-Host "Size: $([math]::Round($file.Length / 1MB, 2)) MB" -ForegroundColor Green
    }
}
catch {
    Write-Host "`nBUILD FAILED: $_" -ForegroundColor Red
    exit 1
}
finally {
    Pop-Location
}
