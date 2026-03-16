/**
 * @file test_babel.c
 * @brief Roundtrip test for the Babel Transform (level 20+).
 */

#include <maxcomp/maxcomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_roundtrip(const char* name, const uint8_t* data, size_t size, int level)
{
    size_t comp_cap = mcx_compress_bound(size);
    uint8_t* comp = (uint8_t*)malloc(comp_cap);
    uint8_t* decomp = (uint8_t*)malloc(size);
    
    if (!comp || !decomp) {
        printf("  [FAIL] %s: alloc failed\n", name);
        free(comp); free(decomp);
        return 1;
    }
    
    size_t comp_size = mcx_compress(comp, comp_cap, data, size, level);
    if (mcx_is_error(comp_size)) {
        printf("  [FAIL] %s (L%d): compress error: %s\n", name, level,
               mcx_get_error_name(comp_size));
        free(comp); free(decomp);
        return 1;
    }
    
    double ratio = (double)size / (double)comp_size;
    
    size_t decomp_size = mcx_decompress(decomp, size, comp, comp_size);
    if (mcx_is_error(decomp_size)) {
        printf("  [FAIL] %s (L%d): decompress error: %s\n", name, level,
               mcx_get_error_name(decomp_size));
        free(comp); free(decomp);
        return 1;
    }
    
    if (decomp_size != size) {
        printf("  [FAIL] %s (L%d): size mismatch: got %zu, expected %zu\n",
               name, level, decomp_size, size);
        free(comp); free(decomp);
        return 1;
    }
    
    if (memcmp(data, decomp, size) != 0) {
        /* Find first difference */
        for (size_t i = 0; i < size; i++) {
            if (data[i] != decomp[i]) {
                printf("  [FAIL] %s (L%d): data mismatch at offset %zu: 0x%02x vs 0x%02x\n",
                       name, level, i, data[i], decomp[i]);
                break;
            }
        }
        free(comp); free(decomp);
        return 1;
    }
    
    printf("  [OK]   %s (L%d): %zu → %zu (%.2fx)\n",
           name, level, size, comp_size, ratio);
    
    free(comp); free(decomp);
    return 0;
}

int main(void)
{
    int failures = 0;
    
    printf("═══════════════════════════════════════════════════\n");
    printf("  Babel Transform Roundtrip Tests\n");
    printf("═══════════════════════════════════════════════════\n\n");
    
    /* Test 1: English text */
    {
        const char* text = "The quick brown fox jumps over the lazy dog. "
                          "In the beginning was the Word, and the Word was with God. "
                          "To be or not to be, that is the question. ";
        size_t base_len = strlen(text);
        size_t size = base_len * 100;
        uint8_t* data = (uint8_t*)malloc(size);
        for (size_t i = 0; i < 100; i++)
            memcpy(data + i * base_len, text, base_len);
        
        failures += test_roundtrip("english_text", data, size, 20);
        failures += test_roundtrip("english_text", data, size, 22);
        /* Also test that existing levels still work */
        failures += test_roundtrip("english_text", data, size, 12);
        free(data);
    }
    
    /* Test 2: JSON-like */
    {
        const char* json = "{\"name\":\"Alice\",\"age\":30,\"city\":\"Paris\"},";
        size_t base_len = strlen(json);
        size_t size = base_len * 200;
        uint8_t* data = (uint8_t*)malloc(size);
        for (size_t i = 0; i < 200; i++)
            memcpy(data + i * base_len, json, base_len);
        
        failures += test_roundtrip("json_data", data, size, 20);
        free(data);
    }
    
    /* Test 3: Source code */
    {
        const char* code = "int main(int argc, char** argv) {\n"
                          "    printf(\"Hello World\\n\");\n"
                          "    for (int i = 0; i < n; i++) {\n"
                          "        sum += array[i];\n"
                          "    }\n    return 0;\n}\n";
        size_t base_len = strlen(code);
        size_t size = base_len * 100;
        uint8_t* data = (uint8_t*)malloc(size);
        for (size_t i = 0; i < 100; i++)
            memcpy(data + i * base_len, code, base_len);
        
        failures += test_roundtrip("source_code", data, size, 20);
        free(data);
    }
    
    /* Test 4: Binary pattern */
    {
        size_t size = 20000;
        uint8_t* data = (uint8_t*)malloc(size);
        for (size_t i = 0; i < 10000; i++)
            data[i] = (uint8_t)((i * 7 + 13) & 0xFF);
        memset(data + 10000, 0x00, 5000);
        memset(data + 15000, 0xFF, 5000);
        
        failures += test_roundtrip("binary_pattern", data, size, 20);
        free(data);
    }
    
    /* Test 5: Small data (edge case) */
    {
        const char* small = "Hello, Babel!";
        failures += test_roundtrip("small_data", (const uint8_t*)small, strlen(small), 20);
    }
    
    /* Test 6: Real file (if available) */
    {
        FILE* f = fopen("../README.md", "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            size_t size = (size_t)ftell(f);
            fseek(f, 0, SEEK_SET);
            uint8_t* data = (uint8_t*)malloc(size);
            if (data && fread(data, 1, size, f) == size) {
                failures += test_roundtrip("README.md", data, size, 12);
                failures += test_roundtrip("README.md", data, size, 20);
            }
            free(data);
            fclose(f);
        }
    }
    
    printf("\n═══════════════════════════════════════════════════\n");
    if (failures == 0) {
        printf("  ✅ All tests passed!\n");
    } else {
        printf("  ❌ %d test(s) failed!\n", failures);
    }
    printf("═══════════════════════════════════════════════════\n");
    
    return failures;
}
