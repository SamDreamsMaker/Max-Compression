# MaxCompression v1.0.0 Professional Benchmark

**Hardware Details:** `Intel64 Family 6 Model 183 Stepping 1, GenuineIntel` on `Windows-11-10.0.26100-SP0`

| Corpus | Algorithm | Threads | Ratio | Comp Speed (MB/s) | Peak RAM (MB) | Weissman | Var Rel |
|--------|-----------|---------|-------|-------------------|---------------|----------|---------|
| calgary | **lz4-fast** | 1 | 1.94x | 575 | 1.6 | 0.90 | 2.92% |
| calgary | **lz4-hc** | 1 | 2.63x | 24 | 1.1 | 0.42 | 1.41% |
| calgary | **zstd-3** | 1 | 3.10x | 280 | 1.0 | 1.00 | 3.67% |
| calgary | **zstd-9** | 1 | 3.37x | 71 | 0.9 | 0.69 | 5.04% |
| calgary | **zstd-19** | 1 | 3.65x | 3 | 0.9 | 0.41 | 1.22% |
| calgary | **mcx-3** | 1 | 2.36x | 117 | 1.3 | 0.56 | 2.37% |
| calgary | **mcx-9** | 1 | 2.47x | 109 | 1.2 | 0.57 | 2.84% |
| calgary | **mcx-12** | 1 | 1.87x | 26 | 0.9 | 0.30 | 8.84% |
| calgary | **mcx-fast** | 1 | 1.42x | 144 | 2.1 | 0.36 | 9.02% |
| canterbury | **lz4-fast** | 1 | 2.29x | 743 | 1.2 | 1.03 | 0.05% |
| canterbury | **lz4-hc** | 1 | 3.00x | 16 | 0.0 | 0.34 | 0.06% |
| canterbury | **zstd-3** | 1 | 4.45x | 207 | 0.6 | 1.00 | 7.90% |
| canterbury | **zstd-9** | 1 | 4.73x | 40 | 0.6 | 0.65 | 0.14% |
| canterbury | **zstd-19** | 1 | 5.40x | 3 | 0.5 | 0.47 | 2.30% |
| canterbury | **mcx-3** | 1 | 2.87x | 211 | 0.9 | 0.65 | 4.14% |
| canterbury | **mcx-9** | 1 | 2.99x | 218 | 1.1 | 0.69 | 5.08% |
| canterbury | **mcx-12** | 1 | 2.36x | 52 | 0.5 | 0.35 | 1.14% |
| canterbury | **mcx-fast** | 1 | 1.43x | 272 | 1.9 | 0.36 | 0.62% |
| enwik8 | **lz4-fast** | 1 | 1.75x | 595 | 54.6 | 0.72 | 1.06% |
| enwik8 | **lz4-hc** | 1 | 2.37x | 46 | 40.2 | 0.65 | 0.04% |
| enwik8 | **zstd-3** | 1 | 2.82x | 268 | 33.8 | 1.00 | 1.72% |
| enwik8 | **zstd-9** | 1 | 3.21x | 65 | 29.7 | 0.92 | 0.09% |
| enwik8 | **zstd-19** | 1 | 3.71x | 2 | 25.7 | 0.73 | 11.04% |
| enwik8 | **mcx-3** | 1 | 2.13x | 101 | 44.7 | 0.65 | 0.42% |
| enwik8 | **mcx-9** | 1 | 2.23x | 107 | 42.7 | 0.68 | 5.21% |
| enwik8 | **mcx-12** | 1 | 2.57x | 18 | 36.1 | 0.62 | 29.72% |
| enwik8 | **mcx-fast** | 1 | 1.29x | 198 | 74.2 | 0.43 | 0.40% |
| silesia | **lz4-fast** | 1 | 2.10x | 778 | 96.2 | 0.75 | 0.82% |
| silesia | **lz4-hc** | 1 | 2.72x | 46 | 74.3 | 0.64 | 0.35% |
| silesia | **zstd-3** | 1 | 3.20x | 360 | 63.2 | 1.00 | 0.59% |
| silesia | **zstd-9** | 1 | 3.58x | 89 | 56.5 | 0.92 | 0.06% |
| silesia | **mcx-3** | 1 | 2.54x | 209 | 79.5 | 0.73 | 0.32% |
| silesia | **mcx-9** | 1 | 2.62x | 205 | 77.2 | 0.75 | 0.01% |
| silesia | **mcx-fast** | 1 | 1.55x | 264 | 130.8 | 0.46 | 0.25% |
