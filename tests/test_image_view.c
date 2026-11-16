// Tests for stbi_view_from_memory(): zero-copy views vs decoding fallbacks.
//
// Covers four kinds of input:
//   1. viewable formats (binary PGM/PPM, uncompressed 24-bit BMP, uncompressed TGA)
//   2. formats that must be decoded (PNG, RLE TGA, ASCII PNM, 16-bit PNM, garbage)
//   3. rows whose storage stride does not equal width*channels (BMP row padding,
//      bottom-up BMP/TGA with a negative stride)
//   4. a read-only input buffer (mmap'd PROT_READ), proving views never write
//
// For every case it checks the reported view parameters, whether a copy took
// place, pixel-for-pixel agreement with stbi_load_from_memory(), and the
// aliasing/lifetime rules (view pixels live inside the caller's buffer; the
// fallback pixels are an ordinary stbi_load allocation).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#if defined(__unix__) || defined(__APPLE__)
#define HAVE_MMAP 1
#include <sys/mman.h>
#include <unistd.h>
#endif

static int g_failures;
static int g_checks;

#define CHECK(cond, ...) do {                                        \
   ++g_checks;                                                       \
   if (!(cond)) {                                                    \
      ++g_failures;                                                  \
      printf("FAIL %s:%d: ", __FILE__, __LINE__);                    \
      printf(__VA_ARGS__);                                           \
      printf("\n");                                                  \
   }                                                                 \
} while (0)

// ---- sample images -------------------------------------------------------

// 3x2 binary PGM, maxval 255: 6 bytes of samples right after "255\n"
static const stbi_uc pgm3x2[] =
   "P5\n3 2\n255\n"
   "\x00\x11\x22\x33\x44\x55";

// 3x2 binary PPM, maxval 255: 18 bytes of samples
static const stbi_uc ppm3x2[] =
   "P6\n3 2\n255\n"
   "\x01\x02\x03\x04\x05\x06\x07\x08\x09"
   "\x0a\x0b\x0c\x0d\x0e\x0f\x10\x11\x12";

// 3x2 16-bit PGM, maxval 65535: view must refuse (samples are 16-bit)
static const stbi_uc pgm16_3x2[] =
   "P5\n3 2\n65535\n"
   "\x00\x00\x11\x11\x22\x22\x33\x33\x44\x44\x55\x55";

// 3x2 ASCII PGM: view must refuse (must parse text)
static const stbi_uc pgma_3x2[] =
   "P2\n3 2\n255\n"
   "0 17 34 51 68 85\n";

// 2x2 RGB PNG (deflate-compressed): view must refuse, fallback must decode
static const stbi_uc png2x2[] = {
   0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,
   0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x02,0x08,0x02,0x00,0x00,0x00,0xfd,0xd4,0x9a,
   0x73,0x00,0x00,0x00,0x14,0x49,0x44,0x41,0x54,0x78,0xda,0x63,0xf8,0xcf,0xc0,0xc0,
   0x00,0xc2,0x0c,0xff,0xff,0xff,0x67,0x00,0x00,0x1e,0xef,0x04,0xfc,0x73,0x1c,0x53,
   0xcc,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82
};

// 24-bit uncompressed bottom-up BMP, 3x2. Rows are padded from 9 to 12 bytes,
// so the storage stride (12) != width*comp (9): the "misaligned" case.
static stbi_uc bmp3x2[54 + 2*12];

// 24-bit uncompressed top-down TGA, 3x2 (descriptor bit 5 set)
static stbi_uc tga3x2_topdown[18 + 3*2*3];

// 24-bit uncompressed bottom-up TGA, 3x2 (descriptor 0): negative stride view
static stbi_uc tga3x2_bottomup[18 + 3*2*3];

// 24-bit RLE TGA, 3x2: view must refuse, fallback must decode
static stbi_uc tga3x2_rle[18 + 8*3];

static void put16le(stbi_uc *p, unsigned v)
{
   p[0] = (stbi_uc) (v & 0xff);
   p[1] = (stbi_uc) ((v >> 8) & 0xff);
}

static void put32le(stbi_uc *p, unsigned v)
{
   put16le(p, v & 0xffff);
   put16le(p + 2, (v >> 16) & 0xffff);
}

