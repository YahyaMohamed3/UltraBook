@echo off
echo ===== Building FastChain Project =====
cd /d %~dp0
cd build
cmake --build . --config Debug
if %ERRORLEVEL% EQU 0 (
    echo.
    echo ===== Build Successful! Running Tests =====
    echo.
    cd Debug
    matching_engine_tests.exe
) else (
    echo.
    echo ===== Build Failed! =====
)
echo.
echo ===== Script Complete =====
pause