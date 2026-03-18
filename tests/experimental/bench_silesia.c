/* Silesia Corpus Benchmark */
#include <maxcomp/maxcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void bench_file(const char* path, const char* name) {
    FILE* f = fopen(path, "rb");
    if (!f) { printf("  %-10s  SKIP (not found)\n", name); return; }
    fseek(f, 0, SEEK_END);
    size_t n = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    /* Skip files > 64MB for memory reasons */
    if (n > 64 * 1024 * 1024) {
        printf("  %-10s %7zuKB  SKIP (>64MB)\n", name, n/1024);
        fclose(f);
        return;
    }
    
    uint8_t* data = (uint8_t*)malloc(n);
    if (!data) { fclose(f); return; }
    fread(data, 1, n, f);
    fclose(f);
    
    size_t cap = n + (n / 4) + 65536;
    uint8_t* comp = (uint8_t*)malloc(cap);
    uint8_t* dec = (uint8_t*)malloc(n + 1024);
    if (!comp || !dec) { free(data); free(comp); free(dec); return; }
    
    /* L12 */
    size_t c12 = mcx_compress(comp, cap, data, n, 12);
    const char* rt12 = "ERR";
    if (!mcx_is_error(c12)) {
        size_t d12 = mcx_decompress(dec, n + 1024, comp, c12);
        rt12 = (!mcx_is_error(d12) && d12 == n && memcmp(data, dec, n) == 0) ? "OK" : "FAIL";
    }
    
    /* L20 */
    size_t c20 = mcx_compress(comp, cap, data, n, 20);
    const char* rt20 = "ERR";
    if (!mcx_is_error(c20)) {
        size_t d20 = mcx_decompress(dec, n + 1024, comp, c20);
        rt20 = (!mcx_is_error(d20) && d20 == n && memcmp(data, dec, n) == 0) ? "OK" : "FAIL";
    }
    
    printf("  %-10s %7zuKB | L12=%6zu (%5.2fx) [%s] | L20=%6zu (%5.2fx) [%s]\n",
           name, n/1024,
           mcx_is_error(c12) ? 0 : c12, mcx_is_error(c12) ? 0.0 : (double)n/c12, rt12,
           mcx_is_error(c20) ? 0 : c20, mcx_is_error(c20) ? 0.0 : (double)n/c20, rt20);
    
    free(data); free(comp); free(dec);
}

int main(void) {
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  MaxCompression — Silesia Corpus Benchmark                  ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    
    bench_file("/tmp/silesia/dickens", "dickens");
    bench_file("/tmp/silesia/xml", "xml");
    bench_file("/tmp/silesia/ooffice", "ooffice");
    bench_file("/tmp/silesia/reymont", "reymont");
    bench_file("/tmp/silesia/sao", "sao");
    bench_file("/tmp/silesia/x-ray", "x-ray");
    bench_file("/tmp/silesia/mr", "mr");
    bench_file("/tmp/silesia/osdb", "osdb");
    bench_file("/tmp/silesia/nci", "nci");
    bench_file("/tmp/silesia/samba", "samba");
    bench_file("/tmp/silesia/webster", "webster");
    bench_file("/tmp/silesia/mozilla", "mozilla");
    
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    return 0;
}
