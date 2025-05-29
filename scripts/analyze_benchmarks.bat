@echo off
setlocal enabledelayedexpansion

echo ===============================================
echo     ULTRABOOK Benchmark Results Analyzer
echo ===============================================
echo This script analyzes benchmark results and compares against performance targets
echo Based on tmrw.md methodology and requirements
echo.

set RESULTS_FILE=%1
if "%RESULTS_FILE%"=="" (
    echo Usage: %~nx0 [results_file.json]
    echo.
    echo Searching for most recent results...
    
    rem Find the most recent results file
    if exist "..\..\benchmarks\results" (
        for /f "delims=" %%i in ('dir "..\..\benchmarks\results" /b /ad /o-d 2^>nul') do (
            set "latest_dir=..\..\benchmarks\results\%%i"
            goto :found_dir
        )
        :found_dir
        if exist "!latest_dir!\benchmark_results.json" (
            set RESULTS_FILE=!latest_dir!\benchmark_results.json
            echo Found: !RESULTS_FILE!
        ) else if exist "!latest_dir!\official_baseline.json" (
            set RESULTS_FILE=!latest_dir!\official_baseline.json
            echo Found: !RESULTS_FILE!
        ) else (
            set RESULTS_FILE=basic_benchmarks_results.json
            echo Using default: !RESULTS_FILE!
        )
    ) else (
        set RESULTS_FILE=basic_benchmarks_results.json
        echo Using default: !RESULTS_FILE!
    )
)

cd /d %~dp0
if exist build\Release cd build\Release

if not exist "%RESULTS_FILE%" (
    echo.
    echo ❌ Error: Results file %RESULTS_FILE% not found.
    echo.
    echo 💡 Please run benchmarks first using one of these methods:
    echo    • scripts\benchmark.bat (unified benchmark runner)
    echo    • Or run the basic_benchmarks.exe manually with JSON output
    echo.
    pause
    exit /b 1
)

echo ===============================================
echo 📊 PERFORMANCE ANALYSIS REPORT
echo ===============================================
echo.
echo 📁 Results file: %RESULTS_FILE%
echo ⏰ Analysis date: %date% %time%
echo.

echo ===============================================
echo 🚀 1. ORDER PROCESSING THROUGHPUT
echo ===============================================
echo 🎯 Target: ^>1,000,000 orders/second
echo 📋 Test: BM_OrderProcessingThroughput/10000
echo.

findstr /C:"BM_OrderProcessingThroughput" "%RESULTS_FILE%" | findstr /C:"\"name\"" | head -1
echo.
echo ⏱️ Timing Results:
findstr /C:"BM_OrderProcessingThroughput" "%RESULTS_FILE%" | findstr /C:"\"real_time\"" | head -1
findstr /C:"BM_OrderProcessingThroughput" "%RESULTS_FILE%" | findstr /C:"\"cpu_time\"" | head -1
echo.
echo 📈 Throughput Results:
findstr /C:"BM_OrderProcessingThroughput" "%RESULTS_FILE%" | findstr /C:"\"items_per_second\"" | head -1
echo.

echo ===============================================
echo ⚡ 2. MATCHING LATENCY
echo ===============================================
echo 🎯 Target: ^<500ns average latency per match
echo 📋 Test: BM_MatchingLatency/100/20
echo.

findstr /C:"BM_MatchingLatency" "%RESULTS_FILE%" | findstr /C:"\"name\"" | head -1
echo.
echo ⏱️ Timing Results:
findstr /C:"BM_MatchingLatency" "%RESULTS_FILE%" | findstr /C:"\"real_time\"" | head -1
findstr /C:"BM_MatchingLatency" "%RESULTS_FILE%" | findstr /C:"\"cpu_time\"" | head -1
echo.

echo ===============================================
echo 📚 3. ORDER BOOK UPDATES
echo ===============================================
echo 🎯 Target: ^>5,000,000 updates/second
echo 📋 Test: BM_OrderBookUpdates/10000
echo.

findstr /C:"BM_OrderBookUpdates" "%RESULTS_FILE%" | findstr /C:"\"name\"" | head -1
echo.
echo ⏱️ Timing Results:
findstr /C:"BM_OrderBookUpdates" "%RESULTS_FILE%" | findstr /C:"\"real_time\"" | head -1
findstr /C:"BM_OrderBookUpdates" "%RESULTS_FILE%" | findstr /C:"\"cpu_time\"" | head -1
echo.
echo 📈 Throughput Results:
findstr /C:"BM_OrderBookUpdates" "%RESULTS_FILE%" | findstr /C:"\"items_per_second\"" | head -1
echo.

