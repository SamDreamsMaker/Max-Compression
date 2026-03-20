/**
 * @file test_regression.c
 * @brief Compression ratio regression test.
 *
 * Ensures that known inputs produce exact expected compressed sizes.
 * If the output size changes, either compression improved (update the
 * expected value) or a bug was introduced.
 *
 * This catches silent ratio regressions that roundtrip tests miss.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <maxcomp/maxcomp.h>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); \
        return 1; \
    } \
} while(0)

/* ─── Expected sizes ─────────────────────────────────────────────────
 * Update these ONLY when compression legitimately improves.
 * Each change should be documented in the commit message.
 * ──────────────────────────────────────────────────────────────────── */

#define ALICE29_SIZE          152089
#define ALICE29_L12_EXPECTED  43154

/* ─── Helpers ────────────────────────────────────────────────────────── */

static uint8_t* load_file(const char* path, size_t* out_size)
{
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0) { fclose(f); return NULL; }

    uint8_t* buf = (uint8_t*)malloc((size_t)len);
    if (!buf) { fclose(f); return NULL; }

    size_t read = fread(buf, 1, (size_t)len, f);
    fclose(f);

    if (read != (size_t)len) { free(buf); return NULL; }
    *out_size = (size_t)len;
    return buf;
}

/* ─── Test: alice29.txt at L12 must produce exactly 43154 bytes ────── */

static int test_alice29_l12(const char* corpus_dir)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/alice29.txt", corpus_dir);

    size_t src_size = 0;
    uint8_t* src = load_file(path, &src_size);
    if (!src) {
        fprintf(stderr, "SKIP: cannot open %s\n", path);
        return 0;  /* skip, don't fail — file may not be present in CI */
    }

    TEST_ASSERT(src_size == ALICE29_SIZE,
        "alice29.txt size mismatch (corrupt or wrong file?)");

    size_t dst_cap = mcx_compress_bound(src_size);
    uint8_t* compressed = (uint8_t*)malloc(dst_cap);
    TEST_ASSERT(compressed != NULL, "malloc compressed");

    /* Compress at L12 */
    size_t comp_size = mcx_compress(compressed, dst_cap,
                                     src, src_size, 12);
    TEST_ASSERT(!mcx_is_error(comp_size), "compression failed");

    printf("alice29.txt L12: %zu bytes (expected %d)\n",
           comp_size, ALICE29_L12_EXPECTED);

    if (comp_size != ALICE29_L12_EXPECTED) {
        if (comp_size < ALICE29_L12_EXPECTED) {
            fprintf(stderr,
                "NOTE: Compressed size IMPROVED (%zu < %d). "
                "Update ALICE29_L12_EXPECTED in test_regression.c!\n",
                comp_size, ALICE29_L12_EXPECTED);
        } else {
            fprintf(stderr,
                "FAIL: Compression REGRESSED (%zu > %d). "
                "Investigate before updating the expected value.\n",
                comp_size, ALICE29_L12_EXPECTED);
        }
        free(src);
        free(compressed);
        return 1;
    }

    /* Roundtrip verification */
    uint8_t* decompressed = (uint8_t*)malloc(src_size);
    TEST_ASSERT(decompressed != NULL, "malloc decompressed");

    size_t dec_size = mcx_decompress(decompressed, src_size,
                                      compressed, comp_size);
    TEST_ASSERT(!mcx_is_error(dec_size), "decompression failed");
    TEST_ASSERT(dec_size == src_size, "decompressed size mismatch");
    TEST_ASSERT(memcmp(src, decompressed, src_size) == 0,
        "roundtrip data mismatch");

    printf("  Roundtrip OK (3.53x ratio)\n");

    free(decompressed);
    free(compressed);
    free(src);
    return 0;
}

/* ─── Main ───────────────────────────────────────────────────────────── */

int main(int argc, char** argv)
{
    /* Corpus directory: passed as arg or default to ../corpora */
    const char* corpus_dir = (argc > 1) ? argv[1] : CORPUS_DIR;

    printf("=== MCX Regression Tests ===\n");
    printf("Corpus: %s\n", corpus_dir);
    printf("MCX version: %s\n\n", mcx_version_string());

    int failures = 0;

    failures += test_alice29_l12(corpus_dir);

    printf("\n=== %s ===\n", failures ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return failures ? 1 : 0;
}
