# 🧪 ULTRABOOK Benchmarking Baseline (May 17, 2025)

This document defines the official baseline benchmarks for ULTRABOOK's order matching engine. All future optimizations, profiling, and performance improvements will be compared against this snapshot.

---

## ✅ Goals

- Establish a reliable performance baseline across key operations.
- Use this data to validate optimizations and catch regressions.
- Standardize benchmarking practices.

---

## 🗂️ Benchmarks Included

| Benchmark                     | Input Size | Purpose                                   |
|------------------------------|------------|-------------------------------------------|
| `BM_OrderProcessingThroughput` | 10,000     | Overall throughput under realistic load    |
| `BM_OrderBookUpdates`         | 10,000     | LOB structure mutation performance         |
| `BM_OrderLookup`              | 10,000     | Lookup/search cost in active book         |
| `BM_OrderCancellation`        | 10,000     | Cancellation latency and cost             |
| `BM_MatchingLatency`          | 100 / 20   | Nanosecond-level latency per match        |
| `BM_MarketOrderExecution`     | 100        | Real-time responsiveness for market orders|
| `BM_OrderModification`        | 10,000     | Modify order latency (price/qty changes)  |

---

## ⚙️ Benchmarking Strategy

### 1. Input Size Guidelines

- Use **large batches (10,000)** for throughput-related benchmarks.
- Use **small micro-batches (100 or less)** for latency-sensitive benchmarks to prevent distortion from caching, memory allocation, and thread contention.

### 2. Run on Clean System

Before running benchmarks:

- ✅ Close all background apps (e.g., browsers, IDEs, Discord, Spotify).
- ✅ Plug in power and enable **Performance Mode** in OS settings.
- ✅ (Optional) Disable CPU throttling (via BIOS or `cpupower frequency-set`).
- ✅ (Optional) Isolate a core (e.g., `taskset` on Linux).
- ✅ (Optional) Prioritize task:
  
```bash
nice -n -20 ./basic_benchmarks.exe --benchmark_repetitions=10 ...
```

---

## 🛠️ Modify Order Benchmark Example

```cpp
static void BM_OrderModification(benchmark::State& state) {
    OrderBook book;
    std::vector<Order> orders = GenerateOrders(state.range(0)); // e.g., 10000

    for (const auto& order : orders)
        book.add(order);

    for (auto _ : state) {
        for (size_t i = 0; i < orders.size(); ++i) {
            Order& original = orders[i];
            Order modified = original;
            modified.price += 5;
            book.modify(modified);
        }
    }

    state.SetItemsProcessed(state.iterations() * orders.size());
}
BENCHMARK(BM_OrderModification)->Arg(10000);
```

---

## 📦 Benchmark Output

Run the benchmark and save results with:

```bash
./basic_benchmarks.exe \
  --benchmark_repetitions=10 \
  --benchmark_out=baseline_2025_05_17.json \
  --benchmark_out_format=json
```

---

## 🗃️ Save Metadata With the Result

Create a `README.txt` or `README.md` alongside the JSON output, containing:

```yaml
Date: 2025-05-17
CPU: Intel Core i7-12700H @ 2.6GHz, 12 cores / 20 threads
CMake Flags: -DCMAKE_BUILD_TYPE=Release -O3 -march=native
Benchmark Command: ./basic_benchmarks.exe --benchmark_repetitions=10 --benchmark_out=baseline_2025_05_17.json --benchmark_out_format=json
Code Version: commit hash abc123 (or tag: baseline-v1)
Notes: Finalized after cleanup and review. All benchmarks used calibrated input sizes. Background processes disabled. System in max performance mode.
```

Store both:

* `baseline_2025_05_17.json`
* `README.md`

in a permanent folder such as:

```
/ultrabook/benchmarks/baselines/
```

---

## 🧼 Tomorrow's Finalization Checklist

* [ ] ✅ Clean build: `Release` mode (`-O3`, `-march=native`)
* [ ] ✅ Run full benchmark suite with proper inputs
* [ ] ✅ Add `BM_OrderModification/10000` to suite
* [ ] ✅ Save output to `baseline_2025_05_17.json`
* [ ] ✅ Write `README.md` with full metadata
* [ ] ✅ Store all in `/benchmarks/baselines/`
* [ ] ✅ Run each test twice to check for stability

---

## 📈 Output Expectations

Your JSON will include:

* `real_time` (for latency): **in nanoseconds**
* `items_per_second` (for throughput): **processed ops/sec**

Use median or average for reporting. Validate deviations are within <5% between runs.

---

You now have a complete, reproducible, and industry-grade benchmark baseline for ULTRABOOK.
