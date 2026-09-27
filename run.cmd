@echo off
setlocal
pushd "%~dp0"

where dotnet.exe >nul 2>nul
if errorlevel 1 (
  echo [DesktopTodo] .NET 8 SDK was not found.
  echo Install it, close this window, and run this file again.
  pause
  exit /b 1
)

dotnet.exe build "%~dp0DesktopTodo\DesktopTodo.csproj" --nologo --verbosity quiet
if errorlevel 1 (
  echo.
  echo Build failed. Check the error message above.
  pause
  exit /b 1
)

dotnet.exe build-server shutdown >nul 2>nul
start "" "%~dp0DesktopTodo\bin\Debug\net8.0-windows\DesktopTodo.exe"

popd
endlocal
