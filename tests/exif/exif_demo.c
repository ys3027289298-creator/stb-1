// Read one JPEG twice (EXIF switch off/on): print orientation, top-left pixel, size.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <stdio.h>

static void read_once(const char *path, int apply)
{
   int w, h, c;
   stbi_set_apply_exif_orientation(apply);
   unsigned char *px = stbi_load(path, &w, &h, &c, 3);
   if (!px) { printf("  switch=%s LOAD FAILED\n", apply ? "ON " : "OFF"); return; }
   printf("  switch=%s topleft=(%d,%d,%d) size=%dx%d\n",
          apply ? "ON " : "OFF", px[0], px[1], px[2], w, h);
   stbi_image_free(px);
}

int main(int argc, char **argv)
{
   if (argc < 3) { printf("usage: %s <case_name> <jpeg>\n", argv[0]); return 2; }
   printf("CASE %s (orientation tag = %d)\n", argv[1], stbi_get_exif_orientation(argv[2]));
   read_once(argv[2], 0);
   read_once(argv[2], 1);
   return 0;
}
