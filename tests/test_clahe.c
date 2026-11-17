// CLAHE (contrast-limited adaptive histogram equalization) tests.
//
// Build/run:
//   cc -I.. -O2 -Wall -Wextra -Werror test_clahe.c -lm -o test_clahe
//   ./test_clahe
//
// The test is fully self-contained: sample images are either synthesised at
// runtime or stored as small byte arrays in clahe_fixtures.h, so it needs no
// external data files or third-party libraries.
//
// What it checks:
//   * invalid tile counts and clip limits are rejected with a failure reason
//   * CLAHE is deterministic, works in place, and never writes to the source
//   * alpha is never modified, and greyscale / grey+alpha are supported
//   * bilinear interpolation leaves no visible tile seams
//   * the clip limit bounds noise amplification
//   * on four kinds of image (low contrast, blown highlights, dark detail,
//     heavy noise) the sparse, detail-bearing region keeps its structure
//     while global equalization eats it, and noise in flat areas is
//     controlled where global equalization runs away
//   * timing on a 512x512 image
//   * the existing stb_image loaders stay bit-identical (regression)

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdarg.h>

//////////////////////////////////////////////////////////////////////////////
// deterministic RNG (xorshift32) so results never depend on the C library

static unsigned int g_rng = 0x9e3779b9u;

static unsigned int rnd(void)
{
   unsigned int x = g_rng;
   x ^= x << 13;
   x ^= x >> 17;
   x ^= x << 5;
   g_rng = x;
   return x;
}

static int rnd_range(int lo, int hi) // inclusive
{
   return lo + (int) (rnd() % (unsigned int) (hi - lo + 1));
}

static unsigned char clamp8f(float v)
{
   if (v <= 0.0f) return 0;
   if (v >= 255.0f) return 255;
   return (unsigned char) (v + 0.5f);
}

//////////////////////////////////////////////////////////////////////////////
// test harness

static int g_failures = 0;
static int g_checks = 0;

static void check(int cond, const char *fmt, ...)
{
   va_list ap;
   ++g_checks;
   if (!cond) {
      ++g_failures;
      printf("FAIL: ");
   } else {
      printf("ok:   ");
   }
   va_start(ap, fmt);
   vprintf(fmt, ap);
   va_end(ap);
   printf("\n");
}

//////////////////////////////////////////////////////////////////////////////
// metrics

static int grey_channels(int comp) { return comp >= 3 ? 3 : 1; }

// mean absolute difference between horizontally/vertically adjacent pixels
// inside a rectangle: a simple local-contrast measure
static double region_contrast(const unsigned char *img, int w, int h, int comp,
                              int rx, int ry, int rw, int rh)
{
   double sum = 0.0;
   long n = 0;
   int x, y, c, chans = grey_channels(comp);

   (void) h;

   for (y = ry; y < ry + rh; ++y) {
      for (x = rx; x + 1 < rx + rw; ++x) {
         const unsigned char *a = img + ((size_t) y * w + x) * comp;
         const unsigned char *b = a + comp;
         for (c = 0; c < chans; ++c) {
            sum += fabs((double) a[c] - (double) b[c]);
            ++n;
         }
      }
   }
   for (y = ry; y + 1 < ry + rh; ++y) {
      for (x = rx; x < rx + rw; ++x) {
         const unsigned char *a = img + ((size_t) y * w + x) * comp;
         const unsigned char *b = a + (size_t) w * comp;
         for (c = 0; c < chans; ++c) {
            sum += fabs((double) a[c] - (double) b[c]);
            ++n;
         }
      }
   }
   return n ? sum / (double) n : 0.0;
}

// standard deviation of channel 0 in a rectangle (noise in a flat area)
static double region_std(const unsigned char *img, int w, int h, int comp,
                         int rx, int ry, int rw, int rh)
{
   double sum = 0.0, sumsq = 0.0;
   long n = 0;
   int x, y;
   (void) h;
   for (y = ry; y < ry + rh; ++y) {
      for (x = rx; x < rx + rw; ++x) {
         double v = img[((size_t) y * w + x) * comp];
         sum += v;
         sumsq += v * v;
         ++n;
      }
   }
   if (!n) return 0.0;
   {
      double mean = sum / (double) n;
      double var = sumsq / (double) n - mean * mean;
      return var > 0.0 ? sqrt(var) : 0.0;
   }
}

// fraction of pixels with a channel pinned at 0 or 255
static double saturated_fraction(const unsigned char *img, int w, int h, int comp)
{
   long n = 0, sat = 0;
   int i, c, chans = grey_channels(comp);
   for (i = 0; i < w * h; ++i) {
      for (c = 0; c < chans; ++c) {
         unsigned char v = img[(size_t) i * comp + c];
         if (v == 0 || v == 255) ++sat;
         ++n;
      }
   }
   return n ? (double) sat / (double) n : 0.0;
}

