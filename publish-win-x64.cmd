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

dotnet.exe publish "%~dp0DesktopTodo\DesktopTodo.csproj" ^
  --configuration Release ^
  --runtime win-x64 ^
  --self-contained true ^
  -p:PublishSingleFile=true ^
  -p:IncludeNativeLibrariesForSelfExtract=true ^
  --output "%~dp0publish\win-x64"

if errorlevel 1 (
  echo.
  echo Publish failed. Check the error message above.
  pause
  exit /b 1
)

dotnet.exe build-server shutdown >nul 2>nul

echo.
echo Published: %~dp0publish\win-x64\DesktopTodo.exe
explorer "%~dp0publish\win-x64"

popd
endlocal
