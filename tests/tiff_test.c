// TIFF loader test for stb_image.h — self-contained: TIFF samples are
// generated in memory, decoded, and compared against exact expected pixels.
// Also exercises corrupt-strip failure paths, req_comp, info/is_16_bit,
// multi-page files, both byte orders, and times the PackBits/LZW paths.
//
// Usage: tiff_test [data_dir]
//
// Compile with -DSTBI_HAS_TIF=1 against the TIFF-enabled stb_image.h.
// Without STBI_HAS_TIF only the existing-format regression checksums are
// compiled, so the same file builds against the pre-change header for
// before/after comparison.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// track decoder heap usage (memory upper bound measurement + leak check)
static size_t mem_cur, mem_peak;
static void *trk_malloc(size_t n)
{
   size_t *p = (size_t *) malloc(n + sizeof(size_t));
   if (!p) return NULL;
   *p = n;
   mem_cur += n;
   if (mem_cur > mem_peak) mem_peak = mem_cur;
   return p + 1;
}
static void trk_free(void *q)
{
   if (q) {
      size_t *p = (size_t *) q - 1;
      mem_cur -= *p;
      free(p);
   }
}
static void *trk_realloc(void *q, size_t n)
{
   size_t *p;
   size_t old = 0;
   if (q) {
      p = (size_t *) q - 1;
      old = *p;
   }
   p = (size_t *) realloc(q ? (size_t *) q - 1 : NULL, n + sizeof(size_t));
   if (!p) return NULL;
   *p = n;
   mem_cur += n - old;
   if (mem_cur > mem_peak) mem_peak = mem_cur;
   return p + 1;
}
#define STBI_MALLOC trk_malloc
#define STBI_FREE trk_free
#define STBI_REALLOC trk_realloc

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); ++failures; } } while (0)

