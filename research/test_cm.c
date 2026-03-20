/**
 * Test harness for CM engine
 * Usage: test_cm <input_file>
 * Compresses, decompresses, verifies roundtrip, reports ratio.
 */
#include "../lib/cm/cm.h"
#include "../lib/cm/cm.c"  /* Include directly for standalone build */
#include <stdio.h>
#include <time.h>

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "Usage: %s <file>\n", argv[0]); return 1; }
    
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror("open"); return 1; }
    fseek(f, 0, SEEK_END); size_t size = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* data = malloc(size);
    if (fread(data, 1, size, f) != size) { fclose(f); free(data); return 1; }
    fclose(f);
    
    printf("=== CM Engine Test ===\n");
    printf("Input: %s (%zu bytes)\n\n", argv[1], size);
    
    /* Compress */
    size_t comp_cap = size + (size / 4) + 1024;
    uint8_t* comp = malloc(comp_cap);
    
    clock_t t0 = clock();
    size_t comp_size = mcx_cm_compress(comp, comp_cap, data, size);
    double comp_time = (double)(clock() - t0) / CLOCKS_PER_SEC;
    
    if (comp_size == 0) {
        printf("ERROR: Compression failed!\n");
        free(data); free(comp);
        return 1;
    }
    
    printf("Compressed: %zu bytes (%.4f bpb, %.3fx)\n", comp_size,
           (double)comp_size * 8.0 / size, (double)size / comp_size);
    printf("Compress time: %.2fs (%.1f KB/s)\n\n", comp_time, size / 1024.0 / comp_time);
    
    /* Decompress */
    uint8_t* decomp = malloc(size);
    
    t0 = clock();
    size_t dec_size = mcx_cm_decompress(decomp, size, comp, comp_size);
    double dec_time = (double)(clock() - t0) / CLOCKS_PER_SEC;
    
    if (dec_size != size) {
        printf("ERROR: Decompressed size %zu != original %zu\n", dec_size, size);
        free(data); free(comp); free(decomp);
        return 1;
    }
    
    /* Verify */
    int ok = (memcmp(data, decomp, size) == 0);
    printf("Roundtrip: %s\n", ok ? "✅ PASS" : "❌ FAIL");
    printf("Decompress time: %.2fs (%.1f KB/s)\n\n", dec_time, size / 1024.0 / dec_time);
    
    if (!ok) {
        /* Find first difference */
        for (size_t i = 0; i < size; i++) {
            if (data[i] != decomp[i]) {
                printf("First diff at byte %zu: expected 0x%02X, got 0x%02X\n",
                       i, data[i], decomp[i]);
                break;
            }
        }
    }
    
    printf("=== Summary ===\n");
    printf("  Input:      %zu bytes\n", size);
    printf("  Compressed: %zu bytes (%.3fx, %.4f bpb)\n", 
           comp_size, (double)size/comp_size, (double)comp_size*8.0/size);
    printf("  Roundtrip:  %s\n", ok ? "PASS" : "FAIL");
    
    free(data); free(comp); free(decomp);
    return ok ? 0 : 1;
}
