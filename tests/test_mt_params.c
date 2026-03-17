/* Test multi-table rANS with different parameters */
#include <maxcomp/maxcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern size_t mcx_bwt_forward(uint8_t*, size_t*, const uint8_t*, size_t);
extern void mcx_mtf_encode(uint8_t*, size_t);
extern size_t mcx_rle2_encode(uint8_t*, size_t, const uint8_t*, size_t);
extern size_t mcx_rans_compress(void*, size_t, const void*, size_t);

/* Parameterized multi-table rANS — just compute size, no actual encoding */
static size_t mt_estimate(const uint8_t* data, size_t n, int n_tables, int group_size) {
    int num_groups = (int)((n + group_size - 1) / group_size);
    if (num_groups < n_tables) n_tables = num_groups;
    
    /* K-means */
    int* assign = calloc(num_groups, sizeof(int));
    uint32_t (*grp_freq)[256] = calloc(num_groups, sizeof(uint32_t[256]));
    for (int g = 0; g < num_groups; g++) {
        size_t off = (size_t)g * group_size;
        size_t len = (off + group_size <= n) ? group_size : (n - off);
        for (size_t i = 0; i < len; i++) grp_freq[g][data[off + i]]++;
    }
    
    for (int g = 0; g < num_groups; g++) assign[g] = g % n_tables;
    
    uint16_t tables[8][256];
    uint32_t table_raw[8][256];
    
    for (int iter = 0; iter < 10; iter++) {
        memset(table_raw, 0, sizeof(table_raw));
        for (int g = 0; g < num_groups; g++) {
            for (int s = 0; s < 256; s++) table_raw[assign[g]][s] += grp_freq[g][s];
        }
        for (int t = 0; t < n_tables; t++) {
            uint32_t total = 0;
            for (int s = 0; s < 256; s++) total += table_raw[t][s];
            if (total == 0) continue;
            uint32_t sum = 0; int mi=0; uint32_t mv=0;
            for (int s = 0; s < 256; s++) {
                if (table_raw[t][s] > 0) {
                    tables[t][s] = (uint16_t)((uint64_t)table_raw[t][s] * 16384 / total);
                    if (tables[t][s] == 0) tables[t][s] = 1;
                    sum += tables[t][s];
                    if (table_raw[t][s] > mv) { mv = table_raw[t][s]; mi = s; }
                } else tables[t][s] = 0;
            }
            if (sum > 16384) tables[t][mi] -= (uint16_t)(sum - 16384);
            else if (sum < 16384) tables[t][mi] += (uint16_t)(16384 - sum);
        }
        int changed = 0;
        for (int g = 0; g < num_groups; g++) {
            size_t off = (size_t)g * group_size;
            size_t len = (off + group_size <= n) ? group_size : (n - off);
            double best = 1e30; int bt = 0;
            for (int t = 0; t < n_tables; t++) {
                double c = 0;
                for (size_t i = 0; i < len; i++) {
                    uint16_t f = tables[t][data[off+i]];
                    if (f == 0) { c = 1e30; break; }
                    c += 14.0 - __builtin_clz(f); /* approx log2(16384/f) */
                }
                if (c < best) { best = c; bt = t; }
            }
            if (bt != assign[g]) { assign[g] = bt; changed++; }
        }
        if (changed == 0) break;
    }
    
    /* Estimate: sum of per-group costs + table overhead + selector overhead */
    double total_bits = 0;
    memset(table_raw, 0, sizeof(table_raw));
    for (int g = 0; g < num_groups; g++) {
        for (int s = 0; s < 256; s++) table_raw[assign[g]][s] += grp_freq[g][s];
    }
    for (int t = 0; t < n_tables; t++) {
        uint32_t total = 0;
        for (int s = 0; s < 256; s++) total += table_raw[t][s];
        if (total == 0) continue;
        for (int s = 0; s < 256; s++) {
            if (table_raw[t][s] == 0) continue;
            double p = (double)table_raw[t][s] / total;
            total_bits -= table_raw[t][s] * log2(p);
        }
    }
    
    /* Table overhead: ~3 bytes per active symbol per table */
    int table_bytes = 0;
    for (int t = 0; t < n_tables; t++) {
        int active = 0;
        for (int s = 0; s < 256; s++) if (tables[t][s] > 0) active++;
        table_bytes += 1 + active * 3;
    }
    
    /* Selector overhead: ~1 bit per selector (most are 0 after MTF) */
    size_t sel_est = num_groups / 4 + 80; /* rough rANS estimate */
    
    size_t total_est = (size_t)(total_bits / 8) + table_bytes + sel_est + 80;
    
    free(assign); free(grp_freq);
    return total_est;
}

#include <math.h>

int main(void) {
    const char* files[] = {"/tmp/cantrbry/alice29.txt", "/tmp/cantrbry/lcet10.txt", NULL};
    for (int fi = 0; files[fi]; fi++) {
        FILE* f = fopen(files[fi], "rb"); if(!f) continue;
        fseek(f,0,SEEK_END); size_t n=ftell(f); fseek(f,0,SEEK_SET);
        uint8_t* d=malloc(n); fread(d,1,n,f); fclose(f);
        size_t cap=n*2;
        uint8_t* bwt=malloc(n); size_t pidx;
        mcx_bwt_forward(bwt,&pidx,d,n); mcx_mtf_encode(bwt,n);
        uint8_t* rle=malloc(cap);
        size_t rle_sz=mcx_rle2_encode(rle,cap,bwt,n);
        
        const char* nm = strrchr(files[fi],'/')+1;
        printf("%-12s rle=%zu\n", nm, rle_sz);
        
        int groups[] = {25, 50, 75, 100, 150, 200};
        int tables[] = {2, 3, 4, 6, 8};
        for (int gi = 0; gi < 6; gi++) {
            for (int ti = 0; ti < 5; ti++) {
                size_t est = mt_estimate(rle, rle_sz, tables[ti], groups[gi]);
                printf("  T=%d G=%3d: ~%zu (%.2fx)\n", tables[ti], groups[gi], est, (double)n/est);
            }
        }
        
        free(d); free(bwt); free(rle);
    }
    return 0;
}
