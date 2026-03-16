/* Test all levels on a file to find the sweet spot */
#include <maxcomp/maxcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    size_t n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = malloc(n);
    fread(data, 1, n, f);
    fclose(f);
    
    const char* name = strrchr(path, '/') + 1;
    size_t cap = n * 2 + 4096;
    uint8_t* comp = malloc(cap);
    uint8_t* dec = malloc(n);
    
    printf("%-16s %6zu bytes:\n", name, n);
    
    int levels[] = {3, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 22};
    int nlev = sizeof(levels)/sizeof(levels[0]);
    
    for (int i = 0; i < nlev; i++) {
        int lev = levels[i];
        size_t cs = mcx_compress(comp, cap, data, n, lev);
        if (mcx_is_error(cs)) {
            printf("  L%-2d: FAIL\n", lev);
            continue;
        }
        size_t ds = mcx_decompress(dec, n, comp, cs);
        int ok = (!mcx_is_error(ds) && ds == n && memcmp(data, dec, n) == 0);
        printf("  L%-2d: %6zu bytes (%.2fx) %s\n", lev, cs, (double)n/cs, ok ? "✓" : "ROUNDTRIP FAIL");
    }
    printf("\n");
    
    free(data); free(comp); free(dec);
}

int main(void) {
    test_file("/tmp/cantrbry/alice29.txt");
    test_file("/tmp/cantrbry/lcet10.txt");
    test_file("/tmp/cantrbry/kennedy.xls");
    test_file("/tmp/cantrbry/ptt5");
    return 0;
}
