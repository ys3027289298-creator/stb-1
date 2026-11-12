#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../stb_image_write.h"

#define STB_IMAGE_IMPLEMENTATION
#include "../stb_image.h"

#define OUTDIR "output"

typedef struct {
   const char *name;
   int w, h, comp;
} case_info;

static void write_sidecar(const char *path, const unsigned short *data, size_t count)
{
   FILE *f = fopen(path, "wb");
   if (!f) { perror(path); exit(2); }
   fwrite(data, sizeof(unsigned short), count, f);
   fclose(f);
}

/* Length-checked wrapper: proves the length/dimension mismatch guard before
 * any file is touched. Returns -1 when the caller-provided buffer is too short
 * for w*h*comp unsigned short samples; otherwise forwards to the new API. */
static int checked_write_png16(const char *path, int w, int h, int comp,
                               const unsigned short *data, size_t data_count,
                               size_t *shortfall)
{
   size_t need = (size_t) w * (size_t) h * (size_t) comp;
   if (data_count < need) {
      if (shortfall) *shortfall = need - data_count;
      return -1;
   }
   remove(path); /* make sure a failure never leaves a half/old file behind */
   return stbi_write_png16(path, w, h, comp, data, 0) ? 0 : -2;
}

static long file_bytes(const char *path)
{
   FILE *f = fopen(path, "rb");
   long n = -1;
   if (f) {
      fseek(f, 0, SEEK_END);
      n = ftell(f);
      fclose(f);
   }
   return n;
}

static void emit_manifest(FILE *mf, const char *name, int w, int h, int comp,
                          const char *png_path, const char *raw_path, int rc)
{
   fprintf(mf, "%s|%d|%d|%d|16|%s|%s|%d\n", name, w, h, comp, png_path, raw_path ? raw_path : "", rc);
}

static void case_gray_gradient(FILE *mf)
{
   int w = 256, h = 64, comp = 1;
   size_t count = (size_t) w * h;
   unsigned short *data = malloc(count * sizeof(unsigned short));
   char png[256], raw[256];
   int x, y;
   for (y = 0; y < h; ++y)
      for (x = 0; x < w; ++x)
         data[y*w+x] = (unsigned short) x; /* 0..255 keeps adjacent values distinct after 16-bit storage */
   snprintf(png, sizeof png, OUTDIR "/gray16_gradient.png");
   snprintf(raw, sizeof raw, OUTDIR "/gray16_gradient.raw");
   int rc = checked_write_png16(png, w, h, comp, data, count, NULL);
   long bytes = rc == 0 ? file_bytes(png) : -1;
   printf("[case] name=gray16_gradient w=%d h=%d bitdepth=16 comp=%d path=%s bytes=%ld rc=%d\n",
          w, h, comp, png, bytes, rc);
   write_sidecar(raw, data, count);
   emit_manifest(mf, "gray16_gradient", w, h, comp, png, raw, rc);
   free(data);
}

