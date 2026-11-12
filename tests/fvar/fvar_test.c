// fvar smoke test: dumps axis/instance metadata and exercises range checks.
#include <stdio.h>
#include <stdlib.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

static unsigned char *read_file(const char *path, long *size)
{
   FILE *f = fopen(path, "rb");
   if (!f) return NULL;
   fseek(f, 0, SEEK_END);
   *size = ftell(f);
   fseek(f, 0, SEEK_SET);
   unsigned char *buf = (unsigned char *) malloc((size_t)*size);
   fread(buf, 1, (size_t)*size, f);
   fclose(f);
   return buf;
}

int main(int argc, char **argv)
{
   if (argc < 3) { fprintf(stderr, "usage: %s dump|oob <font> [tag value]\n", argv[0]); return 2; }
   long size;
   unsigned char *buf = read_file(argv[2], &size);
   if (!buf) { fprintf(stderr, "cannot read %s\n", argv[2]); return 2; }
   stbtt_fontinfo font;
   if (!stbtt_InitFont(&font, buf, 0)) { printf("%s: InitFont FAILED\n", argv[2]); return 1; }

   if (argv[1][0] == 'o') {                                  // out-of-range / bad-tag calls
      stbtt_uint32 tag = STBTT_TAG(argv[3][0], argv[3][1], argv[3][2], argv[3][3]);
      int rc = stbtt_SetAxisValueByTag(&font, tag, (float) atof(argv[4]));
      printf("%s: SetAxisValueByTag(%s,%s) -> error %d\n", argv[2], argv[3], argv[4], rc);
      return 0;
   }

   int axes = stbtt_GetAxisCount(&font);
   printf("file=%s axes=%d\n", argv[2], axes);
   for (int i = 0; i < axes; ++i) {
      stbtt_axisinfo a;
      stbtt_GetAxisInfo(&font, i, &a);
      char t[4] = { (char)(a.tag >> 24), (char)(a.tag >> 16),
                    (char)(a.tag >> 8), (char) a.tag };
      printf("  axis tag=%.4s min=%.1f default=%.1f max=%.1f\n",
             t, a.minValue, a.defaultValue, a.maxValue);
   }
   printf("  named_instances=%d\n", stbtt_GetNamedInstanceCount(&font));
   for (int i = 0; i < stbtt_GetNamedInstanceCount(&font); ++i) {
      stbtt_namedinstance ni;
      stbtt_GetNamedInstance(&font, i, &ni);
      printf("  instance nameID=%d coord=%.1f\n", ni.nameID, ni.coordinates[0]);
   }
   return 0;
}
