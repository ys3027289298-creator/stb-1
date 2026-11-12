# 16-bit PNG writer demo

Adds bit-depth-16 PNG output to `stb_image_write.h` without touching the
existing 8-bit code path, plus a runnable five-stage demo.

## New API (in `../stb_image_write.h`)

```c
int stbi_write_png16(char const *filename, int w, int h, int comp,
                     const unsigned short *data, ptrdiff_t stride_in_bytes);
int stbi_write_png16_to_func(stbi_write_func *func, void *context, int w, int h,
                     int comp, const unsigned short *data, ptrdiff_t stride_in_bytes);
unsigned char *stbi_write_png16_to_mem(const unsigned short *pixels,
                     ptrdiff_t stride_bytes, int x, int y, int n, int *out_len);
```

- Samples are `unsigned short`; IHDR bit depth is 16 and samples are emitted
  big-endian (network byte order).
- `comp` 1/2/3/4 maps to PNG color types grayscale (0), gray+alpha (4),
  RGB (2) and RGBA (6); alpha is an unsigned 16-bit value.
- PNG filters are applied byte-wise on the big-endian sample stream per the
  PNG spec (None/Sub/Up/Average/Paeth, adaptive by default).
- Zero/negative dimensions, bad component counts and size overflows return
  failure before any file is created. `stride_in_bytes` is in bytes; pass 0
  for the tightly packed default `w*comp*2`.
- The original `stbi_write_png*` 8-bit functions are unchanged and produce
  byte-identical output to the original header.

## Run

```sh
# one-time: local venv with the Python PNG decoders (does not touch system Python)
python3 -m venv ../.venv-png16
../.venv-png16/bin/pip install pypng Pillow

./run_demo.sh                 # five stages, no interactive input
```

A recording of the full run is saved at `output/demo_recording.cast`
(`asciinema play output/demo_recording.cast`).

## Stages

1. environment: directory, compiler, Python
2. build: exact commands and exit codes
3. write cases: 16-bit grayscale gradient, gray+alpha, RGB, RGBA (alpha ramp
   through half-transparent), a strided RGB case, plus two failure cases
   (zero width/height, buffer shorter than w*h*comp) and the 8-bit regression
   image; each prints name, dimensions, bit depth, output path and byte size
4. read-back: pypng reports actual bit depth/color type/row bytes, samples are
   compared against the written values with max error; Pillow decodes as a
   second mainstream library; `stbi_load_16` cross-checks from C
5. regression: the 8-bit image is diffed per-pixel against the pre-change
   baseline (`output/baseline_8bit.png`), expected diff count 0

All generated artifacts live under `output/`.
