/**
 * Real-world benchmark: MCX Babel (L20) vs BWT (L12) vs LZ77 (L3)
 * Uses library API directly.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <maxcomp/maxcomp.h>

static uint8_t* read_file(const char* path, size_t* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = malloc(sz);
    if (!buf) { fclose(f); return NULL; }
    *out_size = fread(buf, 1, sz, f);
    fclose(f);
    return buf;
}

static double time_ms(struct timespec* start, struct timespec* end) {
    return (end->tv_sec - start->tv_sec) * 1000.0 + 
           (end->tv_nsec - start->tv_nsec) / 1e6;
}

typedef struct {
    const char* name;
    size_t orig_size;
    size_t comp_size[3];  /* L3, L12, L20 */
    double comp_time[3];
    int roundtrip[3];
} BenchResult;

static BenchResult results[100];
static int nresults = 0;

static void bench_file(const char* path, const char* name) {
    size_t src_size;
    uint8_t* src = read_file(path, &src_size);
    if (!src || src_size < 10) { if(src) free(src); return; }
    
    BenchResult* r = &results[nresults++];
    r->name = strdup(name);
    r->orig_size = src_size;
    
    int levels[] = {3, 12, 20};
    
    for (int li = 0; li < 3; li++) {
        int level = levels[li];
        size_t bound = mcx_compress_bound(src_size) + src_size;
        uint8_t* comp = malloc(bound);
        uint8_t* dec = malloc(src_size + 1024);
        if (!comp || !dec) {
            r->comp_size[li] = 0;
            if(comp) free(comp); if(dec) free(dec);
            continue;
        }
        
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        size_t csize = mcx_compress(comp, bound, src, src_size, level);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        
        r->comp_size[li] = csize;
        r->comp_time[li] = time_ms(&t0, &t1);
        
        if (csize > 0) {
            size_t dsize = mcx_decompress(dec, src_size + 1024, comp, csize);
            r->roundtrip[li] = (dsize == src_size && memcmp(src, dec, src_size) == 0) ? 1 : 0;
        } else {
            r->roundtrip[li] = -1; /* compression failed */
        }
        
        free(comp);
        free(dec);
    }
    
    printf("%-30s %8zu  L3=%7zu(%.2fx)%s  L12=%7zu(%.2fx)%s  L20=%7zu(%.2fx)%s\n",
           name, src_size,
           r->comp_size[0], r->comp_size[0] ? (double)src_size/r->comp_size[0] : 0,
           r->roundtrip[0]==1?"✓":"✗",
           r->comp_size[1], r->comp_size[1] ? (double)src_size/r->comp_size[1] : 0,
           r->roundtrip[1]==1?"✓":"✗",
           r->comp_size[2], r->comp_size[2] ? (double)src_size/r->comp_size[2] : 0,
           r->roundtrip[2]==1?"✓":"✗");
    
    free(src);
}

static void bench_dir(const char* dir, const char* prefix) {
    DIR* d = opendir(dir);
    if (!d) return;
    struct dirent* ent;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.') continue;
        char path[512], name[256];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        snprintf(name, sizeof(name), "%s/%s", prefix, ent->d_name);
        struct stat st;
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
            bench_file(path, name);
        }
    }
    closedir(d);
}

int main(void) {
    printf("═══════════════════════════════════════════════════════════════════════════════════════\n");
    printf("  MCX Real-World Benchmark: L3(LZ77) vs L12(BWT+CM-rANS) vs L20(Babel+rANS)\n");
    printf("═══════════════════════════════════════════════════════════════════════════════════════\n\n");
    
    printf("%-30s %8s  %-20s %-20s %-20s\n", "File", "Original", "L3-LZ77", "L12-BWT", "L20-Babel");
    printf("────────────────────────────────────────────────────────────────────────────────────────\n");
    
    /* Canterbury corpus */
    printf("\n── Canterbury Corpus ──\n");
    bench_dir("corpora", "cant");
    
    /* Silesia corpus */
    printf("\n── Silesia Corpus ──\n");
    bench_dir("corpora/silesia", "sil");
    
    /* Source files */
    printf("\n── Project Source ──\n");
    bench_file("lib/core.c", "src/core.c");
    bench_file("lib/babel/babel_transform.c", "src/babel_transform.c");
    bench_file("tests/test_babel.c", "src/test_babel.c");
    bench_file("README.md", "src/README.md");
    bench_file("include/maxcomp.h", "src/maxcomp.h");
    
    /* Summary */
    printf("\n═══════════════════════════════════════════════════════════════════════════════════════\n");
    printf("  Summary: Babel wins / ties / losses vs L12-BWT\n");
    printf("═══════════════════════════════════════════════════════════════════════════════════════\n");
    
    int wins = 0, losses = 0, ties = 0;
    size_t total_orig = 0, total_l3 = 0, total_l12 = 0, total_l20 = 0;
    int all_rt = 1;
    
    for (int i = 0; i < nresults; i++) {
        total_orig += results[i].orig_size;
        total_l3 += results[i].comp_size[0];
        total_l12 += results[i].comp_size[1];
        total_l20 += results[i].comp_size[2];
        if (results[i].comp_size[2] < results[i].comp_size[1]) wins++;
        else if (results[i].comp_size[2] > results[i].comp_size[1]) losses++;
        else ties++;
        if (results[i].roundtrip[2] != 1) all_rt = 0;
    }
    
    printf("  Babel wins: %d, losses: %d, ties: %d (out of %d files)\n", wins, losses, ties, nresults);
    printf("  Total: orig=%zuB L3=%zuB(%.2fx) L12=%zuB(%.2fx) L20=%zuB(%.2fx)\n",
           total_orig, total_l3, (double)total_orig/total_l3,
           total_l12, (double)total_orig/total_l12,
           total_l20, (double)total_orig/total_l20);
    printf("  All L20 roundtrips: %s\n", all_rt ? "✅ PASS" : "❌ FAIL");
    
    return 0;
}
