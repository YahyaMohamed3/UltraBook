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
echo Target: ^>1,000,000 orders/second
echo.

echo Benchmark Results:
type %RESULTS_FILE% | findstr /C:"BM_OrderProcessingThroughput" | findstr /C:"name"
type %RESULTS_FILE% | findstr /C:"BM_OrderProcessingThroughput" | findstr /C:"real_time"
type %RESULTS_FILE% | findstr /C:"BM_OrderProcessingThroughput" | findstr /C:"items_per_second"
echo.

echo Matching Latency:
echo ----------------
echo Target: ^<500ns average latency per match
echo.

echo Benchmark Results:
type %RESULTS_FILE% | findstr /C:"BM_MatchingLatency" | findstr /C:"name"
type %RESULTS_FILE% | findstr /C:"BM_MatchingLatency" | findstr /C:"real_time"
type %RESULTS_FILE% | findstr /C:"BM_MatchingLatency" | findstr /C:"cpu_time"
echo.

echo Order Book Updates:
echo -----------------
echo Target: ^>5,000,000 updates/second
echo.

echo Benchmark Results:
type %RESULTS_FILE% | findstr /C:"BM_OrderBookUpdates" | findstr /C:"name"
type %RESULTS_FILE% | findstr /C:"BM_OrderBookUpdates" | findstr /C:"real_time"
type %RESULTS_FILE% | findstr /C:"BM_OrderBookUpdates" | findstr /C:"items_per_second"
echo.

echo Order Lookup Performance:
echo ----------------------
echo Target: ^<100ns per lookup
echo.

echo Benchmark Results:
type %RESULTS_FILE% | findstr /C:"BM_OrderLookup" | findstr /C:"name"
type %RESULTS_FILE% | findstr /C:"BM_OrderLookup" | findstr /C:"real_time"
echo.

echo Order Cancellation Performance:
echo ---------------------------
echo Target: ^<1μs per cancellation
echo.

echo Benchmark Results:
type %RESULTS_FILE% | findstr /C:"BM_OrderCancellation" | findstr /C:"name"
type %RESULTS_FILE% | findstr /C:"BM_OrderCancellation" | findstr /C:"real_time"
type %RESULTS_FILE% | findstr /C:"BM_OrderCancellation" | findstr /C:"items_per_second"
echo.

echo Market Order Execution:
echo --------------------
echo Target: ^<10μs per market order execution
echo.

echo Benchmark Results:
type %RESULTS_FILE% | findstr /C:"BM_MarketOrderExecution" | findstr /C:"name"
type %RESULTS_FILE% | findstr /C:"BM_MarketOrderExecution" | findstr /C:"real_time"
type %RESULTS_FILE% | findstr /C:"BM_MarketOrderExecution" | findstr /C:"items_per_second"
echo.

echo ===== Performance Analysis Complete =====
echo.
echo For detailed results, examine the JSON file: %RESULTS_FILE%
echo To run more comprehensive benchmarks, modify benchmark sizes in basic_benchmarks.cpp

endlocal
pause
