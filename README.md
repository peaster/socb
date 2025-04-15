# Self-Optimizing Code Benchmark (SOC-B)
This benchmark is designed to be a simple measure of an LLM's ability to make performance improvements to C source code for a specific hardware configuration. The benchmark contains four components:
- The [base benchmark source](./base/benchmark.c)
- The [system prompt](./SYSTEM_PROMPT.md). This serves as a control so that all models performing inference for this task have the same working environment for inputs.
- The modified benchmark source.
- The user prompt containing the base source and the hardware specification

Total performance of this benchmark is calculated as a percentage of the absolute values of each performance run.

- $(Modified / Base) = R_p$, where $R_p$ is the ratio of performance increase or decrease in a system created by the LLM modified code.


# Running the test
1) Compile the base benchmark and run it for your specific hardware
```bash
cd base/
gcc -o base_benchmark benchmark.c -lm -lpthread -O2
./base_benchmark
```
2) With the LLM you wish to test, use the provided [system prompt](./SYSTEM_PROMPT.md) and in the user prompt: 
  > Optimize the following C source code for my specific hardware: [paste your hardware details here]

  These hardware specs can easily be identified on Linux through `sudo lshw -C processor,memory,disk,storage`. Tweaking this part of the input may yield better or worse SOC-B performance

3) Copy the source it produces onto your machine, run the GCC command it provides you, and run the modified benchmark. 

4) Calculate your SOC-B score based on the formula below

# Base Benchmark Scoring Methodology

The base benchmark employs a comprehensive scoring system to evaluate hardware performance across three major components. It is loosely based on GeekBench's scoring mechanism. Here's how it works:

## 1. Reference-Based Normalization

The scoring uses a reference system as a baseline for comparison. The reference values include:

- CPU: 5 GFLOPS (5 billion floating-point operations per second)
- Memory Read: 10 GB/s (10 gigabytes per second)
- Memory Write: 8 GB/s (8 gigabytes per second)
- Disk Read: 500 MB/s (500 megabytes per second)
- Disk Write: 400 MB/s (400 megabytes per second)
- Disk IOPS: 5000 IOPS (5000 input/output operations per second)

A score of 1000 in any component means the system performs at exactly the reference level. A score of 2000 means it's twice as fast as the reference. A mid-range desktop PC from around 2018-2019 with these specifications would closely match the reference system:

- **CPU**: Intel Core i5-8400 or AMD Ryzen 5 2600
  - These CPUs deliver roughly 5-6 GFLOPS in general floating-point workloads

- **Memory**: 16GB DDR4-2666 RAM
  - This memory configuration typically provides read/write speeds in the 10/8 GB/s range

- **Storage**: SATA SSD (like Samsung 860 EVO)
  - SATA SSDs from this era commonly delivered sequential read/write speeds of 550/520 MB/s
  - Random 4K IOPS in the 5,000-10,000 range

  Additionally, configuration values for multithreading are set for the reference system by default.

## 2. Component Weighting

The overall score is a weighted combination of the three main hardware components:

- CPU: 40% of the total score
- Memory: 35% of the total score
- Disk: 25% of the total score

## 3. Individual Component Scoring

### CPU Score (40% of total)
- Based solely on floating-point operations per second (FLOPS)
- $S_c = 1000 × (F_m / F_r)$, where
  - $F_m$ and $F_r$ are measured and reference FLOPS respectively. 
  - $S_c$ is the aggregate CPU Score

### Memory Score (35% of total)
- Weighted combination of read and write bandwidth:
  - 60% from read bandwidth performance
  - 40% from write bandwidth performance
- $S_m = 1000 × [(R_r × 0.6) + (R_w × 0.4)]$, where
  - $R_r$ and $R_w$ are read and write ratios respectively
  - $S_m$ is the aggregate memory performance score

### Disk Score (25% of total)
- Weighted combination of three I/O metrics:
  - 40% from sequential read throughput
  - 30% from sequential write throughput
  - 30% from random access IOPS
- $S_d = 1000 × [(Read ratio × 0.4) + (Write ratio × 0.3) + (IOPS ratio × 0.3)]$
  - $R_r$ and $R_w$ are read and write ratios respectively
  - $R_i$ is the input/output operations per second (IOPS) ratio
  - $S_d$ is the aggregate disk performance score
## 4. Overall Score Calculation

The final benchmark score is calculated as:

Overall Score = $(S_c × 0.4) + (S_m × 0.35) + (S_d × 0.25)$

This methodology enables fair comparison between different hardware configurations, with higher scores indicating better performance relative to the reference system. The multi-threaded approach ensures the benchmark effectively utilizes modern multi-core processors for more realistic measurements.

You can expect the final combined score to look somewhere in this ballpark:
- Modern SBCs: 500pts - 1,000pts
- Modern midrange PCs: 2,000pts - 8,000pts
- Gaming PCs/Workstations/HPDC - 10,000pts - 12,000pts+
