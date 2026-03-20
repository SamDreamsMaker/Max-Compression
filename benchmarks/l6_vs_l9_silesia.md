# L6 vs L9 on Full Silesia Corpus

Benchmarked on Atom CPU. L6 = HC hash chain (256K, nice_match=32). L9 = HC hash chain (1M, nice_match=128).

| File     | Size (MB) | L6 Ratio | L6 Comp MB/s | L6 Dec MB/s | L9 Ratio | L9 Comp MB/s | L9 Dec MB/s | Ratio Gain | Speed Cost |
|----------|-----------|----------|-------------|-------------|----------|-------------|-------------|-----------|-----------|
| dickens  | 9.7       | 2.29x    | 7.9         | 93.3        | 2.37x    | 4.1         | 12.9        | +3.5%     | -48%      |
| mozilla  | 48.8      | 2.45x    | 11.8        | 98.7        | 2.61x    | 5.7         | 13.9        | +6.5%     | -52%      |
| mr       | 9.5       | 2.61x    | 9.8         | 100.4       | 2.80x    | 4.6         | 14.4        | +7.3%     | -53%      |
| mr.xz    | 2.6       | 1.00x    | 7.6         | 698.3       | 1.00x    | 4.1         | 707.9       | 0%        | -46%      |
| nci      | 32.0      | 8.54x    | 37.1        | 222.2       | 9.68x    | 20.2        | 46.0        | +13.3%    | -46%      |
| ooffice  | 5.9       | 1.79x    | 8.7         | 77.7        | 1.87x    | 4.9         | 10.1        | +4.5%     | -44%      |
| osdb     | 9.6       | 2.70x    | 12.6        | 121.1       | 2.86x    | 7.6         | 15.0        | +5.9%     | -40%      |
| reymont  | 6.3       | 2.93x    | 10.0        | 104.9       | 3.11x    | 4.1         | 16.7        | +6.1%     | -59%      |
| samba    | 20.6      | 3.47x    | 16.2        | 146.0       | 3.67x    | 8.6         | 20.2        | +5.8%     | -47%      |
| sao      | 6.9       | 1.28x    | 6.5         | 76.4        | 1.35x    | 3.5         | 7.6         | +5.5%     | -46%      |
| webster  | 39.5      | 2.91x    | 11.2        | 111.1       | 3.03x    | 6.0         | 16.2        | +4.1%     | -46%      |
| xml      | 5.1       | 6.35x    | 25.6        | 193.4       | 6.80x    | 13.5        | 35.5        | +7.1%     | -47%      |
| x-ray    | 8.1       | 1.31x    | 7.2         | 68.2        | 1.41x    | 4.2         | 7.8         | +7.6%     | -42%      |

## Summary

- **Average ratio gain L9 over L6: +6.3%** (excluding incompressible mr.xz)
- **Average compress speed cost: -47%** (L9 is about half the speed of L6)
- **Average decompress speed cost: -85%** (L9 decompress is significantly slower due to longer match chains in output)
- **Best ratio gain**: nci (+13.3%) — highly repetitive chemical data benefits most from deeper chain search
- **When L9 is worth it**: When you need ~5-13% better ratio and can tolerate 2× slower compression and ~7× slower decompression. Best for archival use where decode speed doesn't matter.
- **When to stick with L6**: When decompress speed matters (streaming, real-time), or when compress time budget is tight. L6 offers the best speed/ratio balance in the LZ range.