echo ===============================================
echo 🔍 4. ORDER LOOKUP PERFORMANCE
echo ===============================================
echo 🎯 Target: ^<100ns per lookup
echo 📋 Test: BM_OrderLookup/10000
echo.

findstr /C:"BM_OrderLookup" "%RESULTS_FILE%" | findstr /C:"\"name\"" | head -1
echo.
echo ⏱️ Timing Results:
findstr /C:"BM_OrderLookup" "%RESULTS_FILE%" | findstr /C:"\"real_time\"" | head -1
findstr /C:"BM_OrderLookup" "%RESULTS_FILE%" | findstr /C:"\"cpu_time\"" | head -1
echo.
echo 📈 Throughput Results:
findstr /C:"BM_OrderLookup" "%RESULTS_FILE%" | findstr /C:"\"items_per_second\"" | head -1
echo.

echo ===============================================
echo ❌ 5. ORDER CANCELLATION PERFORMANCE
echo ===============================================
echo 🎯 Target: ^<1μs per cancellation
echo 📋 Test: BM_OrderCancellation/10000
echo.

findstr /C:"BM_OrderCancellation" "%RESULTS_FILE%" | findstr /C:"\"name\"" | head -1
echo.
echo ⏱️ Timing Results:
findstr /C:"BM_OrderCancellation" "%RESULTS_FILE%" | findstr /C:"\"real_time\"" | head -1
findstr /C:"BM_OrderCancellation" "%RESULTS_FILE%" | findstr /C:"\"cpu_time\"" | head -1
echo.
echo 📈 Throughput Results:
findstr /C:"BM_OrderCancellation" "%RESULTS_FILE%" | findstr /C:"\"items_per_second\"" | head -1
echo.

echo ===============================================
echo 📈 6. MARKET ORDER EXECUTION
echo ===============================================
echo 🎯 Target: ^<10μs per market order execution
echo 📋 Test: BM_MarketOrderExecution/100 (20 market orders)
echo.

findstr /C:"BM_MarketOrderExecution" "%RESULTS_FILE%" | findstr /C:"\"name\"" | head -1
echo.
echo ⏱️ Timing Results:
findstr /C:"BM_MarketOrderExecution" "%RESULTS_FILE%" | findstr /C:"\"real_time\"" | head -1
findstr /C:"BM_MarketOrderExecution" "%RESULTS_FILE%" | findstr /C:"\"cpu_time\"" | head -1
echo.
echo 📈 Throughput Results:
findstr /C:"BM_MarketOrderExecution" "%RESULTS_FILE%" | findstr /C:"\"items_per_second\"" | head -1
echo.

echo ===============================================
echo 🔧 7. ORDER MODIFICATION PERFORMANCE
echo ===============================================
echo 🎯 Target: ^<1μs per modification (with price-time priority)
echo 📋 Test: BM_OrderModification/10000
echo.

findstr /C:"BM_OrderModification" "%RESULTS_FILE%" | findstr /C:"\"name\"" | head -1
echo.
echo ⏱️ Timing Results:
findstr /C:"BM_OrderModification" "%RESULTS_FILE%" | findstr /C:"\"real_time\"" | head -1
findstr /C:"BM_OrderModification" "%RESULTS_FILE%" | findstr /C:"\"cpu_time\"" | head -1
echo.
echo 📈 Throughput Results:
findstr /C:"BM_OrderModification" "%RESULTS_FILE%" | findstr /C:"\"items_per_second\"" | head -1
echo.

echo ===============================================
echo 📊 SUMMARY & RECOMMENDATIONS
echo ===============================================
echo.
echo ✅ Benchmark Analysis Complete!
echo.
echo 📋 Performance Targets (from tmrw.md):
echo    • Order Processing: ^>1M orders/second
echo    • Matching Latency: ^<500ns average
echo    • Order Book Updates: ^>5M updates/second  
echo    • Order Lookup: ^<100ns per lookup
echo    • Order Cancellation: ^<1μs per cancellation
echo    • Market Order Execution: ^<10μs per execution
echo    • Order Modification: ^<1μs per modification
echo.
echo 💡 Tips for Analysis:
echo    • real_time values are in nanoseconds (divide by 1,000 for μs)
echo    • items_per_second shows throughput performance
echo    • Lower real_time = better latency performance
echo    • Higher items_per_second = better throughput performance
echo.
echo 📈 For detailed results, examine the JSON file: %RESULTS_FILE%
echo 🔧 To modify benchmark parameters, edit: benchmarks\engine\basic_benchmarks.cpp
echo 📚 For methodology details, see: benchmarks\engine\tmrw.md
echo.

endlocal
pause
