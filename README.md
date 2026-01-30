# Inclusive Cache Profiler (Intel PIN Tool)

This repository contains an **Intel PIN–based cache simulator** that models a **three-level inclusive cache hierarchy**:

- **Private L1 cache per thread**
- **Private L2 cache per thread**
- **Shared, inclusive Last-Level Cache (LLC)**

The tool periodically logs cache access and miss statistics to a CSV file **for ROI code segment (for that you have to instrument source code earlier) or set the `gotROI` to `true`, it will provide you for the whole program**.

------

## ✨ Features

- Inclusive cache hierarchy: **L1 ⊆ L2 ⊆ LLC**
- Lock-free **per-thread L1/L2 caches**
- Locked **shared LLC** for correctness
- LRU replacement policy at all levels
- Supports arbitrary cache sizes, associativity, and line sizes
- Handles multi-line memory accesses correctly
- Periodic CSV logging for scalable profiling
- Assertions to enforce cache-inclusivity invariants

------

## 🧠 Cache Model Overview

| Level | Scope      | Inclusive | Locking   |
| ----- | ---------- | --------- | --------- |
| L1    | Per-thread | ⊆ L2      | Lock-free |
| L2    | Per-thread | ⊆ LLC     | Lock-free |
| LLC   | Shared     | Global    | PIN lock  |

If a cache line is evicted from:

- **L2** → it is invalidated from that thread’s L1
- **LLC** → it is invalidated from *all* threads’ L2 and L1 caches

------

## ⚠️ Install PIN tool and compile the simulator

#### Download and install PIN tool

Download the source of PIN from Intel's website, then build it in current location.

```bash
$ wget https://software.intel.com/sites/landingpage/pintool/downloads/pin-3.22-98547-g7a303a835-gcc-linux.tar.gz
$ tar zxf pin-3.22-98547-g7a303a835-gcc-linux.tar.gz
$ cd pin-3.22-98547-g7a303a835-gcc-linux/source/tools
$ make
```

#### Building the CacheProfiler

The provided Makefile will generate `obj-intel64/CacheProfiler.so`.

```bash
$ export PIN_ROOT=path to CacheProfiler/pin-3.22-98547-g7a303a835-gcc-linux
$ make
```

------

## 🏃 How It Runs

The tool instruments:

- **Every instruction** (for instruction counting)
- **Every memory read and write**

At runtime:

1. Each memory access is split into cache-line accesses
2. The cache hierarchy is probed in order: L1 → L2 → LLC
3. On misses, lines are inserted while enforcing inclusivity
4. Cache statistics are logged periodically to a CSV file

------

#### 🔧 Command-Line Arguments (PIN Knobs)

All parameters are configurable at runtime:

##### Cache Configuration

| Argument     | Description           | Default   |
| ------------ | --------------------- | --------- |
| `-l1_size`   | L1 cache size (bytes) | `32768`   |
| `-l1_assoc`  | L1 associativity      | `8`       |
| `-l1_line`   | L1 line size (bytes)  | `64`      |
| `-l2_size`   | L2 cache size (bytes) | `262144`  |
| `-l2_assoc`  | L2 associativity      | `8`       |
| `-l2_line`   | L2 line size (bytes)  | `64`      |
| `-llc_size`  | LLC size (bytes)      | `8388608` |
| `-llc_assoc` | LLC associativity     | `16`      |
| `-llc_line`  | LLC line size (bytes) | `64`      |

##### Logging

| Argument  | Description                | Default    |
| --------- | -------------------------- | ---------- |
| `-period` | Instructions per log entry | `1000000`  |
| `-output` | CSV output file            | `data.csv` |

------

## ▶️ Example Usage

```bash
$PIN_ROOT/pin -t obj-intel64/CacheProfiler.so \
    -l1_size 32768 \
    -l2_size 262144 \
    -llc_size 8388608 \
    -period 500000 \
    -output stats.csv \
    -- ./my_program
```

------

## 📄 Output Format (CSV)

Each row corresponds to one logging period **per thread**:

```csv
Thread_ID,Inst_Count,L1_Access,L1_Misses,L1 MR,L2_Access,L2_Misses,L2_MR,LLC_Access,LLC_Misses,LLC_MR
```

Where values are **deltas since the previous logging period**.

------

## 🗂 Code Structure

### Major Components

| File Section   | Responsibility                       |
| -------------- | ------------------------------------ |
| KNOBs          | Runtime configuration                |
| `CacheLevel`   | Set-associative cache model with LRU |
| `ThreadState`  | Per-thread L1/L2 state               |
| `ProcessMem()` | Core cache simulation logic          |
| PIN callbacks  | Instrumentation and lifecycle        |
| Logging        | Periodic CSV output                  |

### Key Functions

- `Instruction()` – Instruments instructions and memory accesses
- `ProcessMem()` – Simulates inclusive cache behavior
- `ThreadStart()` – Initializes per-thread caches
- `CountInst()` – Tracks instruction count
- `LogIfNeeded()` – Periodic logging
- `Fini()` – Cleanup and file close

------

## 🧪 Correctness Guarantees

The simulator enforces:

```
L1 ⊆ L2 ⊆ LLC
```

At runtime using:

- Explicit invalidations
- Assertions that detect inclusivity violations early

If an assertion fails, it indicates a **bug in cache logic**, not undefined behavior.

------

## ⚠️ Notes & Limitations

- This is a **functional cache simulator**, not cycle-accurate
- Memory latency and coherence protocols are not modeled
- LLC invalidation scans all threads (acceptable for teaching/research scale)

