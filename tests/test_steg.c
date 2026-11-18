/*
 * Tests for the LSB steganography helpers in stb_image_write.h.
 *
 * Synthesizes four small image classes (smooth / noise / jpeg-ish / alpha),
 * then for each one runs a few embedding configurations and reports:
 *   - payload capacity
 *   - invisibility metrics (adjacent-pixel difference before/after)
 *   - extraction accuracy (also after a PNG round trip)
 *   - embed/extract wall time
 *   - that an oversized payload fails without modifying any pixel
 *
 * It also hashes the output of the ordinary stbi_write_* functions and
 * compares against values captured from the library before this feature was
 * added, proving the existing writers are bit-for-bit unchanged.
 *
 * Build:  cc -I.. -O2 test_steg.c -lm -o test_steg
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

/* ------------------------------------------------------------------ */
/* helpers                                                             */

static int g_failures = 0;
static int g_checks = 0;

static void check(int cond, const char *what)
{
   ++g_checks;
   if (!cond) {
      ++g_failures;
      printf("  FAIL: %s\n", what);
   }
}

typedef struct
{
   int w, h, comp;
   unsigned char *px;
} image;

typedef struct
{
   unsigned char *data;
   int len, cap;
} membuf;

static void membuf_put(void *ctx, void *data, int size)
{
   membuf *m = (membuf *)ctx;
   if (m->len + size > m->cap) {
      int ncap = m->cap ? m->cap : 4096;
      while (ncap < m->len + size) ncap *= 2;
      m->data = (unsigned char *)realloc(m->data, (size_t)ncap);
      m->cap = ncap;
   }
   memcpy(m->data + m->len, data, (size_t)size);
   m->len += size;
}

static unsigned int rng_state = 0x12345678u;

static unsigned int rng_next(void)
{
   unsigned int x = rng_state;
   x ^= x << 13;
   x ^= x >> 17;
   x ^= x << 5;
   rng_state = x;
   return x;
}

static unsigned int fnv1a(const unsigned char *d, int n)
{
   unsigned int h = 2166136261u;
   int i;
   for (i = 0; i < n; ++i) { h ^= d[i]; h *= 16777619u; }
   return h;
}

/* mean absolute difference between adjacent pixels, all channels */
static double adjacent_mad(const unsigned char *px, int w, int h, int comp)
{
   double sum = 0;
   long long count = 0;
   int x, y, c;

   for (y = 0; y < h; ++y) {
      for (x = 0; x < w; ++x) {
         int base = (y * w + x) * comp;
         for (c = 0; c < comp; ++c) {
            if (x + 1 < w) {
               sum += abs((int)px[base + c] - (int)px[base + comp + c]);
               ++count;
            } else if (y + 1 < h) {
               sum += abs((int)px[base + c] - (int)px[base + w * comp + c]);
               ++count;
            }
         }
      }
   }
   return count ? sum / (double)count : 0.0;
}

static int max_abs_delta(const unsigned char *a, const unsigned char *b, int n, int *changed)
{
   int i, maxd = 0, nchanged = 0;
   for (i = 0; i < n; ++i) {
      int d = abs((int)a[i] - (int)b[i]);
      if (d > maxd) maxd = d;
      if (d) ++nchanged;
   }
   *changed = nchanged;
   return maxd;
}

static double now_ms(void)
{
   return (double)clock() * 1000.0 / (double)CLOCKS_PER_SEC;
}

/* ------------------------------------------------------------------ */
/* image synthesis                                                     */

static void make_smooth(image *im, int w, int h)
{
   int x, y;
   im->w = w; im->h = h; im->comp = 3;
   im->px = (unsigned char *)malloc((size_t)w * h * 3);
   for (y = 0; y < h; ++y) {
      for (x = 0; x < w; ++x) {
         unsigned char *p = im->px + (y * w + x) * 3;
         p[0] = (unsigned char)(30 + (x * 170) / (w - 1));
         p[1] = (unsigned char)(50 + (y * 150) / (h - 1));
         p[2] = (unsigned char)(80 + ((x + y) * 120) / (w + h - 2));
      }
   }
}

static void make_noise(image *im, int w, int h)
{
   int i;
   im->w = w; im->h = h; im->comp = 3;
   im->px = (unsigned char *)malloc((size_t)w * h * 3);
   for (i = 0; i < w * h * 3; ++i)
      im->px[i] = (unsigned char)(rng_next() >> 24);
}

