@echo off
setlocal
cmake -S . -B build -A x64
if errorlevel 1 exit /b 1
cmake --build build --config Release
if errorlevel 1 exit /b 1
echo.
echo Build complete: build\Release\SeowolYTMP3Downloader.exe
endlocal
