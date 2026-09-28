@echo off
call "%~dp0build.cmd" /no-pause
if errorlevel 1 (
  echo.
  pause
  exit /b 1
)
set "OUTPUT=%~dp0publish\win-x64"
if not exist "%OUTPUT%" mkdir "%OUTPUT%"
copy /y "%~dp0build\DesktopTodo.exe" "%OUTPUT%\DesktopTodo.exe" >nul
if errorlevel 1 (pause & exit /b 1)
echo Published: "%OUTPUT%\DesktopTodo.exe"
pause