static void case_rgb16(FILE *mf)
{
   int w = 96, h = 48, comp = 3;
   size_t count = (size_t) w * h * comp;
   unsigned short *data = malloc(count * sizeof(unsigned short));
   char png[256], raw[256];
   int x, y, k = 0;
   for (y = 0; y < h; ++y) {
      for (x = 0; x < w; ++x) {
         data[k++] = (unsigned short) ((x * 65535u) / (w - 1));        /* R spans full 16-bit range */
         data[k++] = (unsigned short) ((y * 65535u) / (h - 1));        /* G spans full 16-bit range */
         data[k++] = (unsigned short) (x * 257u + y);                  /* B mixed, wraps naturally */
      }
   }
   snprintf(png, sizeof png, OUTDIR "/rgb16.png");
   snprintf(raw, sizeof raw, OUTDIR "/rgb16.raw");
   int rc = checked_write_png16(png, w, h, comp, data, count, NULL);
   long bytes = rc == 0 ? file_bytes(png) : -1;
   printf("[case] name=rgb16 w=%d h=%d bitdepth=16 comp=%d path=%s bytes=%ld rc=%d\n",
          w, h, comp, png, bytes, rc);
   write_sidecar(raw, data, count);
   emit_manifest(mf, "rgb16", w, h, comp, png, raw, rc);

   /* Extra: exercise a non-default byte stride (2 bytes padding per pixel row) via the raw API directly. */
   {
      ptrdiff_t stride = (ptrdiff_t) w * comp * (ptrdiff_t) sizeof(unsigned short) + 2;
      size_t padded = (size_t) stride * h;
      unsigned char *buf = calloc(1, padded);
      int yy;
      for (yy = 0; yy < h; ++yy)
         memcpy(buf + (size_t) yy * stride, data + (size_t) yy * w * comp,
                (size_t) w * comp * sizeof(unsigned short));
      int rc2 = stbi_write_png16(OUTDIR "/rgb16_strided.png", w, h, comp,
                                 (const unsigned short *) buf, stride);
      printf("[case] name=rgb16_strided w=%d h=%d bitdepth=16 comp=%d path=%s bytes=%ld rc=%d stride=%td\n",
             w, h, comp, OUTDIR "/rgb16_strided.png",
             rc2 ? file_bytes(OUTDIR "/rgb16_strided.png") : -1, rc2, stride);
      free(buf);
   }
   free(data);
}

static void case_rgba16(FILE *mf)
{
   int w = 64, h = 32, comp = 4;
   size_t count = (size_t) w * h * comp;
   unsigned short *data = malloc(count * sizeof(unsigned short));
   char png[256], raw[256];
   int x, y, k = 0;
   for (y = 0; y < h; ++y) {
      for (x = 0; x < w; ++x) {
         data[k++] = (unsigned short) (x * 1023u);                 /* R */
         data[k++] = (unsigned short) (65535u - y * 2048u);        /* G */
         data[k++] = (unsigned short) ((x ^ y) * 4097u);           /* B */
         data[k++] = (unsigned short) ((x * 65535u) / (w - 1));    /* A: full 16-bit alpha ramp incl. 32768 half-transparent */
      }
   }
   snprintf(png, sizeof png, OUTDIR "/rgba16.png");
   snprintf(raw, sizeof raw, OUTDIR "/rgba16.raw");
   int rc = checked_write_png16(png, w, h, comp, data, count, NULL);
   long bytes = rc == 0 ? file_bytes(png) : -1;
   printf("[case] name=rgba16 w=%d h=%d bitdepth=16 comp=%d path=%s bytes=%ld rc=%d\n",
          w, h, comp, png, bytes, rc);
   write_sidecar(raw, data, count);
   emit_manifest(mf, "rgba16", w, h, comp, png, raw, rc);
   free(data);
}

static void case_gray_alpha16(FILE *mf)
{
   int w = 48, h = 24, comp = 2;
   size_t count = (size_t) w * h * comp;
   unsigned short *data = malloc(count * sizeof(unsigned short));
   char png[256], raw[256];
   int x, y, k = 0;
   for (y = 0; y < h; ++y) {
      for (x = 0; x < w; ++x) {
         data[k++] = (unsigned short) ((y * 65535u) / (h - 1));    /* Y full range */
         data[k++] = (unsigned short) (32768u + x * 500u);         /* A around half-transparent, wraps */
      }
   }
   snprintf(png, sizeof png, OUTDIR "/gray_alpha16.png");
   snprintf(raw, sizeof raw, OUTDIR "/gray_alpha16.raw");
   int rc = checked_write_png16(png, w, h, comp, data, count, NULL);
   long bytes = rc == 0 ? file_bytes(png) : -1;
   printf("[case] name=gray_alpha16 w=%d h=%d bitdepth=16 comp=%d path=%s bytes=%ld rc=%d\n",
          w, h, comp, png, bytes, rc);
   write_sidecar(raw, data, count);
   emit_manifest(mf, "gray_alpha16", w, h, comp, png, raw, rc);
   free(data);
}