// builds the tiny BMP; pixel (x,y) in display coordinates gets color (r,g,b)
static void make_bmp(void)
{
   int y, x;
   stbi_uc *row;
   memset(bmp3x2, 0, sizeof(bmp3x2));
   bmp3x2[0] = 'B';
   bmp3x2[1] = 'M';
   put32le(bmp3x2 + 2, (unsigned) sizeof(bmp3x2));
   put32le(bmp3x2 + 10, 54);
   put32le(bmp3x2 + 14, 40);
   put32le(bmp3x2 + 18, 3);
   put32le(bmp3x2 + 22, 2);          // positive height: rows stored bottom-up
   put16le(bmp3x2 + 26, 1);
   put16le(bmp3x2 + 28, 24);
   put32le(bmp3x2 + 34, sizeof(bmp3x2) - 54);

   for (y = 0; y < 2; ++y) {
      // stored row 0 is display row 1
      row = bmp3x2 + 54 + y*12;
      for (x = 0; x < 3; ++x) {
         row[x*3 + 0] = (stbi_uc) (10 + y*20 + x);   // B
         row[x*3 + 1] = (stbi_uc) (100 + y*20 + x);  // G
         row[x*3 + 2] = (stbi_uc) (200 + y*20 + x);  // R
      }
      // bytes 9..11 stay 0: row padding
   }
}

static void make_tga(stbi_uc *out, int top_down)
{
   int y, x;
   memset(out, 0, 18 + 18);
   out[2] = 2;                        // uncompressed true-color
   put16le(out + 12, 3);
   put16le(out + 14, 2);
   out[16] = 24;
   out[17] = (stbi_uc) (top_down ? 0x20 : 0x00);
   for (y = 0; y < 2; ++y) {
      int display_y = top_down ? y : 1 - y; // stored row y shows display row
      stbi_uc *row = out + 18 + y*9;
      for (x = 0; x < 3; ++x) {
         row[x*3 + 0] = (stbi_uc) (30 + display_y*10 + x);  // B
         row[x*3 + 1] = (stbi_uc) (60 + display_y*10 + x);  // G
         row[x*3 + 2] = (stbi_uc) (90 + display_y*10 + x);  // R
      }
   }
}

static void make_tga_rle(void)
{
   // 3x2, one raw packet of 3 pixels per row, tagged as raw (0x80 | (n-1))
   int y, x, o = 18;
   memset(tga3x2_rle, 0, sizeof(tga3x2_rle));
   tga3x2_rle[2] = 10;                // RLE true-color
   put16le(tga3x2_rle + 12, 3);
   put16le(tga3x2_rle + 14, 2);
   tga3x2_rle[16] = 24;
   for (y = 0; y < 2; ++y) {
      tga3x2_rle[o++] = 0x82;         // raw packet, 3 pixels
      for (x = 0; x < 3; ++x) {
         tga3x2_rle[o++] = (stbi_uc) (y*10 + x);
         tga3x2_rle[o++] = (stbi_uc) (y*10 + x + 50);
         tga3x2_rle[o++] = (stbi_uc) (y*10 + x + 100);
      }
   }
}

// ---- helpers -------------------------------------------------------------

// Display-order byte of channel c of pixel (x,y), mapping BGR views to RGB.
static unsigned view_channel(const stbi_view *v, int x, int y, int c)
{
   const stbi_uc *p = v->pixels + (ptrdiff_t) y * v->stride + (ptrdiff_t) x * v->comp;
   if (v->channel_order == STBI_view_order_bgr && v->comp >= 3 && c < 3)
      c = 2 - c;
   return p[c];
}

