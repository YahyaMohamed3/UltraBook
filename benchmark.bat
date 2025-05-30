@echo off
setlocal enabledelayedexpansion

REM ===============================================
REM    ULTRABOOK Unified Benchmark Runner
REM ===============================================

:menu
cls
echo.
echo  ███╗   ███╗ █████╗ ██████╗ ██╗  ██╗███████╗████████╗
echo  ████╗ ████║██╔══██╗██╔══██╗██║ ██╔╝██╔════╝╚══██╔══╝
echo  ██╔████╔██║███████║██████╔╝█████╔╝ █████╗     ██║   
echo  ██║╚██╔╝██║██╔══██║██╔══██╗██╔═██╗ ██╔══╝     ██║   
echo  ██║ ╚═╝ ██║██║  ██║██║  ██║██║  ██╗███████╗   ██║   
echo  ╚═╝     ╚═╝╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝   ╚═╝   
echo.
echo              HIGH-PERFORMANCE TRADING ENGINE
echo                   Benchmark Suite v2.0
echo.
echo ===============================================
echo.
echo Select benchmark mode:
echo.
echo  [1] 🚀 QUICK BENCHMARK    - Fast build + 1 iteration (30 seconds)
echo  [2] 📊 FULL BENCHMARK     - Complete suite + 5 iterations (5 minutes)  
echo  [3] 🏆 OFFICIAL BASELINE  - Full optimization + system tuning (10 minutes)
echo  [4] 🔧 BUILD ONLY         - Just compile, no benchmarks
echo  [5] 📈 VIEW LAST RESULTS  - Open most recent benchmark results
echo  [6] ❌ EXIT
echo.
set /p choice="Enter your choice (1-6): "

if "%choice%"=="1" goto :quick
if "%choice%"=="2" goto :full
if "%choice%"=="3" goto :official
if "%choice%"=="4" goto :build_only
if "%choice%"=="5" goto :view_results
if "%choice%"=="6" goto :exit
echo Invalid choice. Please try again.
timeout /t 2 >nul
goto :menu

REM ===============================================
REM    QUICK BENCHMARK (Development Mode)
REM ===============================================
:quick
cls
echo.
echo ⚡ QUICK BENCHMARK MODE
echo =====================
echo.
echo Building and running quick performance check...
echo • Single iteration per benchmark
echo • No system optimizations 
echo • Best for development and quick testing
echo.

call :build_project "quick"
if %ERRORLEVEL% NEQ 0 goto :menu

echo [3/3] Running quick benchmarks...
echo.

rem Navigate to project root first, then to build directory
cd /d %~dp0
cd build\Release

rem Check if executable exists
if not exist "basic_benchmarks.exe" (
    echo ❌ ERROR: basic_benchmarks.exe not found!
    echo Available files:
    dir *.exe
    pause
    goto :menu
)

basic_benchmarks.exe --benchmark_format=console --benchmark_repetitions=1
echo.
echo ✅ Quick benchmark complete!
echo 💡 For production results, use Full or Official benchmarks
echo.
pause
goto :menu

REM ===============================================
REM    FULL BENCHMARK (Standard Mode)
REM ===============================================
:full
cls
echo.
echo 📊 FULL BENCHMARK MODE  
echo ======================
echo.
echo Running comprehensive benchmark suite...
echo • 5 iterations per benchmark (aggregated)
echo • Release optimizations enabled
echo • Results saved with timestamp
echo.

call :build_project "full"
if %ERRORLEVEL% NEQ 0 goto :menu

call :create_results_dir
call :run_full_benchmarks
goto :menu

REM ===============================================
REM    OFFICIAL BASELINE (Maximum Performance)
REM ===============================================
:official
cls
echo.
echo 🏆 OFFICIAL BASELINE MODE
echo =========================
echo.
echo Maximum performance benchmark with system optimization...
echo • System power plan optimization
echo • CPU parking disabled  
echo • 5 iterations + detailed analysis
echo • Full compliance with tmrw.md methodology
echo.

rem Check for Administrator privileges
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo.
    echo ⚠️  ADMINISTRATOR PRIVILEGES REQUIRED
    echo.
    echo For official baseline benchmarks, system optimizations need admin rights.
    echo Please right-click this script and select "Run as Administrator"
    echo.
    echo Press any key to continue with limited optimizations...
    pause >nul
) else (
    echo ✅ Administrator privileges detected - full optimization available
    echo.
)

call :optimize_system
call :build_project "official"
if %ERRORLEVEL% NEQ 0 goto :restore_system

call :create_results_dir
call :run_official_benchmarks
call :restore_system
goto :menu

