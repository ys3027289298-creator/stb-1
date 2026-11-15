// KTX2 writer test for stb_image_write.
//
// Writes small KTX2 files, parses them back with a minimal spec-based
// reader, and compares every mip level byte-for-byte against the source
// data. Also checks that stbi_write_ktx2_to_func produces byte-identical
// output, and exercises the failure paths (bad level counts/dimensions).
//
// Build & run from the tests directory:
//    cc -I.. ktx2_write_test.c -lm -o ktx2_write_test && ./ktx2_write_test

#ifdef __clang__
#define STBIWDEF static inline
#endif
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int num_failures = 0;

static void check(int cond, const char *msg)
{
   if (!cond) {
      printf("FAIL: %s\n", msg);
      ++num_failures;
   }
}

static unsigned int rd32(const unsigned char *p)
{
   return (unsigned int) p[0] | ((unsigned int) p[1] << 8) |
          ((unsigned int) p[2] << 16) | ((unsigned int) p[3] << 24);
}

static unsigned long long rd64(const unsigned char *p)
{
   return rd32(p) | ((unsigned long long) rd32(p+4) << 32);
}

static int level_dim(int d, int level)
{
   d >>= level;
   return d < 1 ? 1 : d;
}

static size_t level_size(int w, int h, int comp, int level)
{
   return (size_t) level_dim(w, level) * level_dim(h, level) * comp;
}

static size_t total_size(int w, int h, int comp, int num_levels)
{
   size_t total = 0;
   int i;
   for (i = 0; i < num_levels; ++i)
      total += level_size(w, h, comp, i);
   return total;
}

// deterministic per-level pattern so every byte position is distinguishable
static void fill_pattern(unsigned char *dst, int w, int h, int comp, int num_levels)
{
   size_t pos = 0;
   int i;
   for (i = 0; i < num_levels; ++i) {
      size_t n = level_size(w, h, comp, i), j;
      for (j = 0; j < n; ++j)
         dst[pos + j] = (unsigned char) (j*31 + i*17 + comp*7 + 3);
      pos += n;
   }
}

static unsigned char *load_file(const char *filename, size_t *out_size)
{
   FILE *f = fopen(filename, "rb");
   unsigned char *buf;
   long size;
   if (!f) return NULL;
   fseek(f, 0, SEEK_END);
   size = ftell(f);
   fseek(f, 0, SEEK_SET);
   buf = (unsigned char *) malloc((size_t) size);
   if (buf && fread(buf, 1, (size_t) size, f) != (size_t) size) {
      free(buf);
      buf = NULL;
   }
   fclose(f);
   *out_size = buf ? (size_t) size : 0;
   return buf;
}

