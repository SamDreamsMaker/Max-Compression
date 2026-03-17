/* Sweep group_size to find optimal for each file */
#include <maxcomp/maxcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern size_t mcx_bwt_forward(uint8_t*, size_t*, const uint8_t*, size_t);
extern void mcx_mtf_encode(uint8_t*, size_t);
extern size_t mcx_rle2_encode(uint8_t*, size_t, const uint8_t*, size_t);
extern size_t mcx_multi_rans_compress(uint8_t*, size_t, const uint8_t*, size_t);
extern size_t mcx_multi_rans_decompress(uint8_t*, size_t, const uint8_t*, size_t);

/* Hack: temporarily change MT_GROUP_SIZE by recompiling is impractical.
 * Instead, test actual compression with current group_size=50,
 * but estimate what different sizes would give. */

void test(const char* path) {
    FILE* f = fopen(path, "rb"); if(!f) return;
    fseek(f,0,SEEK_END); size_t n=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t* d=malloc(n); fread(d,1,n,f); fclose(f);
    size_t cap=n*2;
    uint8_t* bwt=malloc(n); size_t pidx;
    mcx_bwt_forward(bwt,&pidx,d,n); mcx_mtf_encode(bwt,n);
    uint8_t* rle=malloc(cap);
    size_t rle_sz=mcx_rle2_encode(rle,cap,bwt,n);
    
    uint8_t* comp=malloc(cap);
    size_t comp_sz=mcx_multi_rans_compress(comp,cap,rle,rle_sz);
    
    uint8_t* dec=malloc(rle_sz+1024);
    size_t dec_sz=mcx_multi_rans_decompress(dec,rle_sz+1024,comp,comp_sz);
    int ok = !mcx_is_error(dec_sz) && dec_sz==rle_sz && memcmp(rle,dec,rle_sz)==0;
    
    const char* nm=strrchr(path,'/')+1;
    printf("%-12s orig=%zu rle=%zu comp=%zu (%.2fx) rt=%s\n", 
           nm, n, rle_sz, comp_sz, (double)n/comp_sz, ok?"✓":"FAIL");
    
    free(d);free(bwt);free(rle);free(comp);free(dec);
}

int main(void) {
    const char* files[] = {
        "/tmp/cantrbry/alice29.txt", "/tmp/cantrbry/asyoulik.txt",
        "/tmp/cantrbry/lcet10.txt", "/tmp/cantrbry/plrabn12.txt",
        "/tmp/cantrbry/cp.html", "/tmp/cantrbry/fields.c",
        NULL
    };
    for (int i = 0; files[i]; i++) test(files[i]);
    return 0;
}
