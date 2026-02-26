# MaxCompression v1.0.0 Professional Benchmark

**Hardware Details:** `Intel64 Family 6 Model 183 Stepping 1, GenuineIntel` on `Windows-11-10.0.26100-SP0`

| Corpus | Algorithm | Threads | Ratio | Comp Speed (MB/s) | Peak RAM (MB) | Weissman | Var Rel |
|--------|-----------|---------|-------|-------------------|---------------|----------|---------|
| calgary | **lz4-fast** | 1 | 1.91x | 664 | 1.6 | 0.99 | 2.58% |
| calgary | **lz4-hc** | 1 | 2.57x | 42 | 1.2 | 0.47 | 3.77% |
| calgary | **zstd-3** | 1 | 2.99x | 288 | 1.0 | 1.00 | 1.96% |
| calgary | **zstd-9** | 1 | 3.24x | 76 | 1.0 | 0.69 | 2.89% |
| calgary | **zstd-19** | 1 | 3.51x | 5 | 0.9 | 0.44 | 2.68% |
| calgary | **mcx-3** | 1 | 2.28x | 112 | 1.3 | 0.54 | 1.53% |
| calgary | **mcx-9** | 1 | 2.38x | 112 | 1.3 | 0.57 | 3.98% |
| calgary | **mcx-12** | 1 | 1.84x | 46 | 0.9 | 0.35 | 1.20% |
| calgary | **mcx-fast** | 1 | 1.82x | 262 | 1.7 | 0.58 | 0.97% |
| canterbury | **lz4-fast** | 1 | 2.29x | 727 | 1.2 | 0.81 | 2.67% |
| canterbury | **lz4-hc** | 1 | 3.00x | 29 | 0.9 | 0.31 | 2.42% |
| canterbury | **zstd-3** | 1 | 4.45x | 345 | 0.7 | 1.00 | 6.14% |
| canterbury | **zstd-9** | 1 | 4.73x | 84 | 0.6 | 0.63 | 2.30% |
| canterbury | **zstd-19** | 1 | 5.40x | 5 | 0.6 | 0.40 | 2.23% |
| canterbury | **mcx-3** | 1 | 2.86x | 139 | 0.9 | 0.45 | 3.89% |
| canterbury | **mcx-9** | 1 | 2.98x | 137 | 0.9 | 0.46 | 8.12% |
| canterbury | **mcx-fast** | 1 | 2.10x | 366 | 1.3 | 0.49 | 2.75% |
| enwik8 | **lz4-fast** | 1 | 1.75x | 597 | 54.6 | 0.71 | 0.48% |
| enwik8 | **lz4-hc** | 1 | 2.37x | 46 | 40.2 | 0.64 | 0.30% |
| enwik8 | **zstd-3** | 1 | 2.82x | 273 | 33.8 | 1.00 | 0.71% |
| enwik8 | **zstd-9** | 1 | 3.21x | 66 | 29.7 | 0.92 | 0.59% |
| enwik8 | **zstd-19** | 1 | 3.71x | 2 | 25.8 | 0.73 | 9.45% |
| enwik8 | **mcx-3** | 1 | 2.12x | 106 | 44.8 | 0.65 | 1.55% |
| enwik8 | **mcx-9** | 1 | 2.22x | 105 | 42.8 | 0.68 | 1.93% |
| enwik8 | **mcx-12** | 1 | 2.76x | 18 | 34.5 | 0.67 | 25.63% |
| enwik8 | **mcx-fast** | 1 | 1.67x | 239 | 57.1 | 0.58 | 1.88% |
| silesia | **lz4-fast** | 1 | 2.10x | 789 | 96.2 | 0.75 | 1.86% |
| silesia | **lz4-hc** | 1 | 2.72x | 46 | 74.3 | 0.64 | 0.30% |
| silesia | **zstd-3** | 1 | 3.20x | 373 | 63.2 | 1.00 | 0.81% |
| silesia | **zstd-9** | 1 | 3.58x | 90 | 56.5 | 0.91 | 0.44% |
| silesia | **mcx-3** | 1 | 2.53x | 129 | 79.9 | 0.68 | 0.89% |
| silesia | **mcx-9** | 1 | 2.60x | 130 | 77.8 | 0.70 | 0.79% |
| silesia | **mcx-fast** | 1 | 2.03x | 274 | 99.5 | 0.61 | 1.88% |
