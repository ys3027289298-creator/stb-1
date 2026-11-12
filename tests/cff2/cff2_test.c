// Validates CFF2 support in stb_truetype against an unchanged TrueType path.
// For each font given on the command line, prints every glyph's index, number
// of outline segments (stbtt_vertex entries) and bounding box.
#include <stdio.h>
#include <stdlib.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "../../stb_truetype.h"

static unsigned char *read_file(const char *path, size_t *length)
{
   FILE *f = fopen(path, "rb");
   if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(1); }
   fseek(f, 0, SEEK_END);
   long n = ftell(f);
   fseek(f, 0, SEEK_SET);
   unsigned char *data = (unsigned char *) malloc((size_t) n);
   if (fread(data, 1, (size_t) n, f) != (size_t) n) { fprintf(stderr, "short read\n"); exit(1); }
   fclose(f);
   *length = (size_t) n;
   return data;
}

int main(int argc, char **argv)
{
   int font_arg;
   for (font_arg = 1; font_arg < argc; font_arg++) {
      size_t length;
      unsigned char *data = read_file(argv[font_arg], &length);
      stbtt_fontinfo info;
      if (!stbtt_InitFont(&info, data, 0)) {
         fprintf(stderr, "stbtt_InitFont failed for %s\n", argv[font_arg]);
         return 1;
      }
      printf("== %s (%s) numGlyphs=%d ==\n", argv[font_arg], info.cff2 ? "CFF2" : (info.cff.size ? "CFF" : "TTF"), info.numGlyphs);
      int i;
      for (i = 0; i < info.numGlyphs; i++) {
         stbtt_vertex *v = NULL;
         int segments = stbtt_GetGlyphShape(&info, i, &v);
         int x0, y0, x1, y1;
         stbtt_GetGlyphBox(&info, i, &x0, &y0, &x1, &y1);
         printf("glyph=%d segments=%d bbox=(%d,%d)-(%d,%d)\n", i, segments, x0, y0, x1, y1);
         free(v);
      }
      free(data);
   }
   return 0;
}