// A seam score: at each nominal tile boundary, compare the step across the
// boundary with the average step three pixels to either side of it. Using
// the local neighbourhood means genuine image content near the boundary
// does not inflate the score; only a discontinuity that appears exactly on
// the grid does. 1.0 means "no different from its surroundings"; bilinear
// interpolation keeps this near 1, hard tile edges push it well above 2.
static double lum_at(const unsigned char *img, int w, int x, int y, int comp)
{
   const unsigned char *p = img + ((size_t) y * w + x) * comp;
   if (comp >= 3) return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
   return p[0];
}

static double tile_seam_ratio(const unsigned char *img, int w, int h, int comp,
                              int tiles_x, int tiles_y)
{
   double boundary = 0.0, local = 0.0;
   long n = 0;
   int x, y, i;

   for (y = 0; y < h; ++y) {
      for (i = 1; i < tiles_x; ++i) {
         int bx = (w * i) / tiles_x;
         if (bx < 3 || bx + 3 >= w) continue;
         boundary += fabs(lum_at(img, w, bx, y, comp) - lum_at(img, w, bx - 1, y, comp));
         local += 0.5 * (fabs(lum_at(img, w, bx - 3, y, comp) - lum_at(img, w, bx - 2, y, comp))
                       + fabs(lum_at(img, w, bx + 2, y, comp) - lum_at(img, w, bx + 3, y, comp)));
         ++n;
      }
   }
   for (x = 0; x < w; ++x) {
      for (i = 1; i < tiles_y; ++i) {
         int by = (h * i) / tiles_y;
         if (by < 3 || by + 3 >= h) continue;
         boundary += fabs(lum_at(img, w, x, by, comp) - lum_at(img, w, x, by - 1, comp));
         local += 0.5 * (fabs(lum_at(img, w, x, by - 3, comp) - lum_at(img, w, x, by - 2, comp))
                       + fabs(lum_at(img, w, x, by + 2, comp) - lum_at(img, w, x, by + 3, comp)));
         ++n;
      }
   }
   if (!n || local <= 0.0) return 1.0;
   return boundary / local;
}

// Global histogram equalization, for comparison. Uses luminance for colour
// images (alpha copied through). No clipping, exactly what "global
// equalization" means.
static void global_equalize(int w, int h, int comp, const unsigned char *src, unsigned char *dst)
{
   int hist[256];
   int cdf[256];
   unsigned char map[256];
   int i, x, y, n = w * h;

   memset(hist, 0, sizeof(hist));
   for (i = 0; i < n; ++i) {
      const unsigned char *p = src + (size_t) i * comp;
      int v;
      if (comp >= 3)
         v = (int) (0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2] + 0.5);
      else
         v = p[0];
      if (v < 0) v = 0;
      if (v > 255) v = 255;
      hist[v]++;
   }
   cdf[0] = hist[0];
   for (i = 1; i < 256; ++i) cdf[i] = cdf[i - 1] + hist[i];
   for (i = 0; i < 256; ++i) map[i] = (unsigned char) (cdf[i] * 255 / n + 0.5);

   for (y = 0; y < h; ++y) {
      for (x = 0; x < w; ++x) {
         const unsigned char *p = src + ((size_t) y * w + x) * comp;
         unsigned char *d = dst + ((size_t) y * w + x) * comp;
         if (comp >= 3) {
            int v = (int) (0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2] + 0.5);
            float scale;
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            scale = v > 0 ? (float) map[v] / (float) v : 0.0f;
            d[0] = clamp8f(p[0] * scale);
            d[1] = clamp8f(p[1] * scale);
            d[2] = clamp8f(p[2] * scale);
            if (comp == 4) d[3] = p[3];
         } else {
            d[0] = map[p[0]];
            if (comp == 2) d[1] = p[1];
         }
      }
   }
}

// mean absolute change in chroma differences normalised by luminance; this
// is a hue-drift measure that ignores a uniform brightness change
static double mean_chroma_shift(const unsigned char *a, const unsigned char *b, int w, int h)
{
   double sum = 0.0;
   int i;
   if (w <= 0 || h <= 0) return 0.0;
   for (i = 0; i < w * h; ++i) {
      double ya = 0.299 * a[i * 3 + 0] + 0.587 * a[i * 3 + 1] + 0.114 * a[i * 3 + 2];
      double yb = 0.299 * b[i * 3 + 0] + 0.587 * b[i * 3 + 1] + 0.114 * b[i * 3 + 2];
      double drg = (a[i * 3 + 0] - a[i * 3 + 1]) / (ya > 8.0 ? ya : 8.0)
                 - (b[i * 3 + 0] - b[i * 3 + 1]) / (yb > 8.0 ? yb : 8.0);
      double dbg = (a[i * 3 + 2] - a[i * 3 + 1]) / (ya > 8.0 ? ya : 8.0)
                 - (b[i * 3 + 2] - b[i * 3 + 1]) / (yb > 8.0 ? yb : 8.0);
      sum += fabs(drg) + fabs(dbg);
   }
   return sum / ((double) w * h);
}

