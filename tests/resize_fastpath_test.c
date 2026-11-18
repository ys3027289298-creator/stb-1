// Test + benchmark for the small-image fast paths in stb_image_resize.h
// (see "FAST PATHS" in that file). Self-contained: samples are generated
// deterministically, everything runs in one go.
//
// Build (from the repo root, no Makefile changes needed):
//   cc -O2 -Ideprecated tests/resize_fastpath_test.c tests/resize_fastpath_ref.c -lm -o resize_fastpath_test
//
// Toggle comparison builds (same command plus one of):
//   -DSTBIR_NO_FAST_PATHS          all fast paths off (baseline behavior)
//   -DSTBIR_NO_FAST_PATH_INT_NN    integer-scale NN path off
//   -DSTBIR_NO_FAST_PATH_GRAY      grayscale/palette path off
//   -DSTBIR_NO_GENERAL_FALLBACK    fallback off; run as `./resize_fastpath_test int`
//
// Metrics reported per case, fast-path build vs. reference build (fast
// paths disabled): per-pixel consistency, time, extra allocations
// (must be 0 for fast-path hits), fallback hit count.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ---- instrumentation hooks into the library -------------------------------

static int g_alloc_calls = 0;
static int g_assert_calls = 0;

static void* test_malloc(size_t size, void* context) { (void)context; g_alloc_calls++; return malloc(size); }
static void  test_free(void* ptr, void* context)     { (void)context; free(ptr); }

#define STBIR_MALLOC(size,context) test_malloc(size,context)
#define STBIR_FREE(ptr,context)    test_free(ptr,context)
#define STBIR_ASSERT(x)            ((x) ? (void)0 : (void)g_assert_calls++)
#define STBIR_FAST_PATH_STATS
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"

// reference implementation: same library, fast paths compiled out
extern int ref_resize_uint8(const unsigned char* input, int in_w, int in_h,
                            unsigned char* output, int out_w, int out_h, int channels);

// ---- test framework -------------------------------------------------------

#define PATH_INT_NN   1
#define PATH_GRAY     2
#define PATH_FALLBACK 3

static const char* path_name(int p)
{
    return p == PATH_INT_NN ? "int-nn" : p == PATH_GRAY ? "gray" : "fallback";
}

typedef struct {
    const char* group;
    const char* name;
    int channels;
    int iw, ih, ow, oh;
    int path;      // path taken in the full (all fast paths on) build
    int palette;   // input is a palette-index map
} test_case;

static const test_case g_cases[] = {
    // group "2x": integer upscale by 2
    { "2x",     "rgb 24x18->48x36",     3, 24, 18, 48, 36, PATH_INT_NN, 0 },
    { "2x",     "gray 20x20->40x40",    1, 20, 20, 40, 40, PATH_INT_NN, 0 },
    { "2x",     "pal 16x16->32x32",     1, 16, 16, 32, 32, PATH_INT_NN, 1 },
    // group "3x": integer upscale by 3
    { "3x",     "rgb 16x16->48x48",     3, 16, 16, 48, 48, PATH_INT_NN, 0 },
    { "3x",     "gray 18x12->54x36",    1, 18, 12, 54, 36, PATH_INT_NN, 0 },
    // group "nonint": non-integer upscale -> explicit fallback for color
    { "nonint", "rgb 17x13->40x31",     3, 17, 13, 40, 31, PATH_FALLBACK, 0 },
    { "nonint", "gray 17x13->40x31",    1, 17, 13, 40, 31, PATH_GRAY, 0 },
    // group "down": downscale -> explicit fallback for color
    { "down",   "rgb 48x36->20x12",     3, 48, 36, 20, 12, PATH_FALLBACK, 0 },
    { "down",   "gray 40x40->15x15",    1, 40, 40, 15, 15, PATH_GRAY, 0 },
};

#define NUM_CASES (sizeof(g_cases) / sizeof(g_cases[0]))
#define MAX_PIXELS (64 * 64 * 4)

static unsigned char g_input [MAX_PIXELS];
static unsigned char g_output[MAX_PIXELS];
static unsigned char g_ref   [MAX_PIXELS];