// ---------------------------------------------------------------------------
// existing-format regression: hash decodes of the repo's sample images so two
// builds (before/after the TIFF change) can be diffed
// ---------------------------------------------------------------------------
static unsigned long long fnv1a(const unsigned char *p, size_t n)
{
   unsigned long long h = 1469598103934665603ULL;
   size_t i;
   for (i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
   return h;
}

static void regression(const char *dir, const char *name)
{
   char path[1024];
   int x = 0, y = 0, n = 0;
   unsigned char *p8;
   stbi__uint16 *p16;
   stbi_info_res info;
   snprintf(path, sizeof path, "%s/%s", dir, name);
   info = stbi_info2(path);
   p8 = stbi_load(path, &x, &y, &n, 0);
   if (!p8) { printf("REG %s LOAD-FAIL (%s)\n", name, stbi_failure_reason()); ++failures; return; }
   printf("REG %-28s %4dx%-4d n=%d h8=%016llx", name, x, y, n, fnv1a(p8, (size_t)x*y*n));
   stbi_image_free(p8);
   p16 = stbi_load_16(path, &x, &y, &n, 0);
   if (!p16) { printf(" h16=LOAD-FAIL"); ++failures; }
   else { printf(" h16=%016llx", fnv1a((unsigned char *) p16, (size_t)x*y*n*2)); stbi_image_free(p16); }
   printf(" info=%d,%d,%d,%d,%d\n", info.x, info.y, info.comp, info.type, info.is_16_bit);
}

#ifdef STBI_HAS_TIF

// ---------------------------------------------------------------------------
// TIFF sample generation
// ---------------------------------------------------------------------------
typedef struct { unsigned char *d; size_t len, cap; } buf;

static void bput(buf *b, const void *p, size_t n)
{
   if (b->len + n > b->cap) {
      size_t nc = b->cap ? b->cap * 2 : 256;
      while (nc < b->len + n) nc *= 2;
      b->d = (unsigned char *) realloc(b->d, nc);
      if (!b->d) { fprintf(stderr, "oom\n"); exit(2); }
      b->cap = nc;
   }
   memcpy(b->d + b->len, p, n);
   b->len += n;
}
static void b8(buf *b, unsigned v) { unsigned char c = (unsigned char) v; bput(b, &c, 1); }
static void b16(buf *b, unsigned v, int le)
{
   if (le) { b8(b, v & 255); b8(b, (v >> 8) & 255); }
   else    { b8(b, (v >> 8) & 255); b8(b, v & 255); }
}
static void b32(buf *b, unsigned v, int le)
{
   if (le) { b16(b, v & 65535, le); b16(b, (v >> 16) & 65535, le); }
   else    { b16(b, (v >> 16) & 65535, le); b16(b, v & 65535, le); }
}
static void bset32(buf *b, size_t off, unsigned v, int le)
{
   if (le) { b->d[off] = (unsigned char) v; b->d[off+1] = (unsigned char)(v>>8); b->d[off+2] = (unsigned char)(v>>16); b->d[off+3] = (unsigned char)(v>>24); }
   else    { b->d[off] = (unsigned char)(v>>24); b->d[off+1] = (unsigned char)(v>>16); b->d[off+2] = (unsigned char)(v>>8); b->d[off+3] = (unsigned char) v; }
}

// deterministic pixel patterns (host-order values)
static unsigned char pix8(int seed, int x, int y, int c)
{
   unsigned v = (unsigned) (x*7 + y*13 + c*29 + seed*3 + ((x*y) >> 3));
   return (unsigned char) ((((unsigned) x >> 2)*17 + v) & 0xff);
}
static unsigned short pix16(int seed, int x, int y, int c)
{
   return (unsigned short) ((x*257 + y*101 + c*407 + seed*11 + ((x*y) >> 2)) & 0xffff);
}

// PackBits encoder
static void packbits_enc(buf *out, const unsigned char *src, size_t n)
{
   size_t i = 0;
   while (i < n) {
      size_t run = 1;
      while (i + run < n && src[i+run] == src[i] && run < 128) run++;
      if (run >= 3) {
         b8(out, (unsigned char) (1 - (int) run)); // replicate packet: header 257-run
         b8(out, src[i]);
         i += run;
      } else {
         size_t start = i, lit;
         i += run;
         while (i < n) {
            run = 1;
            while (i + run < n && src[i+run] == src[i] && run < 128) run++;
            if (run >= 3 || i - start + run > 128) break;
            i += run;
         }
         lit = i - start;
         b8(out, (unsigned char) (lit - 1)); // literal packet: header lit-1
         bput(out, src + start, lit);
      }
   }
}

// TIFF LZW encoder (MSB-first codes, early-change width growth)
static void lzw_enc(buf *out, const unsigned char *src, size_t n)
{
   static int hcode[8192];
   static unsigned hkey[8192];
   unsigned acc = 0;
   int nbits = 0, width = 9, free_code = 258;
   size_t i;
   int old;
   #define LZW_PUT(code) do { \
      acc = (acc << width) | (unsigned) (code); \
      nbits += width; \
      while (nbits >= 8) { nbits -= 8; b8(out, (acc >> nbits) & 0xff); } \
   } while (0)
   memset(hcode, 0, sizeof hcode);
   LZW_PUT(256); // clear
   if (n == 0) { LZW_PUT(257); goto flush; }
   old = src[0];
   for (i = 1; i < n; ++i) {
      int k = src[i];
      unsigned key = ((unsigned) old << 8) | (unsigned) k;
      unsigned h = (key * 2654435761u) >> 19;
      int found = 0, empty = -1, j;
      for (j = 0; j < 8192; ++j) {
         int slot = (int) ((h + (unsigned) j) & 8191);
         if (hcode[slot] == 0) { empty = slot; break; }
         if (hkey[slot] == key) { found = hcode[slot]; break; }
      }
      if (found) {
         old = found;
      } else {
         LZW_PUT(old);
         if (free_code < 4096) {
            hkey[empty] = key;
            hcode[empty] = free_code;
            ++free_code;
            if (free_code > (1 << width) - 1 && width < 12)
               ++width;
         } else {
            LZW_PUT(256);
            memset(hcode, 0, sizeof hcode);
            width = 9;
            free_code = 258;
         }
         old = k;
      }
   }
   LZW_PUT(old);
   LZW_PUT(257); // end of information
flush:
   if (nbits) b8(out, (acc << (8 - nbits)) & 0xff);
   #undef LZW_PUT
}

enum { C_NONE = 0, C_BAD_OFFSET, C_SHORT_COUNT, C_TRUNCATED };