//////////////////////////////////////////////////////////////////////////////
// synthetic medical-style images
//
// All four images are 128x128 RGB with:
//   * a dense, near-flat background that dominates the histogram
//   * a small "detail" rectangle (18.75% of the area) holding fine structure
//     in a sparse intensity band
//   * mild sensor noise everywhere so flat regions are not perfectly uniform
//
// The detail rectangle is (96,16)-(127,111). The flat rectangle used for the
// noise measurement is (8,8)-(39,39).

#define IMG_W 128
#define IMG_H 128
#define DETAIL_X 100
#define DETAIL_Y 16
#define DETAIL_W 28
#define DETAIL_H 96
#define FLAT_X 8
#define FLAT_Y 8
#define FLAT_W 32
#define FLAT_H 32

static unsigned char *alloc_rgb(int w, int h)
{
   return (unsigned char *) malloc((size_t) w * h * 3);
}

static void put(unsigned char *img, int w, int x, int y, float v)
{
   unsigned char *p = img + ((size_t) y * w + x) * 3;
   p[0] = p[1] = p[2] = clamp8f(v);
}

static void add_noise(unsigned char *img, int w, int h, int amp)
{
   int x, y;
   for (y = 0; y < h; ++y)
      for (x = 0; x < w; ++x)
         put(img, w, x, y, (float) img[((size_t) y * w + x) * 3] + rnd_range(-amp, amp));
}

// large uniform-ish field; detail band sits well below the background peak
static unsigned char *make_low_contrast(int w, int h)
{
   unsigned char *img = alloc_rgb(w, h);
   int x, y;
   for (y = 0; y < h; ++y)
      for (x = 0; x < w; ++x)
         put(img, w, x, y, 128.0f + (float) rnd_range(-1, 1));
   for (y = DETAIL_Y; y < DETAIL_Y + DETAIL_H; ++y) {
      for (x = DETAIL_X; x < DETAIL_X + DETAIL_W; ++x) {
         int band = 90 + ((x - DETAIL_X) * 22) / DETAIL_W + ((y - DETAIL_Y) * 8) / DETAIL_H;
         int checker = ((x / 2 + y / 2) & 1) ? 8 : -8;
         put(img, w, x, y, (float) (band + checker));
      }
   }
   add_noise(img, w, h, 2);
   return img;
}

// mostly saturated highlight with a soft-tissue region below it
static unsigned char *make_blown(int w, int h)
{
   unsigned char *img = alloc_rgb(w, h);
   int x, y;
   for (y = 0; y < h; ++y)
      for (x = 0; x < w; ++x) {
         float v = 249.0f + (float) rnd_range(-1, 1);
         if (rnd() % 7 == 0) v = 255.0f; // scattered fully blown pixels
         put(img, w, x, y, v);
      }
   for (y = DETAIL_Y; y < DETAIL_Y + DETAIL_H; ++y) {
      for (x = DETAIL_X; x < DETAIL_X + DETAIL_W; ++x) {
         int band = 95 + ((x - DETAIL_X) * 40) / DETAIL_W + ((y - DETAIL_Y) * 10) / DETAIL_H;
         int checker = ((x / 2 + y / 2) & 1) ? 6 : -6;
         put(img, w, x, y, (float) (band + checker));
      }
   }
   add_noise(img, w, h, 2);
   return img;
}

// bright background with a dark structure carrying the detail
static unsigned char *make_dark_detail(int w, int h)
{
   unsigned char *img = alloc_rgb(w, h);
   int x, y;
   for (y = 0; y < h; ++y)
      for (x = 0; x < w; ++x)
         put(img, w, x, y, 220.0f + (float) rnd_range(-2, 2));
   for (y = DETAIL_Y; y < DETAIL_Y + DETAIL_H; ++y) {
      for (x = DETAIL_X; x < DETAIL_X + DETAIL_W; ++x) {
         int band = 8 + ((x - DETAIL_X) * 32) / DETAIL_W + ((y - DETAIL_Y) * 6) / DETAIL_H;
         int checker = ((x / 2 + y / 2) & 1) ? 4 : -4;
         put(img, w, x, y, (float) (band + checker));
      }
   }
   add_noise(img, w, h, 2);
   return img;
}

