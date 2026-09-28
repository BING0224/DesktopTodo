@echo off
setlocal
set "PROJECT=%~dp0DesktopTodo"
set "OUTPUT=%~dp0build"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo [ERROR] Visual Studio Installer was not found.
  echo Install Visual Studio Community and select "Desktop development with C++".
  echo Download: https://visualstudio.microsoft.com/zh-hans/downloads/
  goto :fail
)
set "VS_DIR="
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_DIR=%%I"
if not defined VS_DIR (
  echo [ERROR] C++ desktop development tools were not found.
  echo Open Visual Studio Installer and add "Desktop development with C++".
  goto :fail
)
call "%VS_DIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto :fail
where rc >nul 2>nul
if errorlevel 1 (
  echo [ERROR] Windows SDK resource compiler is missing. Add a Windows SDK in Visual Studio Installer.
  goto :fail
)
if not exist "%OUTPUT%" mkdir "%OUTPUT%"
pushd "%PROJECT%"
rc /nologo /fo"%OUTPUT%\DesktopTodo.res" DesktopTodo.rc
if errorlevel 1 (
  popd
  goto :fail
)
popd
pushd "%OUTPUT%"
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0A00 /utf-8 /I"%PROJECT%" "%PROJECT%\main.cpp" "%PROJECT%\MainWindow.cpp" "%PROJECT%\ResourceUI.cpp" "%PROJECT%\DeadlinePicker.cpp" "%PROJECT%\DesktopHost.cpp" "%PROJECT%\Store.cpp" "%PROJECT%\Deadline.cpp" "%PROJECT%\Json.cpp" /Fe:"DesktopTodo.exe" /link /SUBSYSTEM:WINDOWS /MANIFEST:NO DesktopTodo.res gdiplus.lib user32.lib gdi32.lib shell32.lib ole32.lib advapi32.lib comctl32.lib comdlg32.lib
if errorlevel 1 (
  popd
  goto :fail
)
popd
echo [OK] Build complete: "%OUTPUT%\DesktopTodo.exe"
set "BUILD_RESULT=0"
goto :done
:fail
echo.
echo Build failed. The messages above show the cause.
set "BUILD_RESULT=1"
:done
if /i not "%~1"=="/no-pause" pause
exit /b %BUILD_RESULT%
