#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <maxcomp/maxcomp.h>

int main(void) {
    unsigned char *src, *compressed, *decompressed;
    size_t src_size = 1000;
    size_t dst_cap, comp_size, dec_size;
    int ok;
    
    src = (unsigned char*)calloc(src_size, 1);
    dst_cap = mcx_compress_bound(src_size);
    compressed = (unsigned char*)malloc(dst_cap);
    decompressed = (unsigned char*)malloc(src_size);
    
    printf("Testing zeros (size=%zu) at level 10...\n", src_size);
    
    comp_size = mcx_compress(compressed, dst_cap, src, src_size, 10);
    if (mcx_is_error(comp_size)) {
        printf("COMPRESS ERROR: %s\n", mcx_get_error_name(comp_size));
        return 1;
    }
    printf("Compressed: %zu -> %zu\n", src_size, comp_size);
    
    dec_size = mcx_decompress(decompressed, src_size, compressed, comp_size);
    if (mcx_is_error(dec_size)) {
        printf("DECOMPRESS ERROR: %s\n", mcx_get_error_name(dec_size));
        return 1;
    }
    printf("Decompressed: %zu\n", dec_size);
    
    ok = (dec_size == src_size && memcmp(src, decompressed, src_size) == 0);
    printf("Match: %s\n", ok ? "YES" : "NO");
    
    if (!ok) {
        size_t i;
        for (i = 0; i < src_size && i < 20; i++) {
            if (src[i] != decompressed[i]) {
                printf("  Diff at %zu: expected %d, got %d\n", i, src[i], decompressed[i]);
            }
        }
    }
    
    free(src); free(compressed); free(decompressed);
    return ok ? 0 : 1;
}