// append one TIFF page (IFD + tag arrays + strip data) to *b.
// raw: pixel bytes in file byte order, w*h*spp*(bps/8) bytes.
// returns the IFD offset; *next_off gets the location of its next-IFD field.
static size_t tif_page(buf *b, int le, int w, int h, int spp, int bps, int comp,
                       int photo, int pred, int rps, const unsigned char *raw,
                       int corrupt, size_t *next_off)
{
   int bpc = bps / 8;
   int nstrips = (h + rps - 1) / rps;
   int nentries = pred != 1 ? 11 : 10;
   size_t ifd_off = b->len;
   size_t entries_end = ifd_off + 2 + (size_t) nentries * 12 + 4;
   size_t bps_arr = 0, off_arr = 0, cnt_arr = 0, data_off, cursor, off;
   buf *strips = (buf *) calloc((size_t) nstrips, sizeof(buf));
   int i, e = 0;
   #define ENTRY(tag, type, count, val) do { \
      b16(b, (tag), le); b16(b, (type), le); b32(b, (count), le); b32(b, (val), le); ++e; \
   } while (0)

   // encode strips
   {
      size_t row_bytes = (size_t) w * spp * bpc;
      for (i = 0; i < nstrips; ++i) {
         int rows = h - i * rps;
         size_t n;
         if (rows > rps) rows = rps;
         n = row_bytes * (size_t) rows;
         if (corrupt == C_TRUNCATED && n > 1) n /= 2; // encode less data than the image needs
         if (comp == 1)
            bput(&strips[i], raw + (size_t) i * rps * row_bytes, n);
         else if (comp == 32773)
            packbits_enc(&strips[i], raw + (size_t) i * rps * row_bytes, n);
         else
            lzw_enc(&strips[i], raw + (size_t) i * rps * row_bytes, n);
      }
   }

   // layout: IFD, then out-of-line tag arrays, then strip data
   cursor = entries_end;
   if (spp > 2)     { bps_arr = cursor; cursor += (size_t) spp * 2; }
   if (nstrips > 1) { off_arr = cursor; cursor += (size_t) nstrips * 4; }
   if (nstrips > 1) { cnt_arr = cursor; cursor += (size_t) nstrips * 4; }
   data_off = cursor;

   b16(b, (unsigned) nentries, le);
   ENTRY(256, 4, 1, (unsigned) w);            // ImageWidth
   ENTRY(257, 4, 1, (unsigned) h);            // ImageLength
   if (spp == 1)
      ENTRY(258, 3, 1, le ? (unsigned) bps : (unsigned) bps << 16);
   else if (spp == 2)
      ENTRY(258, 3, 2, le ? ((unsigned) bps | ((unsigned) bps << 16))
                          : ((unsigned) bps << 16 | (unsigned) bps));
   else
      ENTRY(258, 3, (unsigned) spp, (unsigned) bps_arr);
   ENTRY(259, 3, 1, le ? (unsigned) comp : (unsigned) comp << 16); // Compression
   ENTRY(262, 3, 1, le ? (unsigned) photo : (unsigned) photo << 16); // Photometric
   ENTRY(273, 4, (unsigned) nstrips, nstrips == 1 ? 0 : (unsigned) off_arr); // StripOffsets (patched below if inline)
   ENTRY(277, 3, 1, le ? (unsigned) spp : (unsigned) spp << 16); // SamplesPerPixel
   ENTRY(278, 4, 1, (unsigned) rps);          // RowsPerStrip
   ENTRY(279, 4, (unsigned) nstrips, nstrips == 1 ? 0 : (unsigned) cnt_arr); // StripByteCounts (patched below if inline)
   ENTRY(284, 3, 1, le ? 1u : 1u << 16);      // PlanarConfiguration
   if (pred != 1)
      ENTRY(317, 3, 1, le ? (unsigned) pred : (unsigned) pred << 16); // Predictor
   *next_off = b->len;
   b32(b, 0, le);                             // next IFD offset: none (patched by caller)
   if (e != nentries) { printf("builder bug\n"); exit(2); }

   // out-of-line arrays
   if (spp > 2)
      for (i = 0; i < spp; ++i) b16(b, (unsigned) bps, le);
   if (nstrips > 1) {
      off = data_off;
      for (i = 0; i < nstrips; ++i) {
         unsigned v = (unsigned) off;
         if (corrupt == C_BAD_OFFSET && i == 0) v += 0x40000000u; // way past EOF
         b32(b, v, le);
         off += strips[i].len;
      }
      for (i = 0; i < nstrips; ++i) {
         unsigned v = (unsigned) strips[i].len;
         if (corrupt == C_SHORT_COUNT && i == 0 && v > 0) v -= 1; // count smaller than the image needs
         b32(b, v, le);
      }
   } else {
      // single strip: patch the inline entry values (entry 5 = StripOffsets, entry 8 = StripByteCounts)
      unsigned voff = (unsigned) data_off;
      unsigned vcnt = (unsigned) strips[0].len;
      if (corrupt == C_BAD_OFFSET) voff += 0x40000000u;
      if (corrupt == C_SHORT_COUNT && vcnt > 0) vcnt -= 1;
      bset32(b, ifd_off + 2 + 5 * 12 + 8, voff, le);
      bset32(b, ifd_off + 2 + 8 * 12 + 8, vcnt, le);
   }
   for (i = 0; i < nstrips; ++i) {
      bput(b, strips[i].d, strips[i].len);
      free(strips[i].d);
   }
   free(strips);
   return ifd_off;
   #undef ENTRY
}