// noisy flat field with a structured, low-amplitude lesion
static unsigned char *make_noisy(int w, int h)
{
   unsigned char *img = alloc_rgb(w, h);
   int x, y;
   for (y = 0; y < h; ++y)
      for (x = 0; x < w; ++x)
         put(img, w, x, y, 128.0f + (float) rnd_range(-6, 6));
   for (y = DETAIL_Y; y < DETAIL_Y + DETAIL_H; ++y) {
      for (x = DETAIL_X; x < DETAIL_X + DETAIL_W; ++x) {
         int band = 60 + ((x - DETAIL_X) * 40) / DETAIL_W;
         int checker = ((x / 2 + y / 2) & 1) ? 8 : -8;
         put(img, w, x, y, (float) (band + checker));
      }
   }
   add_noise(img, w, h, 3);
   return img;
}

//////////////////////////////////////////////////////////////////////////////

static void test_invalid_parameters(void)
{
   unsigned char src[64 * 64 * 3];
   unsigned char dst[64 * 64 * 3];
   unsigned char src256[256 * 16 * 3];
   unsigned char dst256[256 * 16 * 3];
   int r;

   memset(src, 128, sizeof(src));
   memset(src256, 128, sizeof(src256));

   r = stbi_clahe_apply(64, 64, 3, src, dst, 0, 4, 2.0f, 0);
   check(r == 0, "tiles_x=0 is rejected (%s)", stbi_failure_reason());

   r = stbi_clahe_apply(64, 64, 3, src, dst, 4, 0, 2.0f, 0);
   check(r == 0, "tiles_y=0 is rejected (%s)", stbi_failure_reason());

   r = stbi_clahe_apply(64, 64, 3, src, dst, -3, 4, 2.0f, 0);
   check(r == 0, "negative tiles_x is rejected (%s)", stbi_failure_reason());

   r = stbi_clahe_apply(64, 64, 3, src, dst, 65, 4, 2.0f, 0);
   check(r == 0, "tiles_x > width is rejected (%s)", stbi_failure_reason());

   r = stbi_clahe_apply(64, 64, 3, src, dst, 4, 65, 2.0f, 0);
   check(r == 0, "tiles_y > height is rejected (%s)", stbi_failure_reason());

   r = stbi_clahe_apply(64, 64, 3, src, dst, 1000, 1, 2.0f, 0);
   check(r == 0, "tiles_x above the 256 cap is rejected (%s)", stbi_failure_reason());

   r = stbi_clahe_apply(64, 64, 3, src, dst, 8, 8, 0.5f, 0);
   check(r == 0, "clip_limit below 1 is rejected (%s)", stbi_failure_reason());

   r = stbi_clahe_apply(64, 64, 3, src, dst, 8, 8, 0.0f, 0);
   check(r == 0, "clip_limit=0 is rejected (%s)", stbi_failure_reason());

   r = stbi_clahe_apply(64, 64, 3, src, dst, 8, 8, -1.0f, 0);
   check(r == 0, "negative clip_limit is rejected (%s)", stbi_failure_reason());

   r = stbi_clahe_apply(64, 64, 3, src, dst, 8, 8, 10001.0f, 0);
   check(r == 0, "clip_limit above 10000 is rejected (%s)", stbi_failure_reason());

   r = stbi_clahe_apply(64, 64, 3, src, dst, 8, 8, (float) (0.0 / 0.0), 0);
   check(r == 0, "NaN clip_limit is rejected (%s)", stbi_failure_reason());

   r = stbi_clahe_apply(0, 64, 3, src, dst, 8, 8, 2.0f, 0);
   check(r == 0, "zero width is rejected (%s)", stbi_failure_reason());

   r = stbi_clahe_apply(64, 64, 5, src, dst, 8, 8, 2.0f, 0);
   check(r == 0, "comp outside 1..4 is rejected (%s)", stbi_failure_reason());

   r = stbi_clahe_apply(64, 64, 3, NULL, dst, 8, 8, 2.0f, 0);
   check(r == 0, "NULL source is rejected (%s)", stbi_failure_reason());

   r = stbi_clahe_apply(64, 64, 3, src, NULL, 8, 8, 2.0f, 0);
   check(r == 0, "NULL destination is rejected (%s)", stbi_failure_reason());

   // valid edge cases must be accepted
   r = stbi_clahe_apply(64, 64, 3, src, dst, 1, 1, 1.0f, 0);
   check(r == 1, "1x1 tile at clip_limit=1 is accepted");
   r = stbi_clahe_apply(64, 64, 3, src, dst, 64, 64, 10000.0f, 0);
   check(r == 1, "one tile per pixel at clip_limit=10000 is accepted");
   r = stbi_clahe_apply(256, 16, 3, src256, dst256, 256, 1, 2.0f, 0);
   check(r == 1, "256x1 tile grid is accepted");
   r = stbi_clahe_apply(64, 64, 3, src, dst, 64, 64, 10000.0f, 1);
   check(r == 1, "64x64 grid fits the 4096 tile cap");
   r = stbi_clahe_apply(256, 64, 3, src256, dst256, 256, 17, 2.0f, 0);
   check(r == 0, "grids past the 4096 tile cap are rejected (%s)", stbi_failure_reason());
}

