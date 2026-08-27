@echo off
setlocal
pushd "%~dp0" || exit /b 1
cmake -S . -B build -A x64
if errorlevel 1 goto :failed
cmake --build build --config Release
if errorlevel 1 goto :failed
echo.
echo Build complete: build\Release\FebiusYTMP3Downloader.exe
popd
endlocal
exit /b 0

:failed
set "build_exit=%errorlevel%"
popd
endlocal & exit /b %build_exit%
