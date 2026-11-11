@echo off
rem Builds build\NativeUiMod.asi with MSVC (x64). Needs the Script Hook V SDK in sdk\inc and sdk\lib.
setlocal
cd /d "%~dp0"

if not exist "sdk\inc\natives.h" (
    echo Script Hook V SDK not found. Extract ScriptHookV_SDK_*.zip so that sdk\inc and sdk\lib exist.
    exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo Visual Studio with the C++ x64 tools was not found.
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

if not exist build mkdir build
cl /nologo /std:c++17 /O2 /MT /EHsc /W3 /utf-8 /LD /DWIN32_LEAN_AND_MEAN /DNOMINMAX ^
   /I sdk\inc src\*.cpp /Fo:build\ /Fe:build\NativeUiMod.asi ^
   /link sdk\lib\ScriptHookV.lib user32.lib
if errorlevel 1 exit /b 1

echo.
echo Built build\NativeUiMod.asi
