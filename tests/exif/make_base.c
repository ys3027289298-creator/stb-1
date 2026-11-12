// Generate base.jpg in memory: 8x12 RGB with four distinct corner colors.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { unsigned char *data; int len; } Buffer;
static void append(void *u, void *d, int n) {
   Buffer *b = u; unsigned char *p = realloc(b->data, (size_t) b->len + n);
   if (!p) exit(1); memcpy(p + b->len, d, (size_t) n); b->data = p; b->len += n;
}
int main(void) {
   enum { W = 8, H = 12 };
   unsigned char px[W * H * 3], *q = px;
   const unsigned char C[4][3] = {{200,40,40},{40,200,40},{40,40,200},{200,200,40}};
   for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
         const unsigned char *c = C[(y >= H / 2) * 2 + (x >= W / 2)];
         *q++ = c[0]; *q++ = c[1]; *q++ = c[2];
      }
   Buffer b = {0};
   if (!stbi_write_jpg_to_func(append, &b, W, H, 3, px, 90)) return 1;
   FILE *f = fopen("tests/exif/base.jpg", "wb");
   if (!f || fwrite(b.data, 1, (size_t) b.len, f) != (size_t) b.len) return 1;
   fclose(f); free(b.data);
   printf("wrote tests/exif/base.jpg %dx%d\n", W, H);
   return 0;
}
