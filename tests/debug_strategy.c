#include <maxcomp/maxcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    const char* files[] = {
        "/tmp/cantrbry/grammar.lsp",
        "/tmp/cantrbry/fields.c",
        "/tmp/cantrbry/alice29.txt",
        "/tmp/cantrbry/kennedy.xls",
        NULL
    };
    
    for (int i = 0; files[i]; i++) {
        FILE* f = fopen(files[i], "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        size_t size = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t* data = malloc(size);
        fread(data, 1, size, f);
        fclose(f);
        
        const char* name = strrchr(files[i], '/') + 1;
        
        /* Compress at different levels and check output */
        for (int level = 12; level <= 20; level += 8) {
            size_t cap = mcx_compress_bound(size) + 200000;
            uint8_t* comp = malloc(cap);
            size_t cs = mcx_compress(comp, cap, data, size, level);
            
            if (mcx_is_error(cs)) {
                printf("%-16s L%d: ERROR %s\n", name, level, mcx_get_error_name(cs));
            } else {
                /* Read strategy from frame header byte 7 */
                uint8_t strategy = comp[7];
                printf("%-16s L%d: %zu → %zu (%.2fx) strategy=%d\n",
                       name, level, size, cs, (double)size/cs, strategy);
            }
            free(comp);
        }
        free(data);
    }
    return 0;
}
