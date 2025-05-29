@echo off
echo ===== Building FastChain Project (Release Mode) =====
cd /d %~dp0

rem Create build directory if it doesn't exist
if not exist build mkdir build
cd build

rem Configure CMake for Release mode with optimizations
echo Configuring CMake for Release mode...
cmake -DCMAKE_BUILD_TYPE=Release ..
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ===== CMake Configuration Failed! =====
    echo.
    echo ===== Script Complete =====
    pause
    exit /b 1
)

rem Build the project
echo Building project in Release mode...
cmake --build . --config Release
if %ERRORLEVEL% EQU 0 (
    echo.
    echo ===== Build Successful! =====
    echo Available executables in Release folder:
    echo - matching_engine_tests.exe (unit tests)
    echo - basic_benchmarks.exe (performance benchmarks)
    echo - matching_engine_benchmarks.exe (advanced benchmarks)
    echo.
    echo To run benchmarks: cd build\Release && basic_benchmarks.exe
) else (
    echo.
    echo ===== Build Failed! =====
)
echo.
echo ===== Script Complete =====
pause
