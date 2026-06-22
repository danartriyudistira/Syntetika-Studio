param(
    [string]$ReleaseDir = "..\build_ninja_release\Source\Syntetika_artefacts\Release",
    [string]$OutputDir = "..\dist",
    [string]$InstallerName = "Syntetika-Studio-Setup.exe"
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path (Join-Path $scriptDir "..")
$releasePath = Resolve-Path (Join-Path $scriptDir $ReleaseDir)
$outputPath = Resolve-Path (New-Item -ItemType Directory -Force -Path (Join-Path $scriptDir $OutputDir))
$buildRoot = Join-Path $env:TEMP "SyntetikaStudioInstaller"
$workPath = Join-Path $buildRoot "iexpress-work"
$payloadZip = Join-Path $workPath "Syntetika-Studio.zip"
$installPs1 = Join-Path $workPath "install.ps1"
$installCmd = Join-Path $workPath "install.cmd"
$sedPath = Join-Path $workPath "Syntetika-Studio.sed"
$tempInstallerPath = Join-Path $buildRoot $InstallerName
$installerPath = Join-Path $outputPath $InstallerName

if (!(Test-Path (Join-Path $releasePath "Syntetika.exe"))) {
    throw "Syntetika.exe was not found in $releasePath. Build the Release target first."
}

if (!(Test-Path "$env:WINDIR\System32\iexpress.exe")) {
    throw "IExpress was not found on this Windows installation."
}

if (Test-Path $buildRoot) {
    Remove-Item -LiteralPath $buildRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $workPath | Out-Null

if (Test-Path $installerPath) {
    Remove-Item -LiteralPath $installerPath -Force
}

Compress-Archive -Path (Join-Path $releasePath "*") -DestinationPath $payloadZip -CompressionLevel Optimal -Force

@'
$ErrorActionPreference = "Stop"

$packageDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$zipPath = Join-Path $packageDir "Syntetika-Studio.zip"
$installDir = Join-Path $env:LOCALAPPDATA "Programs\Syntetika Studio"
$startMenuDir = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\Syntetika Studio"
$desktopShortcut = Join-Path ([Environment]::GetFolderPath("Desktop")) "Syntetika Studio.lnk"
$startShortcut = Join-Path $startMenuDir "Syntetika Studio.lnk"
$exePath = Join-Path $installDir "Syntetika.exe"

if (Test-Path $installDir) {
    Remove-Item -LiteralPath $installDir -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $installDir | Out-Null
Expand-Archive -LiteralPath $zipPath -DestinationPath $installDir -Force

New-Item -ItemType Directory -Force -Path $startMenuDir | Out-Null
$shell = New-Object -ComObject WScript.Shell

$shortcut = $shell.CreateShortcut($startShortcut)
$shortcut.TargetPath = $exePath
$shortcut.WorkingDirectory = $installDir
$shortcut.Description = "Syntetika Studio"
$shortcut.Save()

$shortcut = $shell.CreateShortcut($desktopShortcut)
$shortcut.TargetPath = $exePath
$shortcut.WorkingDirectory = $installDir
$shortcut.Description = "Syntetika Studio"
$shortcut.Save()

Write-Host "Syntetika Studio installed to $installDir"
Write-Host "Shortcuts were created on the Desktop and Start Menu."
'@ | Set-Content -LiteralPath $installPs1 -Encoding ASCII

@'
@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
if errorlevel 1 (
  echo.
  echo Installation failed.
  pause
  exit /b 1
)
exit /b 0
'@ | Set-Content -LiteralPath $installCmd -Encoding ASCII

@"
[Version]
Class=IEXPRESS
SEDVersion=3

[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=1
HideExtractAnimation=1
UseLongFileName=1
InsideCompressed=0
CAB_FixedSize=0
CAB_ResvCodeSigning=0
RebootMode=N
InstallPrompt=
DisplayLicense=
FinishMessage=Syntetika Studio installation has finished.
TargetName=$tempInstallerPath
FriendlyName=Syntetika Studio Installer
AppLaunched=install.cmd
PostInstallCmd=<None>
AdminQuietInstCmd=install.cmd
UserQuietInstCmd=install.cmd
SourceFiles=SourceFiles

[Strings]
FILE0="Syntetika-Studio.zip"
FILE1="install.ps1"
FILE2="install.cmd"

[SourceFiles]
SourceFiles0=$workPath

[SourceFiles0]
%FILE0%=
%FILE1%=
%FILE2%=
"@ | Set-Content -LiteralPath $sedPath -Encoding ASCII

& "$env:WINDIR\System32\iexpress.exe" /N /Q $sedPath

if (!(Test-Path $tempInstallerPath)) {
    throw "Installer was not created: $tempInstallerPath"
}

Copy-Item -LiteralPath $tempInstallerPath -Destination $installerPath -Force
Get-Item $installerPath