/* a textured image, JPEG-compressed and decoded back: real block artifacts */
static void make_jpegish(image *im, int w, int h)
{
   unsigned char *src = (unsigned char *)malloc((size_t)w * h * 3);
   membuf mb;
   unsigned char *decoded;
   int x, y, n = 0;

   mb.data = NULL; mb.len = mb.cap = 0;

   for (y = 0; y < h; ++y) {
      for (x = 0; x < w; ++x) {
         unsigned char *p = src + (y * w + x) * 3;
         int block = ((x / 8) + (y / 8)) & 1;
         int stripe = ((x + y) % 10) < 5;
         p[0] = (unsigned char)((x * 120) / (w - 1) + block * 70 + stripe * 35);
         p[1] = (unsigned char)((y * 120) / (h - 1) + (1 - block) * 60 + stripe * 20);
         p[2] = (unsigned char)(((x ^ y) * 90) / (w - 1) + block * 40);
      }
   }

   check(stbi_write_jpg_to_func(membuf_put, &mb, w, h, 3, src, 78) != 0,
         "write synthetic JPEG");
   decoded = stbi_load_from_memory(mb.data, mb.len, &x, &y, &n, 3);
   check(decoded != NULL && x == w && y == h && n == 3, "decode synthetic JPEG");

   im->w = w; im->h = h; im->comp = 3;
   im->px = (unsigned char *)malloc((size_t)w * h * 3);
   memcpy(im->px, decoded, (size_t)w * h * 3);

   stbi_image_free(decoded);
   free(mb.data);
   free(src);
}

/* RGBA image with a smooth alpha ramp and a hard-edged opaque region */
static void make_alpha(image *im, int w, int h)
{
   int x, y;
   im->w = w; im->h = h; im->comp = 4;
   im->px = (unsigned char *)malloc((size_t)w * h * 4);
   for (y = 0; y < h; ++y) {
      for (x = 0; x < w; ++x) {
         unsigned char *p = im->px + (y * w + x) * 4;
         int in_box = (x > w / 4 && x < 3 * w / 4 && y > h / 4 && y < 3 * h / 4);
         p[0] = (unsigned char)(40 + (x * 180) / (w - 1));
         p[1] = (unsigned char)(60 + (y * 160) / (h - 1));
         p[2] = (unsigned char)(100 + ((x * y) % 120));
         p[3] = (unsigned char)(in_box ? 255 : 20 + (x * 200) / (w - 1));
      }
   }
}

static void image_free(image *im)
{
   free(im->px);
   im->px = NULL;
}

/* ------------------------------------------------------------------ */
/* one configuration on one image                                      */

typedef struct
{
   const char *name;
   int mask;        /* relative to the image's channels */
   int bit_depth;
   int stride;
   int randomize;
   unsigned int seed;
} test_config;