// Compares every pixel/channel of a view against an ordinary decode.
static int view_matches_decode(const char *name, const stbi_view *v,
                               stbi_uc const *buf, int len, int req_comp)
{
   int x, y, n = 0, dx = 0, dy = 0;
   stbi_uc *ref = stbi_load_from_memory(buf, len, &dx, &dy, &n, req_comp);
   int same = 1;
   if (!ref) {
      printf("FAIL %s: reference decode failed: %s\n", name, stbi_failure_reason());
      ++g_failures;
      return 0;
   }
   if (v->x != dx || v->y != dy) {
      printf("FAIL %s: size %dx%d vs decode %dx%d\n", name, v->x, v->y, dx, dy);
      same = 0;
   }
   if (v->comp != (req_comp ? req_comp : n)) {
      printf("FAIL %s: comp %d vs decode comp %d\n", name, v->comp, req_comp ? req_comp : n);
      same = 0;
   }
   for (y = 0; same && y < dy; ++y)
      for (x = 0; same && x < dx; ++x) {
         int c;
         for (c = 0; c < v->comp; ++c) {
            unsigned a = view_channel(v, x, y, c);
            unsigned b = ref[(y*dx + x)*v->comp + c];
            if (a != b) {
               printf("FAIL %s: pixel (%d,%d) ch %d: view %u decode %u\n",
                      name, x, y, c, a, b);
               same = 0;
               break;
            }
         }
      }
   ++g_checks;
   if (!same) ++g_failures;
   stbi_image_free(ref);
   return same;
}

// Checks that a view does not own its pixels and that they alias the input.
static void check_aliasing(const char *name, const stbi_view *v,
                           stbi_uc const *buf, int len)
{
   const stbi_uc *end = buf + len;
   int inside;
   if (v->pixels == NULL) {
      CHECK(0, "%s: no pixels to alias", name);
      return;
   }
   inside = v->pixels >= buf && v->pixels < end;
   CHECK(inside, "%s: view pixels %p outside input [%p,%p)", name,
         (const void *) v->pixels, (const void *) buf, (const void *) end);
   // the last displayed byte must also be inside the caller's buffer
   if (inside) {
      const stbi_uc *last = v->pixels
         + (ptrdiff_t) (v->y - 1) * v->stride
         + (ptrdiff_t) (v->x - 1) * v->comp
         + (v->comp - 1);
      CHECK(last >= buf && last < end, "%s: last pixel %p outside input", name,
            (const void *) last);
   }
}

// Frees a fallback result; views must never be passed to stbi_image_free.
static void free_if_copied(stbi_view *v)
{
   if (v->copied && v->pixels)
      stbi_image_free((void *) v->pixels);
   v->pixels = NULL;
}

// ---- the four input classes ---------------------------------------------

static void test_viewable(void)
{
   stbi_view v;
   int x, y, n;

   printf("== 1. viewable formats (no copy) ==\n");

   v = stbi_view_from_memory(pgm3x2, (int) sizeof(pgm3x2), 0);
   CHECK(v.copied == 0, "PGM: copied=%d (%s)", v.copied, v.reason ? v.reason : "-");
   CHECK(v.reason == NULL, "PGM: reason should be NULL");
   CHECK(v.type == STBI_type_pnm, "PGM: type=%d", v.type);
   CHECK(v.x == 3 && v.y == 2 && v.comp == 1, "PGM: %dx%dx%d", v.x, v.y, v.comp);
   CHECK(v.stride == 3, "PGM: stride=%d", v.stride);
   CHECK(v.channel_order == STBI_view_order_rgb, "PGM: order=%d", v.channel_order);
   CHECK(v.bits_per_channel == 8, "PGM: bpc=%d", v.bits_per_channel);
   check_aliasing("PGM", &v, pgm3x2, (int) sizeof(pgm3x2));
   view_matches_decode("PGM", &v, pgm3x2, (int) sizeof(pgm3x2), 1);
   printf("   PGM 3x2: pixels=%p comp=1 stride=%d order=rgb copied=0\n",
          (const void *) v.pixels, v.stride);

   v = stbi_view_from_memory(ppm3x2, (int) sizeof(ppm3x2), 0);
   CHECK(v.copied == 0, "PPM: copied=%d (%s)", v.copied, v.reason ? v.reason : "-");
   CHECK(v.x == 3 && v.y == 2 && v.comp == 3, "PPM: %dx%dx%d", v.x, v.y, v.comp);
   CHECK(v.stride == 9, "PPM: stride=%d", v.stride);
   CHECK(v.channel_order == STBI_view_order_rgb, "PPM: order=%d", v.channel_order);
   check_aliasing("PPM", &v, ppm3x2, (int) sizeof(ppm3x2));
   view_matches_decode("PPM", &v, ppm3x2, (int) sizeof(ppm3x2), 3);
   printf("   PPM 3x2: pixels=%p comp=3 stride=%d order=rgb copied=0\n",
          (const void *) v.pixels, v.stride);

   v = stbi_view_from_memory(tga3x2_topdown, (int) sizeof(tga3x2_topdown), 0);
   CHECK(v.copied == 0, "TGA top-down: copied=%d (%s)", v.copied,
         v.reason ? v.reason : "-");
   CHECK(v.type == STBI_type_tga, "TGA top-down: type=%d", v.type);
   CHECK(v.x == 3 && v.y == 2 && v.comp == 3, "TGA top-down: %dx%dx%d",
         v.x, v.y, v.comp);
   CHECK(v.stride == 9, "TGA top-down: stride=%d", v.stride);
   CHECK(v.channel_order == STBI_view_order_bgr, "TGA top-down: order=%d",
         v.channel_order);
   check_aliasing("TGA top-down", &v, tga3x2_topdown, (int) sizeof(tga3x2_topdown));
   view_matches_decode("TGA top-down", &v, tga3x2_topdown,
                       (int) sizeof(tga3x2_topdown), 3);
   printf("   TGA 3x2 top-down: pixels=%p comp=3 stride=%d order=bgr copied=0\n",
          (const void *) v.pixels, v.stride);

   // a channel conversion request cannot be a view
   v = stbi_view_from_memory(ppm3x2, (int) sizeof(ppm3x2), 4);
   CHECK(v.copied == 1, "PPM req 4ch: copied=%d", v.copied);
   CHECK(v.reason != NULL, "PPM req 4ch: reason missing");
   CHECK(v.comp == 4, "PPM req 4ch: comp=%d", v.comp);
   view_matches_decode("PPM req 4ch", &v, ppm3x2, (int) sizeof(ppm3x2), 4);
   printf("   PPM 3x2 req 4ch: copied=1 reason=\"%s\"\n", v.reason);
   free_if_copied(&v);

   // decoded-fallback pixels are a normal stbi_load allocation
   (void) x; (void) y; (void) n;
}

