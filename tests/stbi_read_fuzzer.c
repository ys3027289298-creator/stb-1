// stbi_read_fuzzer.c -- fuzz target for stb_image, driven by fuzz_main.c
// (or libFuzzer) through LLVMFuzzerTestOneInput.

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS
#include "stb_image.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
   int w, h, channels;

   if (size < 1 || size > (1 << 24))
      return 0;

   if (stbi_info_from_memory(data, (int) size, &w, &h, &channels)) {
      unsigned char *img8 = stbi_load_from_memory(data, (int) size, &w, &h, &channels, 0);
      stbi_image_free(img8);

      if (stbi_is_16_bit_from_memory(data, (int) size)) {
         stbi_us *img16 = stbi_load_16_from_memory(data, (int) size, &w, &h, &channels, 0);
         stbi_image_free(img16);
      }
   }

   return 0;
}
