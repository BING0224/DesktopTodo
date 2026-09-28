@echo off
call "%~dp0build.cmd" /no-pause
if errorlevel 1 (
  echo.
  pause
  exit /b 1
)
start "" "%~dp0build\DesktopTodo.exe"