static void test_unsupported(void)
{
   stbi_view v;

   printf("== 2. unsupported formats (decode fallback, reason given) ==\n");

   v = stbi_view_from_memory(png2x2, (int) sizeof(png2x2), 0);
   CHECK(v.copied == 1, "PNG: copied=%d", v.copied);
   CHECK(v.type == STBI_type_png, "PNG: type=%d", v.type);
   CHECK(v.reason != NULL, "PNG: no reason");
   CHECK(v.pixels != NULL, "PNG: fallback failed: %s", stbi_failure_reason());
   view_matches_decode("PNG", &v, png2x2, (int) sizeof(png2x2), 0);
   printf("   PNG: copied=1 reason=\"%s\" (%dx%dx%d)\n", v.reason, v.x, v.y, v.comp);
   free_if_copied(&v);

   v = stbi_view_from_memory(tga3x2_rle, (int) sizeof(tga3x2_rle), 0);
   CHECK(v.copied == 1, "TGA RLE: copied=%d", v.copied);
   CHECK(v.type == STBI_type_tga, "TGA RLE: type=%d", v.type);
   CHECK(v.reason != NULL, "TGA RLE: no reason");
   view_matches_decode("TGA RLE", &v, tga3x2_rle, (int) sizeof(tga3x2_rle), 0);
   printf("   TGA RLE: copied=1 reason=\"%s\"\n", v.reason);
   free_if_copied(&v);

   v = stbi_view_from_memory(pgma_3x2, (int) sizeof(pgma_3x2), 0);
   CHECK(v.copied == 1, "ASCII PGM: copied=%d", v.copied);
   CHECK(v.reason != NULL, "ASCII PGM: no reason");
   view_matches_decode("ASCII PGM", &v, pgma_3x2, (int) sizeof(pgma_3x2), 0);
   printf("   ASCII PGM: copied=1 reason=\"%s\"\n", v.reason);
   free_if_copied(&v);

   v = stbi_view_from_memory(pgm16_3x2, (int) sizeof(pgm16_3x2), 0);
   CHECK(v.copied == 1, "16-bit PGM: copied=%d", v.copied);
   CHECK(v.bits_per_channel == 8, "16-bit PGM: bpc=%d", v.bits_per_channel);
   CHECK(v.reason != NULL, "16-bit PGM: no reason");
   view_matches_decode("16-bit PGM", &v, pgm16_3x2, (int) sizeof(pgm16_3x2), 0);
   printf("   16-bit PGM: copied=1 reason=\"%s\"\n", v.reason);
   free_if_copied(&v);

   v = stbi_view_from_memory((const stbi_uc *) "not an image", 12, 0);
   CHECK(v.copied == 1, "garbage: copied=%d", v.copied);
   CHECK(v.pixels == NULL, "garbage: unexpectedly decoded");
   CHECK(v.reason != NULL, "garbage: no reason");
   printf("   garbage: copied=1 pixels=NULL reason=\"%s\"\n", v.reason);
}