static void run_config(const image *orig, const test_config *tc)
{
   stbi_steg_config cfg;
   unsigned char *work;
   unsigned char *extract;
   int px_bytes = orig->w * orig->h * orig->comp;
   int capacity, payload_len, got_len, i;
   int max_delta, changed;
   double mad_before, mad_after;
   double t0, embed_ms, extract_ms;
   int ok, png_ok = 0;
   unsigned char *payload;
   membuf png;
   int pw, ph, pn;
   unsigned char *roundtrip = NULL;
   int accuracy = 0;

   cfg.channel_mask = tc->mask;
   cfg.bit_depth = tc->bit_depth;
   cfg.pixel_stride = tc->stride;
   cfg.randomize = tc->randomize;
   cfg.seed = tc->seed;

   capacity = stbi_write_steg_capacity(orig->w, orig->h, orig->comp, &cfg);
   printf("  %-8s mask=0x%x depth=%d stride=%d rand=%d  capacity=%d B",
          tc->name, tc->mask, tc->bit_depth, tc->stride, tc->randomize, capacity);
   if (capacity < 0) {
      printf("  (no capacity)\n");
      check(capacity <= 0, "configuration rejected cleanly");
      return;
   }

   payload_len = capacity < 512 ? capacity : 512;
   payload = (unsigned char *)malloc((size_t)payload_len + 1);
   for (i = 0; i < payload_len; ++i)
      payload[i] = (unsigned char)(rng_next() & 0xff);
   if (payload_len > 2) { payload[0] = 0x00; payload[1] = 0xff; payload[payload_len - 1] = 0x00; }

   work = (unsigned char *)malloc((size_t)px_bytes);
   memcpy(work, orig->px, (size_t)px_bytes);

   mad_before = adjacent_mad(orig->px, orig->w, orig->h, orig->comp);

   t0 = now_ms();
   ok = stbi_write_steg_embed(work, orig->w, orig->h, orig->comp, 0, &cfg, payload, payload_len);
   embed_ms = now_ms() - t0;
   check(ok, "embed returns success");
   if (!ok) { printf("\n"); free(payload); free(work); return; }

   max_delta = max_abs_delta(orig->px, work, px_bytes, &changed);
   mad_after = adjacent_mad(work, orig->w, orig->h, orig->comp);

   /* pixel changes must be confined to the low bits we claimed to use */
   check(max_delta <= (1 << tc->bit_depth) - 1, "only low bits changed");

   extract = (unsigned char *)malloc((size_t)payload_len);
   got_len = -1;
   t0 = now_ms();
   ok = stbi_write_steg_extract(work, orig->w, orig->h, orig->comp, 0, &cfg,
                                extract, payload_len, &got_len);
   extract_ms = now_ms() - t0;

   if (ok && got_len == payload_len && memcmp(extract, payload, (size_t)payload_len) == 0)
      accuracy = 100;

   /* PNG round trip: write the stego pixels, decode them, extract again */
   png.data = NULL; png.len = png.cap = 0;
   if (stbi_write_png_to_func(membuf_put, &png, orig->w, orig->h, orig->comp, work, 0)) {
      roundtrip = stbi_load_from_memory(png.data, png.len, &pw, &ph, &pn, orig->comp);
      if (roundtrip && pw == orig->w && ph == orig->h) {
         int got2 = -1;
         if (stbi_write_steg_extract(roundtrip, pw, ph, orig->comp, 0, &cfg,
                                     extract, payload_len, &got2) &&
             got2 == payload_len && memcmp(extract, payload, (size_t)payload_len) == 0)
            png_ok = 1;
      }
   }
   free(png.data);
   if (roundtrip) stbi_image_free(roundtrip);

   /* wrong seed (or unmodified image) must not extract */
   {
      stbi_steg_config bad = cfg;
      unsigned char probe[16];
      int probe_len = 0;
      int bad_ok;
      if (cfg.randomize) {
         bad.seed = cfg.seed + 12345;
         bad_ok = stbi_write_steg_extract(work, orig->w, orig->h, orig->comp, 0, &bad,
                                          probe, (int)sizeof(probe), &probe_len);
         check(!bad_ok, "extract with wrong seed fails");
      }
      bad_ok = stbi_write_steg_extract(orig->px, orig->w, orig->h, orig->comp, 0, &cfg,
                                       probe, (int)sizeof(probe), &probe_len);
      check(!bad_ok, "extract from pristine image fails");
   }

   /* pixel_stride must leave the pixels in between completely untouched */
   if (tc->stride > 1) {
      int touched_between = 0;
      int p;
      for (p = 1; p < orig->w * orig->h; ++p) {
         if (p % tc->stride == 0) continue;
         if (memcmp(orig->px + (size_t)p * orig->comp, work + (size_t)p * orig->comp,
                    (size_t)orig->comp) != 0)
            touched_between = 1;
      }
      check(!touched_between, "pixel_stride skips non-selected pixels");
   }

   /* counting-only channels: every changed byte must be in a selected channel */
   {
      int p, c, bad_channel = 0;
      for (p = 0; p < orig->w * orig->h; ++p) {
         for (c = 0; c < orig->comp; ++c) {
            size_t o = (size_t)p * orig->comp + c;
            if (orig->px[o] != work[o] && !(cfg.channel_mask & (1 << c)))
               bad_channel = 1;
         }
      }
      check(!bad_channel, "only selected channels modified");
   }

   /* oversized payload: must fail and leave every pixel untouched */
   {
      unsigned char *big = (unsigned char *)malloc((size_t)capacity + 1);
      unsigned char *before = (unsigned char *)malloc((size_t)px_bytes);
      int big_ok;
      memset(big, 0xA5, (size_t)capacity + 1);
      memcpy(work, orig->px, (size_t)px_bytes);
      memcpy(before, work, (size_t)px_bytes);
      big_ok = stbi_write_steg_embed(work, orig->w, orig->h, orig->comp, 0, &cfg,
                                     big, capacity + 1);
      check(!big_ok, "oversized payload fails");
      check(memcmp(work, before, (size_t)px_bytes) == 0, "oversized payload leaves pixels untouched");

      /* exact capacity must succeed */
      big_ok = stbi_write_steg_embed(work, orig->w, orig->h, orig->comp, 0, &cfg,
                                     big, capacity);
      check(big_ok, "exact-capacity payload succeeds");
      free(big);
      free(before);
   }

   /* corrupt one payload header bit: extraction must fail the integrity check */
   if (!cfg.randomize && (cfg.channel_mask & 1)) {
      int header_len = -1;
      memcpy(work, orig->px, (size_t)px_bytes);
      stbi_write_steg_embed(work, orig->w, orig->h, orig->comp, 0, &cfg, payload, payload_len);
      work[0] ^= 1; /* slot 0 is the first header bit */
      ok = stbi_write_steg_extract(work, orig->w, orig->h, orig->comp, 0, &cfg,
                                   extract, payload_len, &header_len);
      check(!ok, "corrupted header fails integrity check");
   }

   printf("  mad %.3f->%.3f  maxdelta=%d changed=%d  extract=%d%% png=%d  embed=%.2fms extract=%.2fms\n",
          mad_before, mad_after, max_delta, changed, accuracy, png_ok, embed_ms, extract_ms);
   check(accuracy == 100, "extraction byte-exact");
   check(png_ok, "extraction after PNG round trip byte-exact");

   free(extract);
   free(work);
   free(payload);
}