// path expected in THIS build, given the compile-time toggles
static int expected_path(const test_case* tc)
{
#if defined(STBIR_NO_FAST_PATHS)
    (void)tc;
    return PATH_FALLBACK;
#else
    int p = tc->path;
  #if defined(STBIR_NO_FAST_PATH_INT_NN)
    // 1-channel integer scales are still covered by the gray path
    if (p == PATH_INT_NN) p = (tc->channels == 1) ? PATH_GRAY : PATH_FALLBACK;
  #endif
  #if defined(STBIR_NO_FAST_PATH_GRAY)
    if (p == PATH_GRAY) p = PATH_FALLBACK;
  #endif
    return p;
#endif
}

static void gen_sample(const test_case* tc)
{
    int x, y, c;
    for (y = 0; y < tc->ih; y++)
        for (x = 0; x < tc->iw; x++)
            for (c = 0; c < tc->channels; c++)
            {
                unsigned v = (unsigned)(x * 7 + y * 13 + c * 41 + (x * y) * 3);
                if (tc->palette)
                    g_input[(y * tc->iw + x) * tc->channels + c] = (unsigned char)(v % 37); // few distinct indices
                else
                    g_input[(y * tc->iw + x) * tc->channels + c] = (unsigned char)v;
            }
}

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

#define TIMING_ITERS 20000

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("    FAIL: %s\n", msg); g_failures++; } \
} while (0)

