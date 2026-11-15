/*
 * sepia_demo.c - tiny self-contained demo for the retro sepia /
 * gamma-space compositing API added to stb_image.h.
 *
 * It prints the three requested datasets:
 *   1. three sample pixels run through the sepia matrix at several strengths
 *   2. gamma-correct vs. linear-direct ("wrong") layer compositing
 *   3. color deviation after stacking two full-strength sepia layers
 * and also verifies that out-of-range strengths/opacity are rejected.
 *
 * Build (no extra libraries, no network):
 *   cc -I.. sepia_demo.c -lm -o sepia_demo
 */

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <stdio.h>
#include <math.h>
#include <string.h>

#define NPIX 3
#define COMP 3

static const unsigned char sample_pixels[NPIX][3] = {
   { 200, 100,  50 },  /* warm orange            */
   {  30, 120, 200 },  /* cool blue              */
   { 128, 128, 128 },  /* neutral mid gray       */
};

static const float strengths[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };

/* exact sRGB (IEC 61966-2-1) transfer functions, 0..1 domain */
static float srgb_to_linear(float c)
{
   return c <= 0.04045f ? c / 12.92f
                        : powf((c + 0.055f) / 1.055f, 2.4f);
}

static float linear_to_srgb(float c)
{
   return c <= 0.0031308f ? c * 12.92f
                          : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

static void print_rgb(const char *tag, const unsigned char *px)
{
   printf("      %-28s (%3u, %3u, %3u)\n", tag, px[0], px[1], px[2]);
}

static void dataset_1_sepia_strengths(void)
{
   int p, s;
   printf("== 1. sepia matrix output for %d pixels x %d strengths ==\n",
          NPIX, (int) (sizeof(strengths) / sizeof(strengths[0])));
   for (p = 0; p < NPIX; ++p) {
      printf("   pixel %d input (%3u, %3u, %3u):\n", p,
             sample_pixels[p][0], sample_pixels[p][1], sample_pixels[p][2]);
      for (s = 0; s < (int) (sizeof(strengths) / sizeof(strengths[0])); ++s) {
         unsigned char out[3];
         char tag[64];
         stbi_sepia_pixel(sample_pixels[p][0], sample_pixels[p][1],
                          sample_pixels[p][2], strengths[s],
                          &out[0], &out[1], &out[2]);
         sprintf(tag, "strength = %.2f", strengths[s]);
         print_rgb(tag, out);
      }
   }
   printf("\n");
}

static void dataset_2_gamma_vs_linear(void)
{
   int p;
   const float opacity = 0.5f;
   unsigned char base[NPIX][COMP], over[NPIX][COMP];
   unsigned char correct[NPIX][COMP], wrong[NPIX][COMP];

   for (p = 0; p < NPIX; ++p)
      memcpy(base[p], sample_pixels[p], 3);

   /* the overlay layer is a full-strength sepia copy of the base */
   memcpy(over, base, sizeof(base));
   stbi_sepia_image(&over[0][0], NPIX, 1, COMP, 1.0f);

   /* correct: composite directly on gamma-encoded bytes */
   memcpy(correct, base, sizeof(base));
   stbi_overlay_gamma(&correct[0][0], &over[0][0], NPIX, 1, COMP, opacity);

   /* wrong: decode to linear, multiply/add there, re-encode */
   for (p = 0; p < NPIX; ++p) {
      int k;
      for (k = 0; k < 3; ++k) {
         float bl = srgb_to_linear(base[p][k] / 255.0f);
         float ol = srgb_to_linear(over[p][k] / 255.0f);
         float mixed = ol * opacity + bl * (1.0f - opacity);
         wrong[p][k] = (unsigned char) (linear_to_srgb(mixed) * 255.0f + 0.5f);
      }
   }

   printf("== 2. gamma-space (correct) vs linear-direct (wrong) overlay, opacity %.2f ==\n",
          opacity);
   for (p = 0; p < NPIX; ++p) {
      int d0 = (int) correct[p][0] - (int) wrong[p][0];
      int d1 = (int) correct[p][1] - (int) wrong[p][1];
      int d2 = (int) correct[p][2] - (int) wrong[p][2];
      printf("   pixel %d:\n", p);
      print_rgb("correct (gamma blend)", correct[p]);
      print_rgb("wrong   (linear blend)", wrong[p]);
      printf("      %-28s (%+3d, %+3d, %+3d)  |delta| sum = %d / 765\n",
             "correct - wrong", d0, d1, d2,
             abs(d0) + abs(d1) + abs(d2));
   }
   printf("\n");
}

static void dataset_3_two_stacked_layers(void)
{
   int p;
   unsigned char once[NPIX][COMP], twice[NPIX][COMP], second[NPIX][COMP];

   memcpy(once, sample_pixels, sizeof(sample_pixels));
   stbi_sepia_image(&once[0][0], NPIX, 1, COMP, 1.0f);

   /*
    * Base layer: one full-strength sepia pass on the original pixels.
    * Second layer: sepia applied twice to the original pixels, i.e. a
    * stronger, independently-prepared effect layer, composited at 60%
    * opacity through the gamma-space overlay API. Identical opaque layers
    * would cancel out, so partial opacity gives the realistic stacked-look
    * case and a measurable color deviation.
    */
   memcpy(second, sample_pixels, sizeof(sample_pixels));
   stbi_sepia_image(&second[0][0], NPIX, 1, COMP, 1.0f);
   stbi_sepia_image(&second[0][0], NPIX, 1, COMP, 1.0f);

   memcpy(twice, once, sizeof(once));
   stbi_overlay_gamma(&twice[0][0], &second[0][0], NPIX, 1, COMP, 0.6f);

   printf("== 3. color deviation: one sepia layer vs sepia + second sepia layer (60%%) ==\n");
   for (p = 0; p < NPIX; ++p) {
      int d0 = (int) twice[p][0] - (int) once[p][0];
      int d1 = (int) twice[p][1] - (int) once[p][1];
      int d2 = (int) twice[p][2] - (int) once[p][2];
      printf("   pixel %d:\n", p);
      print_rgb("one sepia layer ", once[p]);
      print_rgb("two sepia layers", twice[p]);
      printf("      %-28s (%+3d, %+3d, %+3d)  |delta| sum = %d / 765\n",
             "two - one", d0, d1, d2,
             abs(d0) + abs(d1) + abs(d2));
   }
   printf("\n");
}

static void check_out_of_range(void)
{
   static const float bad_strengths[] = { -0.1f, 1.1f, 0.0f / 0.0f };
   static const char *names[] = { "-0.1", "1.1", "NaN" };
   unsigned char buf[NPIX][COMP], backup[NPIX][COMP];
   unsigned char over[NPIX][COMP];
   int i;

   memcpy(buf, sample_pixels, sizeof(sample_pixels));
   memcpy(backup, sample_pixels, sizeof(sample_pixels));
   memcpy(over, sample_pixels, sizeof(sample_pixels));

   printf("== 4. out-of-range arguments must be rejected (return -1, buffer untouched) ==\n");
   for (i = 0; i < 3; ++i) {
      int rc = stbi_sepia_image(&buf[0][0], NPIX, 1, COMP, bad_strengths[i]);
      int untouched = memcmp(buf, backup, sizeof(buf)) == 0;
      printf("   stbi_sepia_image(strength=%-4s) -> rc=%d, buffer %s, reason: %s\n",
             names[i], rc, untouched ? "unchanged" : "MODIFIED!",
             stbi_failure_reason());
   }
   {
      int rc = stbi_overlay_gamma(&buf[0][0], &over[0][0], NPIX, 1, COMP, 1.5f);
      int untouched = memcmp(buf, backup, sizeof(buf)) == 0;
      printf("   stbi_overlay_gamma(opacity=1.5 ) -> rc=%d, buffer %s, reason: %s\n",
             rc, untouched ? "unchanged" : "MODIFIED!", stbi_failure_reason());
   }
   {
      /* boundary values are accepted and strength 0 is an identity pass */
      int rc0 = stbi_sepia_image(&buf[0][0], NPIX, 1, COMP, 0.0f);
      int identity = memcmp(buf, backup, sizeof(buf)) == 0;
      int rc1 = stbi_sepia_image(&buf[0][0], NPIX, 1, COMP, 1.0f);
      printf("   boundary strengths 0.0 -> rc=%d (identity=%s), 1.0 -> rc=%d\n",
             rc0, identity ? "yes" : "no", rc1);
   }
}

int main(void)
{
   dataset_1_sepia_strengths();
   dataset_2_gamma_vs_linear();
   dataset_3_two_stacked_layers();
   check_out_of_range();
   return 0;
}