static void test_identity_and_alpha(void)
{
   int w = 48, h = 40, i;
   unsigned char *src = alloc_rgb(w, h);
   unsigned char *dst = alloc_rgb(w, h);
   unsigned char *copy = alloc_rgb(w, h);
   unsigned char *rgba, *out;
   int ok;

   for (i = 0; i < w * h; ++i) {
      src[i * 3 + 0] = (unsigned char) ((i * 7) & 255);
      src[i * 3 + 1] = (unsigned char) ((i * 13) & 255);
      src[i * 3 + 2] = (unsigned char) ((i * 29) & 255);
   }
   memcpy(copy, src, (size_t) w * h * 3);

   rgba = (unsigned char *) malloc((size_t) w * h * 4);
   out = (unsigned char *) malloc((size_t) w * h * 4);
   for (i = 0; i < w * h; ++i) {
      rgba[i * 4 + 0] = src[i * 3 + 0];
      rgba[i * 4 + 1] = src[i * 3 + 1];
      rgba[i * 4 + 2] = src[i * 3 + 2];
      rgba[i * 4 + 3] = (unsigned char) (i * 37 + 11);
   }

   stbi_clahe_apply(w, h, 4, rgba, out, 4, 4, 3.0f, 0);
   ok = 1;
   for (i = 0; i < w * h; ++i)
      if (out[i * 4 + 3] != rgba[i * 4 + 3]) ok = 0;
   check(ok, "per-channel mode leaves alpha untouched");

   stbi_clahe_apply(w, h, 4, rgba, out, 4, 4, 3.0f, 1);
   ok = 1;
   for (i = 0; i < w * h; ++i)
      if (out[i * 4 + 3] != rgba[i * 4 + 3]) ok = 0;
   check(ok, "luminance mode leaves alpha untouched");

   // in place vs out of place
   stbi_clahe_apply(w, h, 3, src, dst, 5, 3, 4.0f, 1);
   stbi_clahe_apply(w, h, 3, rgba, rgba, 5, 3, 4.0f, 1); // separate buffer, same call path
   stbi_clahe_apply(w, h, 3, src, src, 5, 3, 4.0f, 1);
   ok = 1;
   for (i = 0; i < w * h * 3; ++i)
      if (src[i] != dst[i]) ok = 0;
   check(ok, "in-place CLAHE matches out-of-place CLAHE");

   // source unmodified by the out-of-place call
   memcpy(src, copy, (size_t) w * h * 3);
   stbi_clahe_apply(w, h, 3, src, dst, 4, 4, 3.0f, 0);
   ok = 1;
   for (i = 0; i < w * h * 3; ++i)
      if (src[i] != copy[i]) ok = 0;
   check(ok, "source image is left unmodified");

   // deterministic
   {
      unsigned char *d2 = alloc_rgb(w, h);
      stbi_clahe_apply(w, h, 3, src, dst, 6, 5, 7.0f, 0);
      stbi_clahe_apply(w, h, 3, src, d2, 6, 5, 7.0f, 0);
      ok = 1;
      for (i = 0; i < w * h * 3; ++i)
         if (dst[i] != d2[i]) ok = 0;
      check(ok, "CLAHE output is deterministic");
      free(d2);
   }

   free(rgba);
   free(out);
   free(src);
   free(dst);
   free(copy);
}

static void test_grey_and_two_channel(void)
{
   int w = 32, h = 24, i;
   unsigned char *grey = (unsigned char *) malloc((size_t) w * h);
   unsigned char *out = (unsigned char *) malloc((size_t) w * h);
   unsigned char *ga = (unsigned char *) malloc((size_t) w * h * 2);
   unsigned char *ga_out = (unsigned char *) malloc((size_t) w * h * 2);
   int alpha_ok = 1;

   for (i = 0; i < w * h; ++i)
      grey[i] = (unsigned char) (30 + (i * 7) % 60);

   check(stbi_clahe_apply(w, h, 1, grey, out, 4, 3, 2.5f, 0) == 1, "greyscale CLAHE succeeds");
   check(region_contrast(out, w, h, 1, 0, 0, w, h) > region_contrast(grey, w, h, 1, 0, 0, w, h) * 1.2,
         "greyscale local contrast rises (%.2f -> %.2f)",
         region_contrast(grey, w, h, 1, 0, 0, w, h), region_contrast(out, w, h, 1, 0, 0, w, h));

   for (i = 0; i < w * h; ++i) {
      ga[i * 2 + 0] = grey[i];
      ga[i * 2 + 1] = (unsigned char) (i * 3);
   }
   check(stbi_clahe_apply(w, h, 2, ga, ga_out, 4, 3, 2.5f, 0) == 1, "grey+alpha CLAHE succeeds");
   for (i = 0; i < w * h; ++i)
      if (ga_out[i * 2 + 1] != ga[i * 2 + 1]) alpha_ok = 0;
   check(alpha_ok, "grey+alpha leaves alpha untouched");

   free(grey);
   free(out);
   free(ga);
   free(ga_out);
}