static void test_unaligned_stride(void)
{
   stbi_view v;

   printf("== 3. row stride != width*channels ==\n");

   v = stbi_view_from_memory(bmp3x2, (int) sizeof(bmp3x2), 0);
   CHECK(v.copied == 0, "BMP: copied=%d (%s)", v.copied, v.reason ? v.reason : "-");
   CHECK(v.type == STBI_type_bmp, "BMP: type=%d", v.type);
   CHECK(v.x == 3 && v.y == 2 && v.comp == 3, "BMP: %dx%dx%d", v.x, v.y, v.comp);
   CHECK(v.stride == -12, "BMP: stride=%d (expected -12: bottom-up, 9 bytes + 3 pad)", v.stride);
   CHECK(v.stride != -v.x * v.comp, "BMP: stride unexpectedly tight");
   CHECK(v.channel_order == STBI_view_order_bgr, "BMP: order=%d", v.channel_order);
   CHECK(v.pixels == bmp3x2 + 54 + 12, "BMP: first display row should be stored last");
   check_aliasing("BMP", &v, bmp3x2, (int) sizeof(bmp3x2));
   view_matches_decode("BMP", &v, bmp3x2, (int) sizeof(bmp3x2), 3);
   printf("   BMP 3x2 bottom-up+padded: pixels=%p comp=3 stride=%d order=bgr copied=0\n",
          (const void *) v.pixels, v.stride);

   v = stbi_view_from_memory(tga3x2_bottomup, (int) sizeof(tga3x2_bottomup), 0);
   CHECK(v.copied == 0, "TGA bottom-up: copied=%d (%s)", v.copied,
         v.reason ? v.reason : "-");
   CHECK(v.x == 3 && v.y == 2 && v.comp == 3, "TGA bottom-up: %dx%dx%d",
         v.x, v.y, v.comp);
   CHECK(v.stride == -9, "TGA bottom-up: stride=%d (expected -9)", v.stride);
   CHECK(v.pixels == tga3x2_bottomup + 18 + 9, "TGA bottom-up: wrong first row");
   check_aliasing("TGA bottom-up", &v, tga3x2_bottomup,
                  (int) sizeof(tga3x2_bottomup));
   view_matches_decode("TGA bottom-up", &v, tga3x2_bottomup,
                       (int) sizeof(tga3x2_bottomup), 3);
   printf("   TGA 3x2 bottom-up: pixels=%p comp=3 stride=%d order=bgr copied=0\n",
          (const void *) v.pixels, v.stride);

   // flipping on load reverses which stored row is the top row
   stbi_set_flip_vertically_on_load(1);
   v = stbi_view_from_memory(tga3x2_topdown, (int) sizeof(tga3x2_topdown), 0);
   CHECK(v.copied == 0, "TGA flipped: copied=%d", v.copied);
   CHECK(v.stride == -9, "TGA flipped: stride=%d (expected -9)", v.stride);
   check_aliasing("TGA flipped", &v, tga3x2_topdown, (int) sizeof(tga3x2_topdown));
   view_matches_decode("TGA flipped", &v, tga3x2_topdown,
                       (int) sizeof(tga3x2_topdown), 3);
   printf("   TGA 3x2 top-down with flip: stride=%d copied=0\n", v.stride);
   stbi_set_flip_vertically_on_load(0);

   // Truncating the input so the final padded row is incomplete must not
   // produce an out-of-bounds view. Decoding tolerates the missing bytes, so
   // the fallback succeeds and owns allocated pixels.
   v = stbi_view_from_memory(bmp3x2, 54 + 12 + 6, 0);
   CHECK(v.copied == 1, "BMP truncated: copied=%d", v.copied);
   CHECK(v.reason != NULL && strstr(v.reason, "truncated") != NULL,
         "BMP truncated: reason=\"%s\"", v.reason ? v.reason : "-");
   CHECK(v.pixels != NULL && v.x == 3 && v.y == 2 && v.comp == 3,
         "BMP truncated: fallback decode %dx%dx%d", v.x, v.y, v.comp);
   printf("   BMP truncated: copied=1 reason=\"%s\"\n", v.reason);
   free_if_copied(&v);
}