// Parse a KTX2 file and verify structure + per-level data against 'expected'.
static int readback_and_compare(const unsigned char *buf, size_t size,
                                int w, int h, int comp, int num_levels,
                                const unsigned char *expected, const char *tag)
{
   static const unsigned char ident[12] = { 0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A };
   static const unsigned int vkformat[5] = { 0, 9, 16, 23, 37 };
   unsigned int dfd_off, dfd_len, align, hdr_ok = 1, struct_ok = 1, data_ok = 1;
   unsigned long long data_end = 0;
   size_t src = 0;
   int i;

   if (size < 80 + 24*(size_t) num_levels) {
      check(0, tag);
      printf("  file too small\n");
      return 0;
   }

   // header
   hdr_ok &= memcmp(buf, ident, 12) == 0;
   hdr_ok &= rd32(buf+12) == vkformat[comp];   // vkFormat
   hdr_ok &= rd32(buf+16) == 1;                // typeSize
   hdr_ok &= rd32(buf+20) == (unsigned int) w; // pixelWidth
   hdr_ok &= rd32(buf+24) == (unsigned int) h; // pixelHeight
   hdr_ok &= rd32(buf+28) == 0;                // pixelDepth
   hdr_ok &= rd32(buf+32) == 0;                // layerCount
   hdr_ok &= rd32(buf+36) == 1;                // faceCount
   hdr_ok &= rd32(buf+40) == (unsigned int) num_levels; // levelCount
   hdr_ok &= rd32(buf+44) == 0;                // supercompressionScheme
   dfd_off = rd32(buf+48);
   dfd_len = rd32(buf+52);
   hdr_ok &= dfd_off == 80 + 24*(unsigned int) num_levels;
   hdr_ok &= dfd_len == 28 + 16*(unsigned int) comp;
   hdr_ok &= rd32(buf+56) == 0 && rd32(buf+60) == 0; // key/value data
   hdr_ok &= rd64(buf+64) == 0 && rd64(buf+72) == 0; // supercompression global data
   check(hdr_ok, tag);
   if (!hdr_ok) {
      printf("  header mismatch\n");
      return 0;
   }

   // DFD basic block
   {
      const unsigned char *d = buf + dfd_off;
      struct_ok &= rd32(d+0) == dfd_len;            // totalSize
      struct_ok &= rd32(d+4) == 0;                  // vendorId + descriptorType
      struct_ok &= (d[8]  | (d[9]  << 8)) == 2;     // versionNumber
      struct_ok &= (d[10] | (d[11] << 8)) == 24 + 16*comp; // descriptorBlockSize
      struct_ok &= d[12] == 1 && d[13] == 1 && d[14] == 2 && d[15] == 0; // model/primaries/transfer/flags
      struct_ok &= d[20] == (unsigned char) comp;   // bytesPlane[0]
      for (i = 0; i < comp; ++i) {
         const unsigned char *sm = d + 28 + 16*i;
         struct_ok &= (sm[0] | (sm[1] << 8)) == 8*i;   // bitOffset
         struct_ok &= sm[2] == 7;                      // bitLength (8-1)
         struct_ok &= sm[3] == (i < 3 ? i : 15);       // channelType R/G/B/A
         struct_ok &= rd32(sm+8) == 0;                 // sampleLower
         struct_ok &= rd32(sm+12) == 0x3f800000;       // sampleUpper (1.0f)
      }
   }

   // level index + level data
   align = (comp == 3) ? 12 : 4;
   for (i = 0; i < num_levels; ++i) {
      const unsigned char *idx = buf + 80 + 24*i;
      unsigned long long off = rd64(idx), len = rd64(idx+8), ulen = rd64(idx+16);
      size_t expect_len = level_size(w, h, comp, i);
      struct_ok &= len == expect_len;
      struct_ok &= ulen == expect_len;
      struct_ok &= (off % align) == 0;
      struct_ok &= off >= 80 + 24*(unsigned long long) num_levels + dfd_len;
      struct_ok &= off + len <= size;
      if (num_levels > 1 && i < num_levels-1) {
         unsigned long long last_off = rd64(buf + 80 + 24*(num_levels-1));
         struct_ok &= last_off < off; // levels stored smallest-first
      }
      if (off + len > data_end)
         data_end = off + len;
      if (struct_ok && data_ok && memcmp(buf + off, expected + src, expect_len) != 0) {
         data_ok = 0;
         printf("  level %d data mismatch (%dx%d)\n", i,
                level_dim(w, i), level_dim(h, i));
      }
      src += expect_len;
   }
   struct_ok &= data_end == size; // no trailing garbage
   check(struct_ok, tag);
   if (!struct_ok)
      printf("  structure mismatch\n");
   check(data_ok, tag);
   return hdr_ok && struct_ok && data_ok;
}

typedef struct {
   unsigned char *data;
   size_t size, cap;
   int calls;
} mem_writer;

static void mem_write(void *context, void *data, int size)
{
   mem_writer *m = (mem_writer *) context;
   if (m->size + (size_t) size > m->cap) {
      m->cap = (m->size + (size_t) size) * 2;
      m->data = (unsigned char *) realloc(m->data, m->cap);
   }
   memcpy(m->data + m->size, data, (size_t) size);
   m->size += (size_t) size;
   ++m->calls;
}

static void test_roundtrip(const char *name, const char *filename,
                           int w, int h, int comp, int num_levels)
{
   size_t bytes = total_size(w, h, comp, num_levels);
   unsigned char *src = (unsigned char *) malloc(bytes);
   unsigned char *file_data;
   size_t file_size = 0;
   int ok;

   fill_pattern(src, w, h, comp, num_levels);
   ok = stbi_write_ktx2(filename, w, h, comp, src, num_levels);
   check(ok, name);
   file_data = load_file(filename, &file_size);
   check(file_data != NULL, name);
   if (file_data) {
      if (readback_and_compare(file_data, file_size, w, h, comp, num_levels, src, name))
         printf("OK: %s (%d level%s, %dx%d, comp=%d, %lu bytes)\n",
                name, num_levels, num_levels > 1 ? "s" : "", w, h, comp,
                (unsigned long) file_size);
   }

   // the to_func variant must produce byte-identical output
   {
      mem_writer m;
      memset(&m, 0, sizeof(m));
      ok = stbi_write_ktx2_to_func(mem_write, &m, w, h, comp, src, num_levels);
      check(ok, name);
      check(m.size == file_size, name);
      if (m.size == file_size && file_data != NULL)
         check(memcmp(m.data, file_data, file_size) == 0, name);
      free(m.data);
   }

   free(file_data);
   remove(filename);
   free(src);
}