// build a complete TIFF file; for npages==2 the second page uses seed+100
static buf make_tiff(int le, int w, int h, int spp, int bps, int comp, int photo,
                     int pred, int rps, int seed, int npages, int corrupt)
{
   buf b = { 0, 0, 0 };
   buf raw = { 0, 0, 0 };
   int x, y, c, page;
   size_t first_ifd = 0, next_off = 0;
   int bpc = bps / 8;

   // header (first-IFD offset patched below)
   if (le) { b8(&b, 'I'); b8(&b, 'I'); b16(&b, 42, 1); }
   else    { b8(&b, 'M'); b8(&b, 'M'); b16(&b, 42, 0); }
   b32(&b, 8, le);

   for (page = 0; page < npages; ++page) {
      int pseed = seed + page * 100;
      size_t ifd;
      raw.len = 0;
      for (y = 0; y < h; ++y)
         for (x = 0; x < w; ++x)
            for (c = 0; c < spp; ++c) {
               if (bps == 8) b8(&raw, pix8(pseed, x, y, c));
               else b16(&raw, pix16(pseed, x, y, c), le);
            }
      // horizontal-differencing predictor is applied to the stored data
      if (pred == 2) {
         int j, k;
         int stride = w * spp;
         for (j = 0; j < h; ++j) {
            unsigned char *row = raw.d + (size_t) j * stride * bpc;
            if (bpc == 1) {
               for (k = stride - 1; k >= spp; --k)
                  row[k] = (unsigned char) (row[k] - row[k - spp]);
            } else {
               for (k = stride - 1; k >= spp; --k) {
                  unsigned short v, p;
                  const unsigned char *a = row + 2*k, *q = row + 2*(k - spp);
                  if (le) { v = (unsigned short)(a[0] | (a[1] << 8)); p = (unsigned short)(q[0] | (q[1] << 8)); }
                  else    { v = (unsigned short)((a[0] << 8) | a[1]); p = (unsigned short)((q[0] << 8) | q[1]); }
                  v = (unsigned short) (v - p);
                  if (le) { row[2*k] = (unsigned char) v; row[2*k+1] = (unsigned char) (v >> 8); }
                  else    { row[2*k] = (unsigned char) (v >> 8); row[2*k+1] = (unsigned char) v; }
               }
            }
         }
      }
      ifd = tif_page(&b, le, w, h, spp, bps, comp, photo, pred, rps, raw.d,
                     page == 0 ? corrupt : C_NONE, &next_off);
      if (page == 0)
         first_ifd = ifd;
      if (page + 1 < npages)
         bset32(&b, next_off, (unsigned) b.len, le); // link to the next page's IFD
   }
   free(raw.d);
   bset32(&b, 4, (unsigned) first_ifd, le);
   return b;
}

// ---------------------------------------------------------------------------
// TIFF test cases
// ---------------------------------------------------------------------------
static void expect_pixels8(const char *name, const unsigned char *got,
                           int w, int h, int spp, int seed, int photo)
{
   int x, y, c, bad = 0;
   for (y = 0; y < h && !bad; ++y)
      for (x = 0; x < w && !bad; ++x)
         for (c = 0; c < spp && !bad; ++c) {
            unsigned char want = pix8(seed, x, y, c);
            if (photo == 0) want = (unsigned char) (255 - want);
            if (got[(size_t)(y*w + x)*spp + c] != want) {
               printf("FAIL %s: pixel (%d,%d,%d) got %u want %u\n", name, x, y, c,
                      got[(size_t)(y*w + x)*spp + c], want);
               ++failures;
               bad = 1;
            }
         }
}

