@echo off
setlocal enabledelayedexpansion

echo ===== FastChain Benchmark Results Analyzer =====
echo This script analyzes benchmark results and compares against performance targets
echo.

set RESULTS_FILE=%1
if "%RESULTS_FILE%"=="" set RESULTS_FILE=basic_benchmarks_results.json

cd /d %~dp0\build\Release

if not exist %RESULTS_FILE% (
    echo Error: Results file %RESULTS_FILE% not found.
    echo Please run the benchmarks first.
    pause
    exit /b 1
)

echo Analyzing results from: %RESULTS_FILE%
echo.

echo ===== Performance Summary =====

echo Order Processing Throughput:
echo ---------------------------
echo Finding throughput metrics...
findstr /C:"BM_OrderProcessingThroughput" %RESULTS_FILE% | findstr /C:"items_per_second"
echo.
echo Target: ^>1,000,000 orders/second
echo.

echo Matching Latency:
echo ----------------
echo Finding latency metrics...
findstr /C:"BM_MatchingLatency" %RESULTS_FILE% | findstr /C:"cpu_time"
echo.
echo Target: ^<500ns average latency
echo.

echo Order Book Updates:
echo -----------------
echo Finding update metrics...
findstr /C:"BM_OrderBookUpdates" %RESULTS_FILE% | findstr /C:"items_per_second"
echo.
echo Target: ^>5,000,000 updates/second
echo.

echo Order Lookup Performance:
echo ----------------------
echo Finding lookup metrics...
findstr /C:"BM_OrderLookup" %RESULTS_FILE% | findstr /C:"cpu_time"
echo.
echo Target: ^<100ns per lookup
echo.

echo Order Cancellation Performance:
echo ---------------------------
echo Finding cancellation metrics...
findstr /C:"BM_OrderCancellation" %RESULTS_FILE% | findstr /C:"cpu_time"
echo.
echo Target: ^<1μs per cancellation
echo.

echo Market Order Execution:
echo --------------------
echo Finding market order metrics...
findstr /C:"BM_MarketOrderExecution" %RESULTS_FILE% | findstr /C:"cpu_time"
echo.
echo Target: ^<10μs per market order
echo.

echo ===== Performance Analysis Complete =====
echo.
echo For detailed results, examine the JSON file: %RESULTS_FILE%

endlocal
pause
