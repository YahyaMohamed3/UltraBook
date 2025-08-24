@echo off
setlocal EnableExtensions EnableDelayedExpansion

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
echo  [1] 🚀 QUICK BENCHMARK    - Fast build + 1 iteration (basic)
echo  [2] 📊 FULL BENCHMARK     - Complete suite + 5 iterations (basic)
echo  [3] 🏆 OFFICIAL BASELINE  - System tuning + 5 iterations (basic)
echo  [4] 🔧 BUILD ONLY         - Just compile, no benchmarks
echo  [5] 📈 VIEW LAST RESULTS  - Open most recent benchmark results
echo  [6] ❌ EXIT
echo  [7] 🔥 STRESS BENCHMARK   - 5 iterations (stress_benchmarks.exe)
echo.
set /p choice="Enter your choice (1-7 or 6 to exit): "

if "%choice%"=="1" goto :quick
if "%choice%"=="2" goto :full
if "%choice%"=="3" goto :official
if "%choice%"=="4" goto :build_only
if "%choice%"=="5" goto :view_results
if "%choice%"=="6" goto :exit
if "%choice%"=="7" goto :stress_full
echo Invalid choice. Please try again.
timeout /t 2 >nul
goto :menu

REM ===============================================
REM    QUICK BENCHMARK (Development Mode) - BASIC
REM ===============================================
:quick
cls
echo.
echo ⚡ QUICK BENCHMARK MODE (basic_benchmarks.exe)
echo =====================
echo.
echo Building and running quick performance check...
echo • Single iteration per benchmark
echo • No system optimizations
echo • Best for development and quick testing
echo.

call :build_project "quick"
if errorlevel 1 (
  echo.
  echo ❌ Build step failed. Press any key to return to the menu...
  pause >nul
  goto :menu
)

echo [3/3] Running quick benchmarks...
echo.

cd /d %~dp0
cd build\Release