static void expect_pixels16(const char *name, const stbi__uint16 *got,
                            int w, int h, int spp, int seed, int photo)
{
   int x, y, c, bad = 0;
   for (y = 0; y < h && !bad; ++y)
      for (x = 0; x < w && !bad; ++x)
         for (c = 0; c < spp && !bad; ++c) {
            unsigned short want = pix16(seed, x, y, c);
            if (photo == 0) want = (unsigned short) (65535 - want);
            if (got[(size_t)(y*w + x)*spp + c] != want) {
               printf("FAIL %s: pixel16 (%d,%d,%d) got %u want %u\n", name, x, y, c,
                      got[(size_t)(y*w + x)*spp + c], want);
               ++failures;
               bad = 1;
            }
         }
}

// decode one sample via the 8-bit and 16-bit APIs and verify exact pixels
static void valid_case(const char *name, int le, int w, int h, int spp, int bps,
                       int comp, int photo, int pred, int rps, int seed)
{
   buf t = make_tiff(le, w, h, spp, bps, comp, photo, pred, rps, seed, 1, C_NONE);
   int x = 0, y = 0, n = 0, ix = 0, iy = 0, in = 0;
   unsigned char *p8;
   stbi__uint16 *p16;
   size_t peak;

   if (!stbi_info_from_memory(t.d, (int) t.len, &ix, &iy, &in)) {
      printf("FAIL %s: stbi_info_from_memory failed (%s)\n", name, stbi_failure_reason());
      ++failures;
   } else {
      CHECK(ix == w && iy == h && in == spp, "info dims mismatch");
   }
   CHECK(stbi_is_16_bit_from_memory(t.d, (int) t.len) == (bps == 16), "is_16_bit mismatch");

   // 8-bit decode
   mem_peak = mem_cur;
   p8 = stbi_load_from_memory(t.d, (int) t.len, &x, &y, &n, 0);
   if (!p8) {
      printf("FAIL %s: 8-bit decode failed (%s)\n", name, stbi_failure_reason());
      ++failures;
   } else {
      CHECK(x == w && y == h && n == spp, "dims/comp mismatch");
      if (bps == 8) {
         expect_pixels8(name, p8, w, h, spp, seed, photo);
      } else {
         // stb_image converts 16->8 by taking the high byte
         int xx, yy, cc, bad = 0;
         for (yy = 0; yy < h && !bad; ++yy)
            for (xx = 0; xx < w && !bad; ++xx)
               for (cc = 0; cc < spp && !bad; ++cc) {
                  unsigned short v16 = pix16(seed, xx, yy, cc);
                  unsigned char want;
                  if (photo == 0) v16 = (unsigned short) (65535 - v16);
                  want = (unsigned char) (v16 >> 8);
                  if (p8[(size_t)(yy*w + xx)*spp + cc] != want) {
                     printf("FAIL %s: 16->8 pixel (%d,%d,%d) got %u want %u\n",
                            name, xx, yy, cc, p8[(size_t)(yy*w + xx)*spp + cc], want);
                     ++failures;
                     bad = 1;
                  }
               }
      }
      stbi_image_free(p8);
   }
   peak = mem_peak - mem_cur; // mem_cur is back to baseline after the free
   if (comp != 1)
      printf("MEM %-26s peak=%zu file=%zu img=%d strip~=%d\n", name, peak, t.len,
             w*h*spp*(bps/8), w*rps*spp*(bps/8));

   // 16-bit decode
   p16 = stbi_load_16_from_memory(t.d, (int) t.len, &x, &y, &n, 0);
   if (!p16) {
      printf("FAIL %s: 16-bit decode failed (%s)\n", name, stbi_failure_reason());
      ++failures;
   } else {
      CHECK(x == w && y == h && n == spp, "dims/comp mismatch (16-bit)");
      if (bps == 16) {
         expect_pixels16(name, p16, w, h, spp, seed, photo);
      } else {
         // stb_image converts 8->16 by replicating the byte
         int xx, yy, cc, bad = 0;
         for (yy = 0; yy < h && !bad; ++yy)
            for (xx = 0; xx < w && !bad; ++xx)
               for (cc = 0; cc < spp && !bad; ++cc) {
                  unsigned char v8 = pix8(seed, xx, yy, cc);
                  unsigned short want;
                  if (photo == 0) v8 = (unsigned char) (255 - v8);
                  want = (unsigned short) ((v8 << 8) | v8);
                  if (p16[(size_t)(yy*w + xx)*spp + cc] != want) {
                     printf("FAIL %s: 8->16 pixel (%d,%d,%d)\n", name, xx, yy, cc);
                     ++failures;
                     bad = 1;
                  }
               }
      }
      stbi_image_free(p16);
   }
   printf("OK  %s\n", name);
   free(t.d);
}