static void test_failures(void)
{
   unsigned char buf[16*16*4];
   const char *filename = "ktx2_fail_test.ktx2";
   int bad = 0;

   memset(buf, 0, sizeof(buf));

   // each of these must fail (return 0)
   bad += stbi_write_ktx2(filename, 16, 8, 4, buf, 0);   // zero levels
   bad += stbi_write_ktx2(filename, 16, 8, 4, buf, -1);  // negative levels
   bad += stbi_write_ktx2(filename, 16, 8, 4, buf, 6);   // too many levels (max is 5)
   bad += stbi_write_ktx2(filename, 1, 1, 4, buf, 2);    // 1x1 has only one level
   bad += stbi_write_ktx2(filename, 0, 8, 4, buf, 1);    // zero width
   bad += stbi_write_ktx2(filename, 16, -8, 4, buf, 1);  // negative height
   bad += stbi_write_ktx2(filename, 16, 8, 0, buf, 1);   // zero components
   bad += stbi_write_ktx2(filename, 16, 8, 5, buf, 1);   // too many components
   bad += stbi_write_ktx2(filename, 16, 8, 4, NULL, 1);  // NULL data
   bad += stbi_write_ktx2("", 16, 8, 4, buf, 1);         // unwritable filename
   check(bad == 0, "invalid parameter rejection");

   // the to_func variant must also reject, without emitting a single byte
   {
      mem_writer m;
      memset(&m, 0, sizeof(m));
      bad = 0;
      bad += stbi_write_ktx2_to_func(mem_write, &m, 16, 8, 4, buf, 0);
      bad += stbi_write_ktx2_to_func(mem_write, &m, 16, 8, 4, buf, 6);
      bad += stbi_write_ktx2_to_func(mem_write, &m, -1, 8, 4, buf, 1);
      bad += stbi_write_ktx2_to_func(mem_write, &m, 16, 8, 4, NULL, 1);
      check(bad == 0, "invalid parameter rejection (to_func)");
      check(m.calls == 0 && m.size == 0, "no bytes emitted on failure");
      free(m.data);
   }

   remove(filename);
   if (num_failures == 0)
      printf("OK: failure paths (10 file-variant + 4 func-variant rejections)\n");
}

// stbi_flip_vertically_on_write must flip every mip level independently
static void test_flip(void)
{
   const char *filename = "ktx2_flip_test.ktx2";
   unsigned char src[4*2 + 2*1];           // 4x2 + 2x1, comp=1, 2 levels
   unsigned char flipped[4*2 + 2*1];
   unsigned char *file_data;
   size_t file_size = 0;
   int i, ok;

   for (i = 0; i < (int) sizeof(src); ++i)
      src[i] = (unsigned char) (i + 1);
   // level 0: rows swapped; level 1 (2x1): single row, unchanged
   memcpy(flipped + 0, src + 4, 4);
   memcpy(flipped + 4, src + 0, 4);
   memcpy(flipped + 8, src + 8, 2);

   stbi_flip_vertically_on_write(1);
   ok = stbi_write_ktx2(filename, 4, 2, 1, src, 2);
   stbi_flip_vertically_on_write(0);
   check(ok, "flip");
   file_data = load_file(filename, &file_size);
   check(file_data != NULL, "flip");
   if (file_data) {
      if (readback_and_compare(file_data, file_size, 4, 2, 1, 2, flipped, "flip"))
         printf("OK: flip (2 levels, 4x2, comp=1, rows flipped per level)\n");
      free(file_data);
   }
   remove(filename);
}

int main(void)
{
   test_roundtrip("1-level mip RGB 6x5",    "ktx2_test_1lvl.ktx2",  6,  5, 3, 1);
   test_roundtrip("4-level mip RGBA 16x8",  "ktx2_test_4lvl.ktx2",  16, 8, 4, 4);
   test_roundtrip("4-level mip RGB 16x16",  "ktx2_test_4lvl3.ktx2", 16, 16, 3, 4);
   test_flip();
   test_failures();

   if (num_failures) {
      printf("FAILED: %d check(s) failed\n", num_failures);
      return 1;
   }
   printf("PASSED: all KTX2 write/readback checks\n");
   return 0;
}
