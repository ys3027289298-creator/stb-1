// test_png16_write.c -- roundtrip tests for stbi_write_png16 (16-bit PNG write)
//
// Four cases: greyscale gradient, RGB, RGBA semi-transparent, and
// zero-dimension/mismatched-stride error handling. Each written file is
// parsed back (IHDR + inflated IDAT) and re-loaded with stbi_load_16;
// written vs. read-back sample values are compared and the max error and
// file byte count are reported.

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, ...) do { \
   if (!(cond)) { \
      printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); \
      ++failures; \
   } \
} while (0)

static unsigned int rd_be32(const unsigned char *p)
{
   return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) | ((unsigned int)p[2] << 8) | p[3];
}

// parse IHDR and inflate IDAT of a PNG file; verifies bit depth, color type
// and that the raw scanline data is exactly (w*n*2+1)*h bytes, i.e. every
// row holds w*n 16-bit samples plus its filter byte
static void verify_png16_file(const char *path, int expect_w, int expect_h, int expect_ctype, long *file_size)
{
   FILE *f = fopen(path, "rb");
   unsigned char *data;
   long size;
   size_t pos;
   int found_ihdr = 0, found_idat = 0;

   CHECK(f != NULL, "%s: cannot open written file", path);
   if (!f) return;
   fseek(f, 0, SEEK_END);
   size = ftell(f);
   rewind(f);
   *file_size = size;
   data = (unsigned char *) malloc((size_t) size);
   CHECK(data != NULL && fread(data, 1, (size_t) size, f) == (size_t) size, "%s: read failed", path);
   fclose(f);
   if (!data) return;

   CHECK(size >= 8 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0, "%s: bad PNG signature", path);

   pos = 8;
   while (pos + 12 <= (size_t) size) {
      unsigned int len = rd_be32(data + pos);
      const unsigned char *type = data + pos + 4;
      const unsigned char *payload = data + pos + 8;
      CHECK(pos + 12 + len <= (size_t) size, "%s: chunk overruns file", path);
      if (pos + 12 + len > (size_t) size) break;

      if (memcmp(type, "IHDR", 4) == 0) {
         unsigned int w = rd_be32(payload), h = rd_be32(payload + 4);
         found_ihdr = 1;
         CHECK((int) w == expect_w && (int) h == expect_h, "%s: IHDR %ux%u, expected %dx%d", path, w, h, expect_w, expect_h);
         CHECK(payload[8] == 16, "%s: bit depth %d, expected 16", path, payload[8]);
         CHECK(payload[9] == expect_ctype, "%s: color type %d, expected %d", path, payload[9], expect_ctype);
         CHECK(payload[10] == 0 && payload[11] == 0 && payload[12] == 0, "%s: unexpected compression/filter/interlace", path);
      } else if (memcmp(type, "IDAT", 4) == 0) {
         int rawlen = 0;
         char *raw = stbi_zlib_decode_malloc_guesssize_headerflag((const char *) payload, (int) len, 16384, &rawlen, 1);
         int channels = (expect_ctype == 0) ? 1 : (expect_ctype == 4) ? 2 : (expect_ctype == 2) ? 3 : 4;
         long expect_raw = (long) (expect_w * channels * 2 + 1) * expect_h;
         found_idat = 1;
         CHECK(raw != NULL, "%s: IDAT inflate failed", path);
         if (raw) {
            CHECK(rawlen == expect_raw, "%s: raw scanline data %d bytes, expected %ld (%d bytes/row + filter byte)",
                  path, rawlen, expect_raw, expect_w * channels * 2);
            free(raw);
         }
      }
      pos += 12 + len;
   }
   CHECK(found_ihdr, "%s: no IHDR chunk", path);
   CHECK(found_idat, "%s: no IDAT chunk", path);
   free(data);
}