static void timing_case(const char *name, int comp, int iters)
{
   int w = 256, h = 256, spp = 3;
   buf t = make_tiff(1, w, h, spp, 8, comp, spp >= 3 ? 2 : 1, 1, 32, 5, 1, C_NONE);
   int x = 0, y = 0, n = 0, i;
   clock_t t0;
   unsigned char *p = stbi_load_from_memory(t.d, (int) t.len, &x, &y, &n, 0);
   if (!p) { printf("FAIL timing %s: %s\n", name, stbi_failure_reason()); ++failures; free(t.d); return; }
   printf("TIME %-8s %dx%dx%d first_px=(%u,%u,%u) ", name, x, y, n, p[0], p[1], p[2]);
   stbi_image_free(p);
   t0 = clock();
   for (i = 0; i < iters; ++i) {
      p = stbi_load_from_memory(t.d, (int) t.len, &x, &y, &n, 0);
      if (!p) { printf("FAIL timing iter\n"); ++failures; break; }
      stbi_image_free(p);
   }
   printf("%.1f us/decode (%d iters, file=%zu)\n",
          1e6 * (double) (clock() - t0) / CLOCKS_PER_SEC / iters, iters, t.len);
   free(t.d);
}

static void corrupt_case(const char *name, buf *t)
{
   int x = 0, y = 0, n = 0;
   unsigned char *p = stbi_load_from_memory(t->d, (int) t->len, &x, &y, &n, 0);
   if (p != NULL) {
      printf("FAIL %s: corrupt input decoded successfully\n", name);
      ++failures;
      stbi_image_free(p);
   } else {
      printf("OK  %-34s rejected: %s\n", name, stbi_failure_reason());
   }
}