static void case_errors(FILE *mf)
{
   unsigned short tiny[4] = { 1, 2, 3, 4 };
   size_t shortfall = 0;

   /* zero width/height: library must reject without creating a file */
   remove(OUTDIR "/err_zero_wh.png");
   int rc_zero = stbi_write_png16(OUTDIR "/err_zero_wh.png", 0, 0, 1, tiny, 0);
   printf("[case] name=err_zero_wh w=%d h=%d bitdepth=16 comp=%d path=%s bytes=%ld rc=%d (expect 0)\n",
          0, 0, 1, OUTDIR "/err_zero_wh.png", file_bytes(OUTDIR "/err_zero_wh.png"), rc_zero);
   emit_manifest(mf, "err_zero_wh", 0, 0, 1, OUTDIR "/err_zero_wh.png", NULL, rc_zero);

   /* data length smaller than w*h*comp: checked wrapper rejects before writing */
   remove(OUTDIR "/err_short_buffer.png");
   int rc_short = checked_write_png16(OUTDIR "/err_short_buffer.png", 4, 4, 1,
                                      tiny, sizeof tiny / sizeof tiny[0], &shortfall);
   printf("[case] name=err_short_buffer w=%d h=%d bitdepth=16 comp=%d path=%s bytes=%ld rc=%d shortfall=%zu (expect rc=-1)\n",
          4, 4, 1, OUTDIR "/err_short_buffer.png", file_bytes(OUTDIR "/err_short_buffer.png"),
          rc_short, shortfall);
   emit_manifest(mf, "err_short_buffer", 4, 4, 1, OUTDIR "/err_short_buffer.png", NULL, rc_short);
}

static void case_regression_8bit(FILE *mf)
{
   int w = 128, h = 64, comp = 3;
   size_t count = (size_t) w * h * comp;
   unsigned char *data = malloc(count);
   size_t i;
   for (i = 0; i < count; ++i)
      data[i] = (unsigned char) ((i * 37 + 11) & 0xff);
   const char *path = OUTDIR "/regression_8bit.png";
   int rc = stbi_write_png(path, w, h, comp, data, 0);
   printf("[case] name=regression_8bit w=%d h=%d bitdepth=8 comp=%d path=%s bytes=%ld rc=%d\n",
          w, h, comp, path, rc ? file_bytes(path) : -1, rc);
   emit_manifest(mf, "regression_8bit", w, h, comp, path, NULL, rc);

   /* C-side cross-check with stb_image 16-bit loader for the 16-bit outputs. */
   {
      const char *checks[4] = {
         OUTDIR "/gray16_gradient.png", OUTDIR "/rgb16.png", OUTDIR "/rgba16.png",
         OUTDIR "/gray_alpha16.png"
      };
      int q;
      for (q = 0; q < 3; ++q) {
         int lw, lh, lc;
         unsigned short *px = stbi_load_16(checks[q], &lw, &lh, &lc, 1);
         if (px) {
            printf("[stbi_load_16] %s -> w=%d h=%d comp=%d first=%u last=%u\n",
                   checks[q], lw, lh, lc, px[0], px[(size_t) lw*lh*lc-1]);
            stbi_image_free(px);
         } else {
            printf("[stbi_load_16] %s -> FAILED: %s\n", checks[q], stbi_failure_reason());
         }
      }
   }
   free(data);
}

int main(void)
{
   FILE *mf = fopen(OUTDIR "/manifest.txt", "w");
   if (!mf) { perror("manifest"); return 2; }

   case_gray_gradient(mf);
   case_rgb16(mf);
   case_rgba16(mf);
   case_gray_alpha16(mf);
   case_errors(mf);
   case_regression_8bit(mf);

   fclose(mf);
   printf("[done] manifest=" OUTDIR "/manifest.txt\n");
   return 0;
}
