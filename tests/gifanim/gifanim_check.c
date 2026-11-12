#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <stdio.h>
#include <stdlib.h>

static unsigned char *read_file(const char *p, int *len) {
    FILE *f = fopen(p, "rb");
    if (!f) { perror(p); exit(1); }
    fseek(f, 0, SEEK_END);
    *len = (int) ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t) *len);
    fread(b, 1, (size_t) *len, f);
    fclose(f);
    return b;
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        int len, x, y, z, comp;
        unsigned char *buf = read_file(argv[i], &len);
        int *delays = NULL;
        unsigned char *frames = stbi_load_gif_from_memory(buf, len, &delays, &x, &y, &z, &comp, STBI_rgb_alpha);
        printf("== %s ==\n", argv[i]);
        if (!frames) { printf("ERROR: %s\n", stbi_failure_reason()); free(buf); continue; }
        printf("frames=%d size=%dx%d comp=%d\n", z, x, y, comp);
        int cx = x / 2, cy = y / 2;
        for (int f = 0; f < z; ++f) {
            unsigned char *px = frames + ((f * y + cy) * x + cx) * 4;
            unsigned char *corner = frames + (f * y * x) * 4;
            printf("  frame %d: delay_ms=%d center_rgba=(%d,%d,%d,%d)\n",
                   f, delays ? delays[f] : -1, px[0], px[1], px[2], px[3]);
            printf("           corner_rgba=(%d,%d,%d,%d)\n",
                   corner[0], corner[1], corner[2], corner[3]);
        }
        if (z == 1) {
            int ox, oy, oc;
            unsigned char *one = stbi_load_from_memory(buf, len, &ox, &oy, &oc, STBI_rgb_alpha);
            int mismatches = 0;
            if (!one || ox != x || oy != y) {
                mismatches = -1;
            } else {
                for (int k = 0; k < x * y * 4; ++k)
                    if (one[k] != frames[k]) ++mismatches;
            }
            printf("  single-frame vs stbi_load_from_memory: %s (mismatches=%d)\n",
                   mismatches == 0 ? "IDENTICAL" : "DIFFER", mismatches);
            free(one);
        }
        free(frames);
        free(delays);
        free(buf);
    }
    return 0;
}