static void tiff_tests(void)
{
   // uncompressed
   valid_case("raw/8bit/rgb/LE/multi-strip",  1, 96, 64, 3, 8,  1, 2, 1, 8,  1);
   valid_case("raw/8bit/gray/BE/whiteiszero", 0, 64, 48, 1, 8,  1, 0, 1, 16, 2);
   valid_case("raw/16bit/gray/LE",            1, 64, 64, 1, 16, 1, 1, 1, 64, 3);
   valid_case("raw/16bit/rgb/BE",             0, 32, 32, 3, 16, 1, 2, 1, 8,  4);
   valid_case("raw/8bit/ga/LE",               1, 40, 40, 2, 8,  1, 1, 1, 40, 5);
   valid_case("raw/8bit/rgba/LE",             1, 40, 40, 4, 8,  1, 2, 1, 40, 6);
   // PackBits
   valid_case("packbits/8bit/rgb/LE",         1, 96, 64, 3, 8,  32773, 2, 1, 8,  7);
   valid_case("packbits/8bit/gray/BE",        0, 64, 48, 1, 8,  32773, 1, 1, 12, 8);
   valid_case("packbits/16bit/gray/LE",       1, 64, 64, 1, 16, 32773, 1, 1, 64, 9);
   // LZW
   valid_case("lzw/8bit/rgb/LE",              1, 96, 64, 3, 8,  5, 2, 1, 8,  10);
   valid_case("lzw/8bit/gray/BE",             0, 64, 48, 1, 8,  5, 1, 1, 12, 11);
   valid_case("lzw/16bit/gray/LE",            1, 64, 64, 1, 16, 5, 1, 1, 64, 12);
   valid_case("lzw/8bit/rgb/LE/predictor2",   1, 96, 64, 3, 8,  5, 2, 2, 8,  13);
   valid_case("lzw/16bit/gray/LE/predictor2", 1, 64, 64, 1, 16, 5, 1, 2, 64, 14);
   valid_case("raw/8bit/rgb/LE/predictor2",   1, 64, 32, 3, 8,  1, 2, 2, 16, 15);

   // multi-page: must return the first page (seed 20, not 120)
   {
      buf t = make_tiff(1, 48, 32, 3, 8, 1, 2, 1, 8, 20, 2, C_NONE);
      int x = 0, y = 0, n = 0;
      unsigned char *p = stbi_load_from_memory(t.d, (int) t.len, &x, &y, &n, 0);
      if (!p) { printf("FAIL multipage: %s\n", stbi_failure_reason()); ++failures; }
      else {
         CHECK(x == 48 && y == 32 && n == 3, "multipage dims");
         expect_pixels8("multipage-first", p, 48, 32, 3, 20, 2);
         stbi_image_free(p);
         printf("OK  multipage (first page returned)\n");
      }
      free(t.d);
   }

   // req_comp conversion
   {
      buf t = make_tiff(1, 32, 32, 3, 8, 1, 2, 1, 32, 30, 1, C_NONE);
      int x = 0, y = 0, n = 0, i, bad = 0;
      unsigned char *p = stbi_load_from_memory(t.d, (int) t.len, &x, &y, &n, 4);
      if (!p) { printf("FAIL req_comp: %s\n", stbi_failure_reason()); ++failures; }
      else {
         CHECK(x == 32 && y == 32 && n == 3, "req_comp dims");
         for (i = 0; i < 32*32; ++i)
            if (p[i*4+3] != 255) { bad = 1; break; }
         CHECK(!bad, "req_comp=4 alpha not 255");
         stbi_image_free(p);
         printf("OK  req_comp=4 (alpha=255)\n");
      }
      free(t.d);
   }

   // corrupt inputs: must fail cleanly, never crash or read out of bounds
   {
      buf t = make_tiff(1, 32, 32, 3, 8, 1, 2, 1, 8, 40, 1, C_BAD_OFFSET);
      corrupt_case("corrupt/strip-offset-past-eof", &t);
      free(t.d);
   }
   {
      buf t = make_tiff(1, 32, 32, 3, 8, 1, 2, 1, 8, 41, 1, C_SHORT_COUNT);
      corrupt_case("corrupt/uncomp-count-too-small", &t);
      free(t.d);
   }
   {
      buf t = make_tiff(1, 64, 32, 3, 8, 32773, 2, 1, 8, 42, 1, C_TRUNCATED);
      corrupt_case("corrupt/packbits-truncated", &t);
      free(t.d);
   }
   {
      buf t = make_tiff(1, 64, 32, 3, 8, 5, 2, 1, 8, 43, 1, C_TRUNCATED);
      corrupt_case("corrupt/lzw-truncated", &t);
      free(t.d);
   }
   {
      buf t = make_tiff(1, 32, 32, 3, 8, 1, 2, 1, 32, 44, 1, C_NONE);
      bset32(&t, 4, 0x7fffff00u, 1); // first-IFD offset way past EOF
      corrupt_case("corrupt/ifd-offset-past-eof", &t);
      free(t.d);
   }
   {
      // LZW stream of garbage bytes behind a valid header/IFD
      buf t = make_tiff(1, 64, 32, 3, 8, 5, 2, 1, 8, 45, 1, C_NONE);
      size_t i;
      for (i = t.len / 2; i < t.len; ++i) t.d[i] = (unsigned char) (i * 37 + 11);
      {
         int x = 0, y = 0, n = 0;
         unsigned char *p = stbi_load_from_memory(t.d, (int) t.len, &x, &y, &n, 0);
         // garbage may decode to wrong pixels but must never crash/overflow;
         // with exact-length checking it should normally fail
         printf("OK  corrupt/lzw-garbage handled (%s)\n", p ? "decoded, no crash" : stbi_failure_reason());
         if (p) stbi_image_free(p);
      }
      free(t.d);
   }

   // pixel + timing line for each of the two compressions
   timing_case("packbits", 32773, 2000);
   timing_case("lzw", 5, 2000);
}
#endif // STBI_HAS_TIF

int main(int argc, char **argv)
{
   const char *dir = argc > 1 ? argv[1] : "data";
#ifdef STBI_HAS_TIF
   tiff_tests();
#else
   printf("(built without STBI_HAS_TIF: regression checksums only)\n");
#endif
   regression(dir, "atari_8bit_font_revised.png");
   regression(dir, "easy_font_raw.png");
   regression(dir, "map_01.png");
   regression(dir, "map_02.png");
   regression(dir, "map_03.png");
   if (mem_cur != 0) {
      printf("FAIL: decoder leaked %zu bytes\n", mem_cur);
      ++failures;
   }
   printf(failures ? "RESULT: %d FAILURES\n" : "RESULT: ALL PASS\n", failures);
   return failures ? 1 : 0;
}
