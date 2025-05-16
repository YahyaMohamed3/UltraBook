@echo off
setlocal enabledelayedexpansion

echo ===== FastChain Basic Benchmark Runner =====
echo This script will run basic performance benchmarks for the matching engine
echo.

rem Path to the build directory
set BUILD_DIR=c:\Users\yahya\fastchain\build

rem Build the benchmarks
cd /d %BUILD_DIR%
echo Building benchmarks...

rem Configure CMake to include the new benchmark file
cmake -DBENCHMARK_FILE="c:\Users\yahya\fastchain\benchmarks\engine\basic_benchmarks.cpp" ..

rem Build the benchmark executable
cmake --build . --config Release

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ===== Build Failed! =====
    pause
    exit /b 1
)

cd Release

echo.
echo ===== Running Basic Benchmarks =====
echo This will take a few minutes...
echo.

rem Run the benchmarks with appropriate settings
basic_benchmarks.exe --benchmark_format=console --benchmark_out=basic_benchmarks_results.json --benchmark_out_format=json

echo.
echo ===== Performance Summary =====
echo.
echo Results saved to: %BUILD_DIR%\Release\basic_benchmarks_results.json
echo.

echo ===== Performance Targets =====
echo Target: Order processing - ^>1M orders/second
echo Target: Matching latency - ^<500ns average
echo Target: Order book updates - ^>5M updates/second
echo.

endlocal
pause