static void test_tile_seams(void)
{
   int w = 128, h = 128;
   unsigned char *src = make_low_contrast(w, h);
   unsigned char *dst = alloc_rgb(w, h);
   unsigned char *bad = alloc_rgb(w, h);
   double ratio, src_ratio;

   stbi_clahe_apply(w, h, 3, src, dst, 8, 8, 6.0f, 1);
   ratio = tile_seam_ratio(dst, w, h, 3, 8, 8);
   src_ratio = tile_seam_ratio(src, w, h, 3, 8, 8);
   check(ratio < 1.8 && ratio <= src_ratio * 1.2,
         "no block seams after bilinear interpolation (seam ratio %.2f, source %.2f)", ratio, src_ratio);

   // an artificially blocky image must score much worse, proving the metric
   {
      int x, y;
      memcpy(bad, dst, (size_t) w * h * 3);
      for (y = 0; y < h; ++y)
         for (x = 0; x < w; ++x)
            if ((x / 16 + y / 16) & 1)
               put(bad, w, x, y, (float) bad[((size_t) y * w + x) * 3] + 60.0f);
      check(tile_seam_ratio(bad, w, h, 3, 8, 8) > ratio * 1.8,
            "the seam metric detects a blocky image (%.2f vs %.2f)",
            tile_seam_ratio(bad, w, h, 3, 8, 8), ratio);
   }

   free(src);
   free(dst);
   free(bad);
}

static void test_clip_limit_monotonic(void)
{
   int w = 96, h = 96;
   unsigned char *src = alloc_rgb(w, h);
   unsigned char *out = alloc_rgb(w, h);
   double low, mid, high;
   int i;

   for (i = 0; i < w * h; ++i)
      put(src, w, i % w, i / w, 128.0f + (float) rnd_range(-30, 30));

   stbi_clahe_apply(w, h, 3, src, out, 8, 8, 1.0f, 0);
   low = region_std(out, w, h, 3, 0, 0, w, h);
   stbi_clahe_apply(w, h, 3, src, out, 8, 8, 3.0f, 0);
   mid = region_std(out, w, h, 3, 0, 0, w, h);
   stbi_clahe_apply(w, h, 3, src, out, 8, 8, 12.0f, 0);
   high = region_std(out, w, h, 3, 0, 0, w, h);

   check(low <= mid && mid <= high,
         "raising the clip limit does not lower output spread (%.2f <= %.2f <= %.2f)", low, mid, high);
   check(high > low * 1.1,
         "a high clip limit noticeably amplifies fine noise (%.2f vs %.2f)", high, low);

   free(src);
   free(out);
}

static void test_luminance_mode(void)
{
   int w = 96, h = 64, i;
   unsigned char *src = alloc_rgb(w, h);
   unsigned char *per_ch = alloc_rgb(w, h);
   unsigned char *luma = alloc_rgb(w, h);
   double shift_per_ch, shift_luma;

   for (i = 0; i < w * h; ++i) {
      int x = i % w, y = i / w;
      src[i * 3 + 0] = (unsigned char) (60 + (x % 7) * 3);
      src[i * 3 + 1] = (unsigned char) (30 + (y % 5) * 2);
      src[i * 3 + 2] = (unsigned char) (90 + (x % 3) * 2);
   }

   stbi_clahe_apply(w, h, 3, src, per_ch, 6, 4, 8.0f, 0);
   stbi_clahe_apply(w, h, 3, src, luma, 6, 4, 8.0f, 1);

   shift_per_ch = mean_chroma_shift(src, per_ch, w, h);
   shift_luma = mean_chroma_shift(src, luma, w, h);

   check(shift_luma < shift_per_ch,
         "luminance-only mode preserves chroma better than per-channel (%.4f vs %.4f)",
         shift_luma, shift_per_ch);
   check(region_contrast(luma, w, h, 3, 0, 0, w, h) > region_contrast(src, w, h, 3, 0, 0, w, h),
         "luminance-only mode still boosts local contrast (%.2f -> %.2f)",
         region_contrast(src, w, h, 3, 0, 0, w, h), region_contrast(luma, w, h, 3, 0, 0, w, h));

   free(src);
   free(per_ch);
   free(luma);
}