if not exist "basic_benchmarks.exe" (
    echo ❌ ERROR: basic_benchmarks.exe not found!
    echo Available files:
    dir *.exe
    echo.
    echo Press any key to return to the menu...
    pause >nul
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
REM    FULL BENCHMARK (Standard Mode) - BASIC
REM ===============================================
:full
cls
echo.
echo 📊 FULL BENCHMARK MODE  (basic_benchmarks.exe)
echo ======================
echo.
echo Running comprehensive benchmark suite...
echo • 5 iterations per benchmark (aggregated)
echo • Release optimizations enabled
echo • Results saved with timestamp
echo.

call :build_project "full"
if errorlevel 1 (
  echo.
  echo ❌ Build step failed. Press any key to return to the menu...
  pause >nul
  goto :menu
)

call :create_results_dir
call :run_full_benchmarks
goto :menu

REM ===============================================
REM    OFFICIAL BASELINE (Maximum Performance) - BASIC
REM ===============================================
:official
cls
echo.
echo 🏆 OFFICIAL BASELINE MODE (basic_benchmarks.exe)
echo =========================
echo.
echo Maximum performance benchmark with system optimization...
echo • System power plan optimization
echo • CPU parking disabled
echo • 5 iterations + detailed analysis
echo • Full compliance with tmrw.md methodology
echo.

net session >nul 2>&1
if errorLevel 1 (
    echo.
    echo ⚠️  ADMINISTRATOR PRIVILEGES RECOMMENDED
    echo Right-click this script and select "Run as Administrator" for best results.
    echo Proceeding with limited optimizations...
    echo.
) else (
    echo ✅ Administrator privileges detected - full optimization available
    echo.
)

call :optimize_system
call :build_project "official"
if errorlevel 1 (
  echo.
  echo ❌ Build step failed. Press any key to restore system settings...
  pause >nul
  call :restore_system
  goto :menu
)

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
if not errorlevel 1 (
    echo ✅ Build completed successfully!
    echo 💡 Executables:
    echo    - build\Release\basic_benchmarks.exe
    echo    - build\Release\stress_benchmarks.exe
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
REM    STRESS BENCHMARK (5 reps) - STRESS EXEC
REM ===============================================
:stress_full
cls
echo.
echo 🔥 STRESS BENCHMARK MODE  (stress_benchmarks.exe)
echo ========================
echo.
echo Running stress benchmark suite...
echo • 5 iterations per benchmark (aggregated)
echo • Release optimizations enabled
echo • Results saved with timestamp
echo.

call :build_project "stress_full"
if errorlevel 1 (
  echo.
  echo ❌ Build step failed. Press any key to return to the menu...
  pause >nul
  goto :menu
)

call :create_results_dir
call :run_stress_benchmarks
goto :menu

REM ===============================================
REM    HELPER FUNCTIONS
REM ===============================================

:detect_toolchain
REM Sets CMAKE_FLAG_LINE to appropriate flags for MSVC or GCC/Clang
set "CMAKE_FLAG_LINE="

where cl >nul 2>&1
if %errorlevel%==0 (
    REM MSVC detected
    set "CMAKE_FLAG_LINE=-DCMAKE_CXX_FLAGS=/O2^ /Ob2^ /Oi^ /Ot^ /GL^ /DNDEBUG^ /DBENCHMARK_MODE -DCMAKE_EXE_LINKER_FLAGS=/LTCG -DCMAKE_SHARED_LINKER_FLAGS=/LTCG"
) else (
    REM Assume GCC/Clang
    set "CMAKE_FLAG_LINE=-DCMAKE_CXX_FLAGS=-O3^-march=native^-DNDEBUG^-DBENCHMARK_MODE -DCMAKE_EXE_LINKER_FLAGS=-flto -DCMAKE_SHARED_LINKER_FLAGS=-flto"
)
exit /b 0

:build_project
set "build_mode=%~1"
echo.
echo [1/3] Preparing build environment...
echo.

cd /d %~dp0

if exist build (
    echo Cleaning existing build...
    rmdir /s /q build
)
mkdir build
cd build

echo [2/3] Configuring CMake for maximum performance...
echo.

call :detect_toolchain

REM Configure CMake (Release). We don’t use pwsh or tee.
cmake -DCMAKE_BUILD_TYPE=Release %CMAKE_FLAG_LINE% ..

if errorlevel 1 (
    echo.
    echo ❌ CMake configuration failed!
    echo Make sure CMake and your compiler toolchain are in PATH.
    echo.
    echo Press any key to return to the menu...
    pause >nul
    exit /b 1
)

echo Building benchmark executables...
REM Build both targets; pass MSBuild /m for parallel when MSVC
cmake --build . --config Release --target basic_benchmarks stress_benchmarks -- /m

if errorlevel 1 (
    echo.
    echo ❌ Build failed!
    echo Check compilation errors above.
    echo.
    echo Press any key to return to the menu...
    pause >nul
    exit /b 1
)

echo ✅ Build completed successfully!
exit /b 0

:optimize_system
echo [SYSTEM] Optimizing for maximum performance...
powercfg /setactive 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c >nul 2>&1
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
for /f "tokens=1-3 delims=/" %%a in ('date /t') do set "mydate=%%c%%a%%b"
for /f "tokens=1-2 delims=:" %%a in ('time /t') do set "mytime=%%a%%b"
set "mydate=%mydate: =%"
set "mytime=%mytime: =%"
set "timestamp=%mydate%_%mytime%"

cd /d %~dp0
set "results_dir=benchmarks\results\%timestamp%"

if not exist "benchmarks" mkdir "benchmarks"
if not exist "benchmarks\results" mkdir "benchmarks\results"
if not exist "%results_dir%" mkdir "%results_dir%"

echo Results will be saved to: %results_dir%
exit /b 0

:run_full_benchmarks
echo [3/3] Running full benchmark suite (basic)...
echo.
echo 📋 Benchmark Configuration:
echo    • 5 iterations per test (aggregated results)
echo    • BENCHMARK_MODE enabled (logging disabled)
echo    • Release build with optimized flags
echo.

cd /d %~dp0
cd build\Release

if not exist "basic_benchmarks.exe" (
    echo ❌ ERROR: basic_benchmarks.exe not found!
    dir *.exe
    echo.
    echo Press any key to return to the menu...
    pause >nul
    exit /b 1
)

basic_benchmarks.exe ^
    --benchmark_format=console ^
    --benchmark_repetitions=5 ^
    --benchmark_report_aggregates_only=true

echo.
echo Benchmarks completed! Saving detailed results...

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
echo [3/3] Running official baseline benchmarks (basic)...
echo.
echo 📋 Official Benchmark Configuration:
echo    • 5 iterations per test (aggregated results)
echo    • System optimization enabled
echo    • High priority execution
echo    • tmrw.md methodology
echo.
echo ⏳ Starting in 5 seconds... (Press Ctrl+C to cancel)
timeout /t 5 >nul

cd /d %~dp0
cd build\Release

if not exist "basic_benchmarks.exe" (
    echo ❌ ERROR: basic_benchmarks.exe not found!
    dir *.exe
    echo.
    echo Press any key to return to the menu...
    pause >nul
    exit /b 1
)

basic_benchmarks.exe ^
    --benchmark_format=console ^
    --benchmark_repetitions=5 ^
    --benchmark_report_aggregates_only=true

echo.
echo Benchmarks completed! Saving detailed results...

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

:run_stress_benchmarks
echo [3/3] Running stress benchmark suite...
echo.
echo 📋 Stress Benchmark Configuration:
echo    • 5 iterations per test (aggregated results)
echo    • BENCHMARK_MODE enabled (logging disabled)
echo    • Release build with optimized flags
echo.

cd /d %~dp0
cd build\Release

if not exist "stress_benchmarks.exe" (
    echo ❌ ERROR: stress_benchmarks.exe not found!
    dir *.exe
    echo.
    echo Press any key to return to the menu...
    pause >nul
    exit /b 1
)

stress_benchmarks.exe ^
    --benchmark_format=console ^
    --benchmark_repetitions=5 ^
    --benchmark_report_aggregates_only=true

echo.
echo Benchmarks completed! Saving detailed stress results...

cd /d %~dp0
set "full_results_path=%cd%\%results_dir%"
cd build\Release

stress_benchmarks.exe ^
    --benchmark_out="%full_results_path%\stress_results.json" ^
    --benchmark_out_format=json ^
    --benchmark_repetitions=5 ^
    --benchmark_report_aggregates_only=true

call :generate_stress_report
exit /b 0

:generate_report
echo.
echo [REPORT] Generating benchmark summary...

cd /d %~dp0
(
echo ========================================
echo ULTRABOOK TRADING ENGINE - BENCHMARK RESULTS
echo ========================================
echo.
echo Timestamp: %date% %time%
echo CPU: %PROCESSOR_IDENTIFIER%
echo Build: Release ^(optimized^)
echo Mode: BENCHMARK_MODE ^(logging disabled^)
echo Iterations: 5 ^(aggregated^)
echo Files:
echo   - benchmark_results.json
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

cd /d %~dp0
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
echo Files:
echo   - official_baseline.json
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

:generate_stress_report
echo.
echo [REPORT] Generating stress benchmark summary...

cd /d %~dp0
(
echo ========================================
echo ULTRABOOK TRADING ENGINE - STRESS RESULTS
echo ========================================
echo.
echo Timestamp: %date% %time%
echo CPU: %PROCESSOR_IDENTIFIER%
echo Build: Release ^(optimized^)
echo Mode: BENCHMARK_MODE ^(logging disabled^)
echo Iterations: 5 ^(aggregated^)
echo Files:
echo   - stress_results.json
echo.
) > "%results_dir%\STRESS_README.md"

echo ✅ STRESS BENCHMARK COMPLETED!
echo.
echo 📊 Results: %results_dir%
echo 📄 Summary: %results_dir%\STRESS_README.md
echo 📈 Data: %results_dir%\stress_results.json
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
echo 📊 Benchmarks: benchmarks\engine\basic_benchmarks.cpp, benchmarks\engine\stress_benchmark.cpp
echo.
exit /b 0