/* ------------------------------------------------------------------ */
/* regression: existing writers must be bit-identical                  */

static void writers_unchanged(void)
{
   enum { W = 64, H = 64 };
   unsigned char rgb[W * H * 3];
   unsigned char rgba[W * H * 4];
   unsigned char *png;
   membuf mb;
   int x, y, len = 0;

   for (y = 0; y < H; ++y) {
      for (x = 0; x < W; ++x) {
         unsigned char *p = rgb + (y * W + x) * 3;
         p[0] = (unsigned char)(x * 255 / (W - 1));
         p[1] = (unsigned char)(y * 255 / (H - 1));
         p[2] = (unsigned char)((x + y) * 255 / (W + H - 2));
         p = rgba + (y * W + x) * 4;
         p[0] = (unsigned char)(x * 2);
         p[1] = (unsigned char)(y * 3);
         p[2] = (unsigned char)((x * y) & 0xff);
         p[3] = (unsigned char)(255 - x * 255 / (W - 1));
      }
   }

   printf("  writer output hashes (must match pre-change library):\n");

   png = stbi_write_png_to_mem(rgb, W * 3, W, H, 3, &len);
   printf("    png_rgb   hash=%u len=%d\n", fnv1a(png, len), len);
   check(fnv1a(png, len) == 1287419114u && len == 470, "png rgb output unchanged");
   free(png);

   png = stbi_write_png_to_mem(rgba, W * 4, W, H, 4, &len);
   printf("    png_rgba  hash=%u len=%d\n", fnv1a(png, len), len);
   check(fnv1a(png, len) == 3382683496u && len == 2126, "png rgba output unchanged");
   free(png);

   mb.data = NULL; mb.len = mb.cap = 0;
   stbi_write_bmp_to_func(membuf_put, &mb, W, H, 3, rgb);
   printf("    bmp_rgb   hash=%u len=%d\n", fnv1a(mb.data, mb.len), mb.len);
   check(fnv1a(mb.data, mb.len) == 2650971793u && mb.len == 12342, "bmp output unchanged");

   mb.len = 0;
   stbi_write_tga_to_func(membuf_put, &mb, W, H, 3, rgb);
   printf("    tga_rgb   hash=%u len=%d\n", fnv1a(mb.data, mb.len), mb.len);
   check(fnv1a(mb.data, mb.len) == 556107647u && mb.len == 12370, "tga output unchanged");

   mb.len = 0;
   stbi_write_jpg_to_func(membuf_put, &mb, W, H, 3, rgb, 90);
   printf("    jpg_rgb   hash=%u len=%d\n", fnv1a(mb.data, mb.len), mb.len);
   check(fnv1a(mb.data, mb.len) == 234402628u && mb.len == 1127, "jpg output unchanged");

   free(mb.data);
}

/* ------------------------------------------------------------------ */
/* edge cases: strided rows, tiny slot counts, config validation       */