static void test_read_only_buffer(void)
{
#ifdef HAVE_MMAP
   long page = sysconf(_SC_PAGESIZE);
   stbi_uc *ro;
   stbi_view v;
   int ok = 1;

   printf("== 4. read-only input buffer ==\n");

   ro = (stbi_uc *) mmap(NULL, (size_t) page, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
   if (ro == MAP_FAILED) {
      printf("   mmap unavailable, skipping\n");
      return;
   }
   memset(ro, 0xAA, (size_t) page);
   memcpy(ro, bmp3x2, sizeof(bmp3x2));
   memcpy(ro + 1024, pgm3x2, sizeof(pgm3x2));
   ok &= mprotect(ro, (size_t) page, PROT_READ) == 0;

   v = stbi_view_from_memory(ro, (int) sizeof(bmp3x2), 0);
   CHECK(ok, "mprotect failed");
   CHECK(v.copied == 0, "read-only BMP: copied=%d (%s)", v.copied,
         v.reason ? v.reason : "-");
   CHECK(v.reason == NULL, "read-only BMP: reason set");
   CHECK(v.x == 3 && v.y == 2 && v.comp == 3, "read-only BMP: %dx%dx%d",
         v.x, v.y, v.comp);
   CHECK(v.stride == -12, "read-only BMP: stride=%d", v.stride);
   view_matches_decode("read-only BMP", &v, ro, (int) sizeof(bmp3x2), 3);
   printf("   read-only BMP: copied=0 stride=%d (no write, no fault)\n", v.stride);

   v = stbi_view_from_memory(ro + 1024, (int) sizeof(pgm3x2), 0);
   CHECK(v.copied == 0, "read-only PGM: copied=%d", v.copied);
   CHECK(v.x == 3 && v.y == 2 && v.comp == 1, "read-only PGM: %dx%dx%d",
         v.x, v.y, v.comp);
   view_matches_decode("read-only PGM", &v, ro + 1024, (int) sizeof(pgm3x2), 1);
   printf("   read-only PGM: copied=0 comp=1 stride=%d\n", v.stride);

   munmap(ro, (size_t) page);
#else
   printf("== 4. read-only input buffer ==\n   no mmap on this platform, skipping\n");
#endif
}

static void test_lifetime(void)
{
   stbi_uc *buf;
   stbi_view v;
   int len = (int) sizeof(ppm3x2) - 1; // drop the string literal's NUL

   printf("== 5. buffer lifetime / ownership ==\n");
   buf = (stbi_uc *) malloc((size_t) len);
   memcpy(buf, ppm3x2, (size_t) len);

   v = stbi_view_from_memory(buf, len, 0);
   CHECK(v.copied == 0, "lifetime: copied=%d", v.copied);
   CHECK(v.pixels == buf + (len - 18), "lifetime: pixel pointer not into buffer");
   ++g_checks;
   if (buf[0] != 'P') ++g_failures;

   // a view aliases the caller's memory: changing the buffer changes the view
   buf[len - 1] = 0x77;
   CHECK(view_channel(&v, 2, 1, 2) == 0x77, "lifetime: view did not alias buffer");

   // the view must not be freed with stbi_image_free; the buffer owns it
   printf("   view pixels alias caller buffer (%p, offset %d); "
          "free the buffer, not the view\n",
          (const void *) v.pixels, (int) (v.pixels - buf));
   free(buf);
}

int main(void)
{
   make_bmp();
   make_tga(tga3x2_topdown, 1);
   make_tga(tga3x2_bottomup, 0);
   make_tga_rle();

   test_viewable();
   test_unsupported();
   test_unaligned_stride();
   test_read_only_buffer();
   test_lifetime();

   printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
   return g_failures ? 1 : 0;
}