// write img w*h*n 16-bit samples, read back with stbi_load_16, compare every sample
static void roundtrip_case(const char *name, const char *path, int w, int h, int n, int ctype, const unsigned short *img)
{
   long file_size = 0;
   int rw, rh, rc;
   unsigned short *back;
   long i, total = (long) w * h * n;
   long max_err = 0;
   int ok;

   ok = stbi_write_png16(path, w, h, n, img, 0);
   CHECK(ok, "%s: stbi_write_png16 returned failure", name);
   if (!ok) return;

   verify_png16_file(path, w, h, ctype, &file_size);

   back = stbi_load_16(path, &rw, &rh, &rc, 0);
   CHECK(back != NULL, "%s: stbi_load_16 could not read the file back", name);
   if (!back) return;
   CHECK(rw == w && rh == h && rc == n, "%s: read back %dx%dx%d, expected %dx%dx%d", name, rw, rh, rc, w, h, n);

   for (i = 0; i < total; ++i) {
      long err = (long) back[i] - (long) img[i];
      if (err < 0) err = -err;
      if (err > max_err) max_err = err;
   }
   CHECK(max_err == 0, "%s: max sample error %ld, expected 0", name, max_err);
   printf("%-28s file=%7ld bytes  samples=%6ld  max_err=%ld  %s\n",
          name, file_size, total, max_err, max_err == 0 ? "PASS" : "FAIL");
   stbi_image_free(back);
}

int main(void)
{
   int x, y;

   // case 1: greyscale gradient, full 0..65535 range
   {
      int w = 64, h = 48;
      unsigned short *img = (unsigned short *) malloc((size_t) w * h * sizeof(unsigned short));
      for (y = 0; y < h; ++y)
         for (x = 0; x < w; ++x)
            img[y * w + x] = (unsigned short) (((y * w + x) * 65535) / (w * h - 1));
      roundtrip_case("grey gradient 64x48", "output/png16_gray.png", w, h, 1, 0, img);
      free(img);
   }

   // case 2: RGB
   {
      int w = 32, h = 32;
      unsigned short *img = (unsigned short *) malloc((size_t) w * h * 3 * sizeof(unsigned short));
      for (y = 0; y < h; ++y)
         for (x = 0; x < w; ++x) {
            img[(y * w + x) * 3 + 0] = (unsigned short) (x * 65535 / (w - 1));
            img[(y * w + x) * 3 + 1] = (unsigned short) (y * 65535 / (h - 1));
            img[(y * w + x) * 3 + 2] = (unsigned short) ((x + y) * 65535 / (w + h - 2));
         }
      roundtrip_case("RGB 32x32", "output/png16_rgb.png", w, h, 3, 2, img);
      free(img);
   }

   // case 3: RGBA, semi-transparent (alpha around half range)
   {
      int w = 16, h = 16;
      unsigned short *img = (unsigned short *) malloc((size_t) w * h * 4 * sizeof(unsigned short));
      for (y = 0; y < h; ++y)
         for (x = 0; x < w; ++x) {
            img[(y * w + x) * 4 + 0] = (unsigned short) (x * 4096);
            img[(y * w + x) * 4 + 1] = (unsigned short) (y * 4096);
            img[(y * w + x) * 4 + 2] = (unsigned short) ((x ^ y) * 4096);
            img[(y * w + x) * 4 + 3] = (unsigned short) (16384 + (x + y) * 512); // ~25%..~50% alpha
         }
      roundtrip_case("RGBA semi-transparent 16x16", "output/png16_rgba.png", w, h, 4, 6, img);
      free(img);
   }

   // case 4: errors -- zero dimensions and stride/data-length mismatch must
   // fail without creating a file
   {
      unsigned short img[4] = { 1, 2, 3, 4 };
      const char *path = "output/png16_must_not_exist.png";
      int ok;
      remove(path);

      ok = stbi_write_png16(path, 0, 8, 1, img, 0);
      CHECK(ok == 0, "zero width: expected failure, got %d", ok);
      CHECK(fopen(path, "rb") == NULL, "zero width: file was created");

      ok = stbi_write_png16(path, 8, 0, 1, img, 0);
      CHECK(ok == 0, "zero height: expected failure, got %d", ok);
      CHECK(fopen(path, "rb") == NULL, "zero height: file was created");

      // stride smaller than one row: buffer can't hold w*h*n samples
      ok = stbi_write_png16(path, 2, 2, 1, img, 3);
      CHECK(ok == 0, "stride/data-length mismatch: expected failure, got %d", ok);
      CHECK(fopen(path, "rb") == NULL, "stride mismatch: file was created");

      ok = stbi_write_png16(path, 2, 2, 1, NULL, 0);
      CHECK(ok == 0, "NULL data: expected failure, got %d", ok);
      CHECK(fopen(path, "rb") == NULL, "NULL data: file was created");

      printf("%-28s %s\n", "zero dims / bad stride", failures == 0 ? "PASS (all rejected, no file written)" : "FAIL");
   }

   if (failures) {
      printf("RESULT: %d failure(s)\n", failures);
      return 1;
   }
   printf("RESULT: all png16 write tests passed\n");
   return 0;
}