static void edge_cases(void)
{
   stbi_steg_config cfg;
   unsigned char payload[7] = { 's', 't', 'e', 'g', 0x00, 0xff, 0x42 };
   unsigned char out[16];
   int got = -1, r;

   printf("\n== edge cases ==\n");

   /* rows padded to a stride larger than w*comp: embedding must stay in the
    * pixel area and extraction must read the same bytes back */
   {
      enum { EW = 20, EH = 9, ECOMP = 3, PAD = 5 };
      ptrdiff_t rowbytes = EW * ECOMP + PAD;
      unsigned char *img = (unsigned char *)malloc((size_t)rowbytes * EH);
      unsigned char *copy = (unsigned char *)malloc((size_t)rowbytes * EH);
      int x, y;
      for (y = 0; y < EH; ++y)
         for (x = 0; x < EW * ECOMP + PAD; ++x)
            img[y * rowbytes + x] = (unsigned char)(x * 7 + y * 3);

      stbi_write_steg_default_config(&cfg);
      cfg.channel_mask = 0x7;
      r = stbi_write_steg_capacity(EW, EH, ECOMP, &cfg);
      check(r > 0, "strided capacity positive");
      memcpy(copy, img, (size_t)rowbytes * EH);
      check(stbi_write_steg_embed(img, EW, EH, ECOMP, rowbytes, &cfg, payload, 7) != 0,
            "strided embed succeeds");
      check(stbi_write_steg_extract(img, EW, EH, ECOMP, rowbytes, &cfg, out, 16, &got) != 0 &&
            got == 7 && memcmp(out, payload, 7) == 0, "strided extract byte-exact");

      /* padding bytes must be untouched */
      for (y = 0; y < EH; ++y) {
         int pad_ok = memcmp(img + y * rowbytes + EW * ECOMP,
                             copy + y * rowbytes + EW * ECOMP, PAD) == 0;
         check(pad_ok, "stride padding untouched");
      }
      free(img);
      free(copy);
   }

   /* a stride smaller than a row is not a valid layout */
   {
      unsigned char img[8 * 8 * 3];
      memset(img, 0, sizeof(img));
      stbi_write_steg_default_config(&cfg);
      check(stbi_write_steg_capacity(8, 8, 3, &cfg) > 0, "valid capacity");
      check(stbi_write_steg_embed(img, 8, 8, 3, 8 * 3 - 1, &cfg, payload, 7) == 0,
            "stride smaller than a row rejected");
   }

   /* an image too small to even hold the 12-byte header has no capacity */
   {
      unsigned char img[4 * 4 * 3];
      memset(img, 0, sizeof(img));
      stbi_write_steg_default_config(&cfg);
      check(stbi_write_steg_capacity(4, 4, 3, &cfg) < 0, "header-only image reports no capacity");
      check(stbi_write_steg_embed(img, 4, 4, 3, 0, &cfg, payload, 0) == 0,
            "header-only image rejects embedding");
   }

   /* tiny slot counts: cycle walking on a near-empty domain still round-trips */
   {
      static const int dims[][2] = {
         { 1, 104 }, { 13, 12 }, { 10, 31 }, { 31, 10 }, { 2, 53 }, { 17, 17 }, { 23, 9 }
      };
      int d;
      for (d = 0; d < (int)(sizeof(dims) / sizeof(dims[0])); ++d) {
         int w = dims[d][0], h = dims[d][1];
         unsigned char *img = (unsigned char *)malloc((size_t)w * h);
         int cap;
         memset(img, 0x5a, (size_t)w * h);
         stbi_write_steg_default_config(&cfg);
         cfg.channel_mask = 0x1;
         cfg.randomize = 1;
         cfg.seed = 0xabcdef01u + (unsigned int)d;
         cap = stbi_write_steg_capacity(w, h, 1, &cfg);
         check(cap >= 1, "tiny image has capacity");
         if (cap >= 1) {
            int plen = cap < 7 ? cap : 7;
            got = -1;
            check(stbi_write_steg_embed(img, w, h, 1, 0, &cfg, payload, plen) != 0,
                  "tiny image embed");
            check(stbi_write_steg_extract(img, w, h, 1, 0, &cfg, out, 16, &got) != 0 &&
                  got == plen && memcmp(out, payload, (size_t)plen) == 0, "tiny image extract");

            /* and the same image read with the wrong seed must not verify */
            cfg.seed += 1u;
            check(stbi_write_steg_extract(img, w, h, 1, 0, &cfg, out, 16, &got) == 0,
                  "tiny image wrong seed rejected");
         }
         free(img);
      }
   }

   /* invalid configurations are rejected, not crashed on */
   {
      unsigned char img[16 * 16 * 3];
      memset(img, 0, sizeof(img));
      stbi_write_steg_default_config(&cfg);
      cfg.bit_depth = 0;
      check(stbi_write_steg_capacity(16, 16, 3, &cfg) < 0, "bit_depth 0 rejected");
      cfg.bit_depth = 9;
      check(stbi_write_steg_capacity(16, 16, 3, &cfg) < 0, "bit_depth 9 rejected");
      cfg.bit_depth = 1;
      cfg.pixel_stride = 0;
      check(stbi_write_steg_capacity(16, 16, 3, &cfg) < 0, "pixel_stride 0 rejected");
      cfg.pixel_stride = 1;
      cfg.channel_mask = 0;
      check(stbi_write_steg_capacity(16, 16, 3, &cfg) < 0, "empty channel mask rejected");
      check(stbi_write_steg_embed(NULL, 16, 16, 3, 0, &cfg, payload, 7) == 0,
            "NULL pixels rejected");
      stbi_write_steg_default_config(&cfg);
      check(stbi_write_steg_embed(img, 0, 16, 3, 0, &cfg, payload, 7) == 0,
            "zero width rejected");
      check(stbi_write_steg_embed(img, 16, 16, 3, 0, &cfg, NULL, 3) == 0,
            "NULL payload rejected");
      check(stbi_write_steg_embed(img, 16, 16, 3, 0, &cfg, payload, -1) == 0,
            "negative length rejected");
   }

   /* extract into a buffer that is too small must fail, not overflow */
   {
      unsigned char img[16 * 16 * 3];
      memset(img, 0, sizeof(img));
      stbi_write_steg_default_config(&cfg);
      cfg.channel_mask = 0x7;
      check(stbi_write_steg_embed(img, 16, 16, 3, 0, &cfg, payload, 7) != 0, "small embed");
      got = -1;
      check(stbi_write_steg_extract(img, 16, 16, 3, 0, &cfg, out, 4, &got) == 0,
            "undersized extract buffer rejected");
      check(got == 0, "failed extract reports zero length");
   }
}