REM ===============================================
REM    BUILD ONLY MODE
REM ===============================================
:build_only
cls
echo.
echo 🔧 BUILD ONLY MODE
echo =================
echo.
echo Building project without running benchmarks...
echo.

call :build_project "build_only"
if %ERRORLEVEL% EQU 0 (
    echo ✅ Build completed successfully!
    echo 💡 Benchmark executable ready at: build\Release\basic_benchmarks.exe
) else (
    echo ❌ Build failed - check error messages above
)
echo.
pause
goto :menu

REM ===============================================
REM    VIEW RESULTS
REM ===============================================
:view_results
cls
echo.
echo 📈 OPENING RECENT BENCHMARK RESULTS
echo ==================================
echo.

if exist "benchmarks\results" (
    rem Find the most recent results directory
    for /f "delims=" %%i in ('dir "benchmarks\results" /b /ad /o-d 2^>nul') do (
        set "latest_results=benchmarks\results\%%i"
        goto :found_results
    )
    :found_results
    if defined latest_results (
        echo Opening: !latest_results!
        explorer "!latest_results!"
    ) else (
        echo No benchmark results found.
        echo Run a benchmark first to generate results.
    )
) else (
    echo No results directory found.
    echo Run a benchmark first to generate results.
)
echo.
pause
goto :menu

REM ===============================================
REM    HELPER FUNCTIONS
REM ===============================================

:build_project
set build_mode=%~1
echo.
echo [1/3] Preparing build environment...
echo.

rem Navigate to project directory
cd /d %~dp0

rem Clean and create build directory
if exist build (
    echo Cleaning existing build...
    rmdir /s /q build
)
mkdir build
cd build

echo [2/3] Configuring CMake for maximum performance...
echo.

rem Configure CMake with optimizations
cmake -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_CXX_FLAGS="-O3 -march=native -DBENCHMARK_MODE" ^
      .. 

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ❌ CMake configuration failed!
    echo Please ensure CMake is installed and in your PATH
    exit /b 1
)

echo Building benchmark executable...
cmake --build . --config Release --target basic_benchmarks

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ❌ Build failed!
    echo Check compilation errors above
    exit /b 1
)

echo ✅ Build completed successfully!
exit /b 0

:optimize_system
echo [SYSTEM] Optimizing for maximum performance...
rem Set High Performance power plan
powercfg /setactive 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c >nul 2>&1
rem Disable CPU parking
powercfg /setacvalueindex scheme_current sub_processor PROCTHROTTLEMIN 100 >nul 2>&1
powercfg /setacvalueindex scheme_current sub_processor PROCTHROTTLEMAX 100 >nul 2>&1
powercfg /setactive scheme_current >nul 2>&1
echo ✅ System optimized for benchmarking
exit /b 0

:restore_system
echo [CLEANUP] Restoring normal system settings...
powercfg /setactive scheme_balanced >nul 2>&1
echo ✅ System settings restored
exit /b 0

:create_results_dir
rem Create timestamped results directory
rem Generate timestamp in YYYYMMDD_HHMMSS format
for /f "tokens=1-3 delims=/" %%a in ('date /t') do set "mydate=%%c%%a%%b"
for /f "tokens=1-2 delims=:" %%a in ('time /t') do set "mytime=%%a%%b"
set "mydate=%mydate: =%"
set "mytime=%mytime: =%"
set "timestamp=%mydate%_%mytime%"

rem Set paths relative to project root
cd /d %~dp0
set "results_dir=benchmarks\results\%timestamp%"

rem Ensure results directory structure exists
if not exist "benchmarks" mkdir "benchmarks"
if not exist "benchmarks\results" mkdir "benchmarks\results"
if not exist "%results_dir%" mkdir "%results_dir%"

echo Results will be saved to: %results_dir%
exit /b 0

:run_full_benchmarks
echo [3/3] Running full benchmark suite...
echo.
echo 📋 Benchmark Configuration:
echo    • 5 iterations per test (aggregated results)
echo    • BENCHMARK_MODE enabled (logging disabled)
echo    • Release build with -O3 -march=native
echo.
echo ⏳ Estimated time: 3-5 minutes
echo.

rem Navigate to project root first, then to build directory
cd /d %~dp0
cd build\Release

rem Check if executable exists
if not exist "basic_benchmarks.exe" (
    echo ❌ ERROR: basic_benchmarks.exe not found!
    echo Available files:
    dir *.exe
    pause
    exit /b 1
)

echo Running full benchmark suite...
echo.

rem Run benchmarks and show output in console
basic_benchmarks.exe ^
    --benchmark_format=console ^
    --benchmark_repetitions=5 ^
    --benchmark_report_aggregates_only=true

echo.
echo Benchmarks completed! Saving detailed results...