int main(int argc, char** argv)
{
    const char* only = argc > 1 ? argv[1] : "all";
    size_t i;
    int k;

    printf("stb_image_resize fast-path test");
#if defined(STBIR_NO_FAST_PATHS)
    printf(" [config: STBIR_NO_FAST_PATHS]");
#elif defined(STBIR_NO_FAST_PATH_INT_NN)
    printf(" [config: STBIR_NO_FAST_PATH_INT_NN]");
#elif defined(STBIR_NO_FAST_PATH_GRAY)
    printf(" [config: STBIR_NO_FAST_PATH_GRAY]");
#else
    printf(" [config: all fast paths on]");
#endif
#if defined(STBIR_NO_GENERAL_FALLBACK)
    printf(" [config: STBIR_NO_GENERAL_FALLBACK]");
#endif
    printf("\n\n");

    for (i = 0; i < NUM_CASES; i++)
    {
        const test_case* tc = &g_cases[i];
        int expect = expected_path(tc);
        int n_out = tc->ow * tc->oh * tc->channels;
        int nn = tc->iw * tc->ih * tc->channels;
        int identical, maxdiff;
        long sumdiff;
        int fb0, nn0, gray0, allocs0, ret, ret_ref;
        double t0, t1, t_fast, t_ref;
        unsigned long checksum = 0;

        if (strcmp(only, "all") != 0 && strcmp(only, tc->group) != 0 &&
            !(strcmp(only, "int") == 0 && (strcmp(tc->group, "2x") == 0 || strcmp(tc->group, "3x") == 0)))
            continue;

        gen_sample(tc);

        // counters before
        fb0 = stbir__stat_fallback_hits;
        nn0 = stbir__stat_int_nn_hits;
        gray0 = stbir__stat_gray_hits;
        allocs0 = g_alloc_calls;

        memset(g_output, 0xAA, (size_t)n_out);
        ret = stbir_resize_uint8(g_input, tc->iw, tc->ih, 0, g_output, tc->ow, tc->oh, 0, tc->channels);

#if defined(STBIR_NO_GENERAL_FALLBACK)
        if (expect == PATH_FALLBACK)
        {
            printf("[%s] %s: fallback disabled, resize refused (ret=%d) -- as designed\n",
                   tc->group, tc->name, ret);
            CHECK(ret == 0, "expected refusal when fallback is compiled out");
            continue;
        }
#endif

        CHECK(ret == 1, "resize returned 0");
        if (ret != 1) continue;

        // ---- metric 4: fallback hit count (and which path ran) ----
        {
            int d_fb = stbir__stat_fallback_hits - fb0;
            int d_nn = stbir__stat_int_nn_hits - nn0;
            int d_gray = stbir__stat_gray_hits - gray0;
            int d_allocs = g_alloc_calls - allocs0;
            int taken = d_nn ? PATH_INT_NN : d_gray ? PATH_GRAY : PATH_FALLBACK;

            CHECK(taken == expect, "unexpected path taken");
            CHECK(d_fb + d_nn + d_gray == 1, "exactly one path must run per call");

            // ---- metric 3: extra allocations (must be 0 on fast paths) ----
            if (taken != PATH_FALLBACK)
                CHECK(d_allocs == 0, "fast path allocated memory");
            else
                CHECK(d_allocs == 1, "general path must make its documented single malloc");

            // ---- metric 1: per-pixel consistency vs reference ----
            memset(g_ref, 0x55, (size_t)n_out);
            ret_ref = ref_resize_uint8(g_input, tc->iw, tc->ih, g_ref, tc->ow, tc->oh, tc->channels);
            CHECK(ret_ref == 1, "reference resize returned 0");

            identical = 0; maxdiff = 0; sumdiff = 0;
            for (k = 0; k < n_out; k++)
            {
                int d = g_output[k] - g_ref[k];
                if (d == 0) identical++;
                if (d < 0) d = -d;
                if (d > maxdiff) maxdiff = d;
                sumdiff += d;
            }

            if (taken == PATH_FALLBACK)
                CHECK(identical == n_out, "fallback output differs from reference (general path changed!)");

            // nearest neighbor never invents values: required for palette maps
            if (taken != PATH_FALLBACK)
            {
                unsigned char present[256];
                memset(present, 0, sizeof(present));
                for (k = 0; k < nn; k++) present[g_input[k]] = 1;
                for (k = 0; k < n_out; k++)
                    if (!present[g_output[k]]) break;
                CHECK(k == n_out, "nearest-neighbor output contains a value not in the input");
            }

            // ---- metric 2: timing vs reference ----
            t0 = now_sec();
            for (k = 0; k < TIMING_ITERS; k++)
                stbir_resize_uint8(g_input, tc->iw, tc->ih, 0, g_output, tc->ow, tc->oh, 0, tc->channels);
            t1 = now_sec();
            t_fast = t1 - t0;

            t0 = now_sec();
            for (k = 0; k < TIMING_ITERS; k++)
                ref_resize_uint8(g_input, tc->iw, tc->ih, g_ref, tc->ow, tc->oh, tc->channels);
            t1 = now_sec();
            t_ref = t1 - t0;

            for (k = 0; k < n_out; k++) checksum += g_output[k];

            printf("[%s] %s: path=%-8s identical=%5.1f%% maxdiff=%3d meandiff=%5.2f | "
                   "fast=%7.3fus ref=%7.3fus speedup=%5.2fx | allocs=%d | fallback_hits=%d\n",
                   tc->group, tc->name, path_name(taken),
                   100.0 * identical / n_out, maxdiff, (double)sumdiff / n_out,
                   t_fast / TIMING_ITERS * 1e6, t_ref / TIMING_ITERS * 1e6,
                   t_ref / t_fast, d_allocs, d_fb);
            (void)checksum;
        }
    }

    // ---- degenerate scales: explicit handling (return 0, no crash) ----
    printf("\ndegenerate scale handling:\n");
    {
        int asserts0 = g_assert_calls;
        int r1 = stbir_resize_uint8(g_input, 16, 16, 0, g_output, 0, 32, 0, 3);   // 0x width
        int r2 = stbir_resize_uint8(g_input, 16, 16, 0, g_output, 32, -2, 0, 3);  // negative height
        int r3 = stbir_resize_uint8(g_input, 0, 16, 0, g_output, 32, 32, 0, 3);   // 0 input width
        int r4 = stbir_resize_uint8(NULL, 16, 16, 0, g_output, 32, 32, 0, 3);     // NULL input
        printf("  out_w=0 -> ret=%d, out_h=-2 -> ret=%d, in_w=0 -> ret=%d, NULL -> ret=%d "
               "(validation asserts fired: %d)\n",
               r1, r2, r3, r4, g_assert_calls - asserts0);
        CHECK(r1 == 0 && r2 == 0 && r3 == 0 && r4 == 0, "degenerate inputs must return 0");
        CHECK(g_assert_calls - asserts0 == 4, "each degenerate call must trigger a validation assert");
    }

    printf("\ncounters: int_nn=%d gray=%d fallback=%d | total validation asserts=%d\n",
           stbir__stat_int_nn_hits, stbir__stat_gray_hits, stbir__stat_fallback_hits, g_assert_calls);
    printf("%s (%d failure%s)\n", g_failures ? "FAIL" : "PASS", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
