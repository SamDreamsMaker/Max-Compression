#include <stdio.h>
#include <stdlib.h>

extern int mcx_babel_stride_detect(const unsigned char* src, size_t src_size);

int main(void) {
    FILE* f;
    unsigned char* data;
    long n;
    int stride;
    
    const char* files[] = {
        "/tmp/cantrbry/kennedy.xls",
        "/tmp/cantrbry/ptt5",
        "/tmp/cantrbry/alice29.txt",
        "/tmp/cantrbry/sum",
        NULL
    };
    
    for (int i = 0; files[i]; i++) {
        f = fopen(files[i], "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        n = ftell(f);
        fseek(f, 0, SEEK_SET);
        data = malloc(n);
        fread(data, 1, n, f);
        fclose(f);
        
        size_t sample = n > 65536 ? 65536 : n;
        stride = mcx_babel_stride_detect(data, sample);
        printf("%-20s (%ld bytes) → stride=%d\n", files[i]+14, n, stride);
        free(data);
    }
    return 0;
}