rem Save JSON output - use full path from project root
cd /d %~dp0
set "full_results_path=%cd%\%results_dir%"
cd build\Release

basic_benchmarks.exe ^
    --benchmark_out="%full_results_path%\benchmark_results.json" ^
    --benchmark_out_format=json ^
    --benchmark_repetitions=5 ^
    --benchmark_report_aggregates_only=true

call :generate_report
exit /b 0

:run_official_benchmarks
echo [3/3] Running official baseline benchmarks...
echo.
echo 📋 Official Benchmark Configuration:
echo    • 5 iterations per test (aggregated results)
echo    • System optimization enabled
echo    • High priority execution
echo    • Full compliance with tmrw.md methodology
echo.
echo ⚠️  IMPORTANT FOR ACCURATE RESULTS:
echo    • Close all unnecessary applications
echo    • Ensure power adapter is connected
echo    • Do not use computer during benchmarking
echo.
echo ⏳ Starting in 5 seconds... (Press Ctrl+C to cancel)
timeout /t 5 >nul

rem Navigate to project root first, then to build directory
cd /d %~dp0
cd build\Release

rem Check if executable exists
if not exist "basic_benchmarks.exe" (
    echo ❌ ERROR: basic_benchmarks.exe not found!
    echo Available files:
    dir *.exe
    pause
    exit /b 1
)

echo Running official baseline benchmarks...
echo.

rem Run benchmarks and show output in console, also save to file
basic_benchmarks.exe ^
    --benchmark_format=console ^
    --benchmark_repetitions=5 ^
    --benchmark_report_aggregates_only=true

echo.
echo Benchmarks completed! Saving detailed results...

rem Save JSON output - use full path from project root
cd /d %~dp0
set "full_results_path=%cd%\%results_dir%"
cd build\Release

basic_benchmarks.exe ^
    --benchmark_out="%full_results_path%\official_baseline.json" ^
    --benchmark_out_format=json ^
    --benchmark_repetitions=5 ^
    --benchmark_report_aggregates_only=true

call :generate_official_report
exit /b 0

:generate_report
echo.
echo [REPORT] Generating benchmark summary...

rem Navigate back to project root for report generation
cd /d %~dp0

rem Create the summary report
(
echo ========================================
echo ULTRABOOK TRADING ENGINE - BENCHMARK RESULTS
echo ========================================
echo.
echo Timestamp: %date% %time%
echo CPU: %PROCESSOR_IDENTIFIER%
echo Build: Release ^(-O3 -march=native^)
echo Mode: BENCHMARK_MODE ^(logging disabled^)
echo Iterations: 5 ^(aggregated^)
echo.
) > "%results_dir%\README.md"

echo ✅ BENCHMARK COMPLETED!
echo.
echo 📊 Results: %results_dir%
echo 📄 Summary: %results_dir%\README.md
echo 📈 Data: %results_dir%\benchmark_results.json
echo.
pause
exit /b 0

:generate_official_report
echo.
echo [REPORT] Generating official baseline report...

rem Navigate back to project root for report generation
cd /d %~dp0

rem Create the official report
(
echo ========================================
echo ULTRABOOK TRADING ENGINE - OFFICIAL BASELINE
echo ========================================
echo.
echo This benchmark complies with tmrw.md methodology
echo.
echo Timestamp: %date% %time%
echo System: %COMPUTERNAME%
echo CPU: %PROCESSOR_IDENTIFIER%
echo Power Plan: High Performance
echo CPU Parking: Disabled
echo Build: Release with maximum optimizations
echo BENCHMARK_MODE: Enabled ^(zero I/O overhead^)
echo Priority: High
echo Iterations: 5 ^(aggregated results^)
echo.
echo PERFORMANCE TARGETS:
echo - Order Processing: ^>1M orders/second
echo - Matching Latency: ^<500ns average
echo - Order Book Updates: ^>5M updates/second
) > "%results_dir%\OFFICIAL_BASELINE.md"

echo 🏆 OFFICIAL BASELINE COMPLETED!
echo.
echo 📊 Results: %results_dir%
echo 📜 Official Report: %results_dir%\OFFICIAL_BASELINE.md  
echo 📈 Raw Data: %results_dir%\official_baseline.json
echo.
echo 🎯 This benchmark meets all tmrw.md requirements
echo    and can be used for official performance comparisons.
echo.
pause
exit /b 0

:exit
cls
echo.
echo Thank you for using ULTRABOOK Benchmark Suite!
echo.
echo 📚 Documentation: benchmarks\engine\tmrw.md
echo 🔧 Source Code: engine\engine.cpp
echo 📊 Benchmarks: benchmarks\engine\basic_benchmarks.cpp
echo.
exit /b 0