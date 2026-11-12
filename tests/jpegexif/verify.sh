#!/usr/bin/env bash
# Round-trip verification for JPEG APP1/EXIF writing. Run from repo root.
set -e
cd "$(dirname "$0")/../.."
mkdir -p output

echo "=== Stage 1/4: build jpegexif driver ==="
cc -std=c99 -Wall -Wextra -I. tests/jpegexif/jpegexif_driver.c -o output/jpegexif_driver -lm
sleep 11

echo "=== Stage 2/4: write four cases, stb_image read-back ==="
./output/jpegexif_driver | tee output/jpegexif_report.txt
sleep 11

echo "=== Stage 3/4: no-EXIF output stays byte-identical ==="
cmp output/baseline.jpg output/baseline_exif.jpg \
  && echo "with_exif(NULL,0) vs stbi_write_jpg: byte-identical" | tee -a output/jpegexif_report.txt
sleep 11

echo "=== Stage 4/4: common-library read-back (Pillow + raw APP1 parse) ==="
python3 - <<'PY' | tee -a output/jpegexif_report.txt
import struct
from PIL import Image

def app1_payload(path):
    data = open(path, "rb").read()
    assert data[:2] == b"\xff\xd8"
    p = 2
    while data[p] == 0xFF and data[p+1] == 0xE1:
        ln = struct.unpack(">H", data[p+2:p+4])[0]
        seg, p = data[p+4:p+2+ln], p + 2 + ln
        if seg[:6] == b"Exif\0\0":
            return seg[6:]
    return None

for name in ("small_exif", "near_limit_exif", "empty_exif", "no_exif", "file_api"):
    path = f"output/{name}.jpg"
    raw = app1_payload(path)
    with Image.open(path) as im:
        im.load()
        exif = im.getexif()
    print(f"{name:15s} pillow_open=ok pillow_exif={type(exif).__name__} app1_bytes={0 if raw is None else len(raw)}")
PY
echo "report: output/jpegexif_report.txt"
