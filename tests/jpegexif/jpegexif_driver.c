#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image_write.h"
#include "stb_image.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define SMALL 64
#define MAXP 65527
#define OVER (MAXP + 1)
static unsigned char px[6*5*3], exif_small[SMALL], exif_near[MAXP], exif_over[OVER];
typedef struct { unsigned char *b; int n, cap; } Buf;
static void sink(void *ctx, void *data, int size) {
   Buf *b = (Buf*)ctx;
   if (b->n + size > b->cap) { b->cap = (b->n + size) * 2; b->b = realloc(b->b, (size_t)b->cap); }
   memcpy(b->b + b->n, data, (size_t)size); b->n += size;
}
static void save(const char *path, const Buf *b) {
   FILE *f = fopen(path, "wb");
   if (f) { fwrite(b->b, 1, (size_t)b->n, f); fclose(f); }
}
static int find_app1(const unsigned char *b, int n, int *olen) {
   int p = 2, len;
   while (p + 4 <= n && b[p] == 0xFF && b[p+1] == 0xE1) {
      len = (b[p+2] << 8) | b[p+3];
      if (len >= 8 && p + 2 + len <= n && !memcmp(b + p + 4, "Exif\0\0", 6)) { *olen = len - 8; return p + 10; }
      p += 2 + len;
   }
   return -1;
}
static void case_run(const char *name, const unsigned char *exif, int elen) {
   Buf jpg = {0};
   int rc = stbi_write_jpg_strided_to_func_with_exif(sink, &jpg, 6, 5, 3, px, 0, 90, exif, elen);
   int olen = 0, off = find_app1(jpg.b, jpg.n, &olen), w, h, c, has = off >= 0;
   unsigned char *img = stbi_load_from_memory(jpg.b, jpg.n, &w, &h, &c, 3);
   int match = rc && has == (elen > 0) && olen == elen && (elen == 0 || !memcmp(jpg.b + off, exif, (size_t)elen));
   printf("%-15s segment=%-3s written=%-5d readback=%-5d bytes_match=%-3s stb_load=%s\n",
          name, has ? "yes" : "no", elen, olen, match ? "yes" : "no", img ? "ok" : "fail");
   if (img) stbi_image_free(img);
   char path[256]; snprintf(path, sizeof path, "output/%s.jpg", name); save(path, &jpg);
   free(jpg.b);
}
int main(void) {
   int i;
   memset(exif_small, 'A', SMALL);
   memcpy(exif_small, "II*\0\x08\0\0\0\0\0", 10); // TIFF header, zero IFDs
   memcpy(exif_near, exif_small, SMALL);
   for (i = SMALL; i < MAXP; i++) exif_near[i] = (unsigned char)(i * 31 + 7);
   memcpy(exif_over, exif_near, MAXP); exif_over[MAXP] = 0x5A;
   case_run("small_exif", exif_small, SMALL);
   case_run("near_limit_exif", exif_near, MAXP);
   case_run("empty_exif", exif_small, 0);
   case_run("no_exif", NULL, 0);
   Buf over = {0};
   int rc = stbi_write_jpg_strided_to_func_with_exif(sink, &over, 6, 5, 3, px, 0, 90, exif_over, OVER);
   printf("%-15s over_limit_write=%s (expect no)\n", "over_limit", rc ? "yes" : "no");
   free(over.b);
   Buf base = {0}, base_exif = {0};
   stbi_write_jpg_to_func(sink, &base, 6, 5, 3, px, 90);
   stbi_write_jpg_strided_to_func_with_exif(sink, &base_exif, 6, 5, 3, px, 0, 90, NULL, 0);
   printf("%-15s identical=%s\n", "no_exif_compat",
          base.n == base_exif.n && !memcmp(base.b, base_exif.b, (size_t)base.n) ? "yes" : "no");
   save("output/baseline.jpg", &base); save("output/baseline_exif.jpg", &base_exif);
   free(base.b); free(base_exif.b);
   int frc = stbi_write_jpg_strided_with_exif("output/file_api.jpg", 6, 5, 3, px, 0, 90, exif_small, SMALL);
   int fbad = stbi_write_jpg_strided_with_exif("output/file_api_bad.jpg", 6, 5, 3, px, 0, 90, exif_over, OVER);
   printf("%-15s file_api_write=%s file_api_over_limit=%s\n", "file_entry",
          frc ? "ok" : "fail", fbad ? "yes" : "no(expect)");
   return 0;
}