//////////////////////////////////////////////////////////////////////////////
// the four image classes: CLAHE vs global equalization

typedef struct
{
   const char *name;
   unsigned char *(*make)(int w, int h);
   double min_detail_lift; // CLAHE must beat the source by this ratio
} image_class;

static void test_image_class(const image_class *cls)
{
   int w = IMG_W, h = IMG_H;
   unsigned char *src = cls->make(w, h);
   unsigned char *clahe = alloc_rgb(w, h);
   unsigned char *glob = alloc_rgb(w, h);
   double c_src, c_clahe, c_glob;
   double d_src, d_clahe, d_glob;
   double n_src, n_clahe, n_glob;
   double sat_src, sat_clahe, sat_glob;
   double seam;
   clock_t t0, t1;
   double ms;

   t0 = clock();
   check(stbi_clahe_apply(w, h, 3, src, clahe, 8, 8, 4.0f, 1) == 1, "%s: CLAHE succeeds", cls->name);
   t1 = clock();
   ms = 1000.0 * (double) (t1 - t0) / (double) CLOCKS_PER_SEC;
   global_equalize(w, h, 3, src, glob);

   c_src   = region_contrast(src, w, h, 3, 0, 0, w, h);
   c_clahe = region_contrast(clahe, w, h, 3, 0, 0, w, h);
   c_glob  = region_contrast(glob, w, h, 3, 0, 0, w, h);

   d_src   = region_contrast(src, w, h, 3, DETAIL_X, DETAIL_Y, DETAIL_W, DETAIL_H);
   d_clahe = region_contrast(clahe, w, h, 3, DETAIL_X, DETAIL_Y, DETAIL_W, DETAIL_H);
   d_glob  = region_contrast(glob, w, h, 3, DETAIL_X, DETAIL_Y, DETAIL_W, DETAIL_H);

   n_src   = region_std(src, w, h, 3, FLAT_X, FLAT_Y, FLAT_W, FLAT_H);
   n_clahe = region_std(clahe, w, h, 3, FLAT_X, FLAT_Y, FLAT_W, FLAT_H);
   n_glob  = region_std(glob, w, h, 3, FLAT_X, FLAT_Y, FLAT_W, FLAT_H);

   sat_src   = saturated_fraction(src, w, h, 3);
   sat_clahe = saturated_fraction(clahe, w, h, 3);
   sat_glob  = saturated_fraction(glob, w, h, 3);

   seam = tile_seam_ratio(clahe, w, h, 3, 8, 8);

   printf("      %-12s whole-image contrast: src %.2f | CLAHE %.2f | global %.2f\n",
          cls->name, c_src, c_clahe, c_glob);
   printf("      %-12s detail contrast:      src %.2f | CLAHE %.2f | global %.2f\n",
          cls->name, d_src, d_clahe, d_glob);
   printf("      %-12s flat noise std:       src %.2f | CLAHE %.2f | global %.2f\n",
          cls->name, n_src, n_clahe, n_glob);
   printf("      %-12s saturated:            src %.1f%% | CLAHE %.1f%% | global %.1f%%\n",
          cls->name, sat_src * 100.0, sat_clahe * 100.0, sat_glob * 100.0);
   printf("      %-12s seam ratio %.2f | %.1f ms\n", cls->name, seam, ms);

   check(d_clahe > d_src * cls->min_detail_lift,
         "%s: CLAHE recovers detail in the sparse region (%.2f -> %.2f)",
         cls->name, d_src, d_clahe);
   check(d_clahe > d_glob,
         "%s: CLAHE keeps more detail than global equalization (%.2f vs %.2f)",
         cls->name, d_clahe, d_glob);
   check(n_clahe < n_glob,
         "%s: CLAHE adds less noise in flat areas than global equalization (%.2f vs %.2f)",
         cls->name, n_clahe, n_glob);
   check(sat_clahe <= sat_glob + 1e-9,
         "%s: CLAHE saturates no more than global equalization (%.1f%% vs %.1f%%)",
         cls->name, sat_clahe * 100.0, sat_glob * 100.0);
   check(seam < 1.8, "%s: no block artifacts (seam ratio %.2f)", cls->name, seam);

   free(src);
   free(clahe);
   free(glob);
}

