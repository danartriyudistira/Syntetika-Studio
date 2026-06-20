@echo off
setlocal

set BUILD_DIR=..\build\Source\Syntetika_artefacts\Release
set WIX_BIN="C:\Program Files (x86)\WiX Toolset v3.14\bin"

if not exist %BUILD_DIR%\Syntetika.exe (
    echo ERROR: Syntetika.exe not found in %BUILD_DIR%
    echo Build the Release target first using CMake/Visual Studio.
    pause
    exit /b 1
)

echo Harvesting resource directory...
%WIX_BIN%\heat dir %BUILD_DIR%\resource -o HarvestedResourceDir.wxs -scom -frag -srd -sreg -gg -cg SyntetikaResourceDir -dr RESOURCE_DIR_REF
if errorlevel 1 (
    echo ERROR: heat failed for resource directory
    pause
    exit /b 1
)

echo Harvesting Python directory...
%WIX_BIN%\heat dir %BUILD_DIR%\Python -o HarvestedPythonDir.wxs -scom -frag -srd -sreg -gg -cg SyntetikaPythonDir -dr PYTHON_DIR_REF
if errorlevel 1 (
    echo ERROR: heat failed for Python directory
    pause
    exit /b 1
)

echo Compiling WiX sources...
%WIX_BIN%\candle Syntetika.wxs HarvestedResourceDir.wxs HarvestedPythonDir.wxs -arch x64
if errorlevel 1 (
    echo ERROR: candle compilation failed
    pause
    exit /b 1
)

echo Linking MSI package...
%WIX_BIN%\light Syntetika.wixobj HarvestedResourceDir.wixobj HarvestedPythonDir.wixobj -b %BUILD_DIR%\resource -b %BUILD_DIR%\Python -out Syntetika-Windows-x64.msi -ext WixUIExtension
if errorlevel 1 (
    echo ERROR: light linking failed
    pause
    exit /b 1
)

echo.
echo SUCCESS: Syntetika-Windows-x64.msi created successfully.
pause