/* ------------------------------------------------------------------ */

int main(void)
{
   enum { W = 96, H = 96 };
   image imgs[4];
   int i;
   static const test_config configs_rgb[] = {
      { "all-1b",    0x7, 1, 1, 0, 0u },
      { "all-2b-rs", 0x7, 2, 1, 1, 0x5eed1234u },
      { "blue-s3",   0x4, 1, 3, 1, 7u },
      { "rg-3b-s2",  0x3, 3, 2, 0, 0u },
      { "red-1b",    0x1, 1, 1, 0, 0u },
   };
   static const test_config configs_rgba[] = {
      { "all-1b",    0xf, 1, 1, 0, 0u },
      { "all-2b-rs", 0xf, 2, 1, 1, 0x5eed1234u },
      { "alpha-s3",  0x8, 1, 3, 1, 7u },
      { "rgb-3b-s2", 0x7, 3, 2, 0, 0u },
      { "red-1b",    0x1, 1, 1, 0, 0u },
   };
   static const char *names[4] = { "smooth", "noise", "jpegish", "alpha" };

   printf("== existing writer regression ==\n");
   writers_unchanged();

   edge_cases();

   make_smooth(&imgs[0], W, H);
   make_noise(&imgs[1], W, H);
   make_jpegish(&imgs[2], W, H);
   make_alpha(&imgs[3], W, H);

   for (i = 0; i < 4; ++i) {
      const test_config *cfgs = imgs[i].comp == 4 ? configs_rgba : configs_rgb;
      int ncfg = imgs[i].comp == 4 ? (int)(sizeof(configs_rgba) / sizeof(configs_rgba[0]))
                                   : (int)(sizeof(configs_rgb) / sizeof(configs_rgb[0]));
      int j;
      printf("\n== image %s (%dx%d, %d ch) ==\n", names[i], imgs[i].w, imgs[i].h, imgs[i].comp);
      for (j = 0; j < ncfg; ++j)
         run_config(&imgs[i], &cfgs[j]);
      image_free(&imgs[i]);
   }

   printf("\n%d checks, %d failures\n", g_checks, g_failures);
   return g_failures ? 1 : 0;
}