static void test_timing(void)
{
   int w = 512, h = 512;
   unsigned char *src = alloc_rgb(w, h);
   unsigned char *dst = alloc_rgb(w, h);
   clock_t t0, t1;
   int i;

   for (i = 0; i < w * h; ++i) {
      src[i * 3 + 0] = (unsigned char) (i * 7);
      src[i * 3 + 1] = (unsigned char) (i * 13);
      src[i * 3 + 2] = (unsigned char) (i * 29);
   }

   t0 = clock();
   check(stbi_clahe_apply(w, h, 3, src, dst, 8, 8, 3.0f, 0) == 1, "512x512 per-channel CLAHE succeeds");
   t1 = clock();
   printf("      %dx%d per-channel, 8x8 tiles: %.1f ms\n", w, h,
          1000.0 * (double) (t1 - t0) / (double) CLOCKS_PER_SEC);

   t0 = clock();
   check(stbi_clahe_apply(w, h, 3, src, dst, 8, 8, 3.0f, 1) == 1, "512x512 luminance CLAHE succeeds");
   t1 = clock();
   printf("      %dx%d luminance,  8x8 tiles: %.1f ms\n", w, h,
          1000.0 * (double) (t1 - t0) / (double) CLOCKS_PER_SEC);

   free(src);
   free(dst);
}

//////////////////////////////////////////////////////////////////////////////
// regression: existing loaders must stay pixel-identical

#include "clahe_fixtures.h"

static unsigned long long fnv1a(const void *p, size_t n)
{
   const unsigned char *b = (const unsigned char *) p;
   unsigned long long h = 1469598103934665603ULL;
   size_t i;
   for (i = 0; i < n; ++i) {
      h ^= b[i];
      h *= 1099511628211ULL;
   }
   return h;
}

static void test_loader_regression(void)
{
   // hashes recorded from the unmodified library at the base commit: the
   // existing loaders must still produce exactly these pixels
   struct {
      const char *name;
      const unsigned char *data;
      int len;
      int req;
      int is_float;
      unsigned long long expect;
   } fx[] = {
      { "png rgba 16x16", png_tiny,  (int) sizeof png_tiny,  4, 0, 0x9ca63ae9ffee67c3ULL },
      { "png grey 8x8",   png_tiny8, (int) sizeof png_tiny8, 1, 0, 0xc3fad38ab23a5da3ULL },
      { "bmp rgb 16x16",  bmp_tiny,  (int) sizeof bmp_tiny,  3, 0, 0x2d102b996786fa83ULL },
      { "tga rgb 16x16",  tga_tiny,  (int) sizeof tga_tiny,  3, 0, 0x2d102b996786fa83ULL },
      { "pnm grey",       pnm_p5,    (int) sizeof pnm_p5,    1, 0, 0xfea9335a3ee1c4cbULL },
      { "pnm rgb",        pnm_p6,    (int) sizeof pnm_p6,    3, 0, 0x57e320b59501282bULL },
      { "hdr float",      hdr_tiny,  (int) sizeof hdr_tiny,  3, 1, 0xbc883ef6b878c243ULL },
      { "psd rgba",       psd_tiny,  (int) sizeof psd_tiny,  4, 0, 0x929aa129e2d80343ULL },
   };
   size_t i;

   for (i = 0; i < sizeof(fx) / sizeof(fx[0]); ++i) {
      int x, y, n;
      unsigned long long h;
      if (fx[i].is_float) {
         float *p = stbi_loadf_from_memory(fx[i].data, fx[i].len, &x, &y, &n, fx[i].req);
         check(p != NULL, "loader regression: %s decodes", fx[i].name);
         if (!p) continue;
         h = fnv1a(p, (size_t) x * y * fx[i].req * sizeof(float));
         stbi_image_free(p);
      } else {
         unsigned char *p = stbi_load_from_memory(fx[i].data, fx[i].len, &x, &y, &n, fx[i].req);
         check(p != NULL, "loader regression: %s decodes", fx[i].name);
         if (!p) continue;
         h = fnv1a(p, (size_t) x * y * fx[i].req);
         stbi_image_free(p);
      }
      check(h == fx[i].expect, "loader regression: %s is pixel-identical (%016llx)",
            fx[i].name, h);
   }
}

//////////////////////////////////////////////////////////////////////////////

int main(void)
{
   static const image_class classes[] = {
      { "low contrast",  make_low_contrast,  1.2 },
      { "blown",         make_blown,         1.1 },
      { "dark detail",   make_dark_detail,   1.2 },
      { "noisy",         make_noisy,         1.2 },
   };
   size_t i;

   printf("CLAHE test suite\n");
   printf("----------------\n");

   test_invalid_parameters();
   test_identity_and_alpha();
   test_grey_and_two_channel();
   test_tile_seams();
   test_clip_limit_monotonic();
   test_luminance_mode();

   printf("----------------\n");
   for (i = 0; i < sizeof(classes) / sizeof(classes[0]); ++i)
      test_image_class(&classes[i]);

   printf("----------------\n");
   test_timing();
   test_loader_regression();

   printf("----------------\n");
   printf("%d checks, %d failure(s)\n", g_checks, g_failures);
   return g_failures ? 1 : 0;
}
