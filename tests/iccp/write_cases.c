// Writes 4 PNGs exercising the iCCP support in stb_image_write.h.
// usage: write_cases <outdir> <fake.icc>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../stb_image_write.h"

static unsigned char *read_file(const char *path, long *len)
{
   FILE *f = fopen(path, "rb");
   unsigned char *buf;
   if (!f) return NULL;
   fseek(f, 0, SEEK_END); *len = ftell(f); fseek(f, 0, SEEK_SET);
   buf = (unsigned char *) malloc(*len);
   if (buf && fread(buf, 1, *len, f) != (size_t) *len) { free(buf); buf = NULL; }
   fclose(f);
   return buf;
}

static int write_case(const char *dir, const char *name, unsigned char *img)
{
   char path[512];
   snprintf(path, sizeof(path), "%s/case_%s.png", dir, name);
   return stbi_write_png(path, 8, 8, 3, img, 0);
}

int main(int argc, char **argv)
{
   unsigned char img[8*8*3];
   unsigned char *icc;
   long icc_len;
   char long_name[101];
   int i, rc, ok = 1;

   if (argc < 3) { fprintf(stderr, "usage: %s <outdir> <fake.icc>\n", argv[0]); return 2; }
   icc = read_file(argv[2], &icc_len);
   if (!icc) { fprintf(stderr, "cannot read %s\n", argv[2]); return 2; }
   for (i = 0; i < 8*8*3; ++i) img[i] = (unsigned char) (i * 5 + 3);
   memset(long_name, 'n', 100); long_name[100] = 0;

   /* case 1: valid ICC profile */
   stbi_write_png_icc_profile(NULL, NULL, 0);
   rc = stbi_write_png_icc_profile("FakePrintICC", icc, (int) icc_len);
   ok &= write_case(argv[1], "with_icc", img);
   printf("case=with_icc set_rc=%d\n", rc);

   /* case 2: empty ICC profile (0 bytes) */
   stbi_write_png_icc_profile(NULL, NULL, 0);
   rc = stbi_write_png_icc_profile("EmptyICC", icc, 0);
   ok &= write_case(argv[1], "empty_icc", img);
   printf("case=empty_icc set_rc=%d\n", rc);

   /* case 3: profile name too long (100 > 79) */
   stbi_write_png_icc_profile(NULL, NULL, 0);
   rc = stbi_write_png_icc_profile(long_name, icc, (int) icc_len);
   ok &= write_case(argv[1], "long_name", img);
   printf("case=long_name set_rc=%d (expect %d=STBIW_ICC_ERR_NAME_LENGTH)\n", rc, STBIW_ICC_ERR_NAME_LENGTH);

   /* case 4: no ICC profile at all */
   stbi_write_png_icc_profile(NULL, NULL, 0);
   ok &= write_case(argv[1], "no_icc", img);
   printf("case=no_icc set_rc=0\n");

   free(icc);
   return ok ? 0 : 1;
}
