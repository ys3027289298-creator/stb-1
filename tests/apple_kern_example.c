// Sample kerning pairs from a font (exercises Apple 'kern' version 0 tables).
// usage: apple_kern_example font.ttf pairs.txt
// pairs.txt has one "L R value" entry per line; prints "L R kern" per pair.
#include <stdio.h>
#include <stdlib.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "../stb_truetype.h"

int main(int argc, char **argv)
{
   static unsigned char ttf[1 << 20];
   stbtt_fontinfo font;
   FILE *f;
   char left, right;
   int expected;

   if (argc < 3) {
      fprintf(stderr, "usage: %s font.ttf pairs.txt\n", argv[0]);
      return 1;
   }

   f = fopen(argv[1], "rb");
   if (!f || !fread(ttf, 1, sizeof(ttf), f)) {
      fprintf(stderr, "cannot read %s\n", argv[1]);
      return 1;
   }
   fclose(f);

   if (!stbtt_InitFont(&font, ttf, 0)) {
      fprintf(stderr, "cannot init font %s\n", argv[1]);
      return 1;
   }

   f = fopen(argv[2], "r");
   if (!f) {
      fprintf(stderr, "cannot read %s\n", argv[2]);
      return 1;
   }
   {
      char line[128];
      while (fgets(line, sizeof(line), f)) {
         if (line[0] == '#')
            continue;
         if (sscanf(line, " %c %c %d", &left, &right, &expected) == 3)
            printf("%c %c %d\n", left, right,
                   stbtt_GetCodepointKernAdvance(&font, left, right));
      }
   }
   fclose(f);
   return 0;
}
