#!/usr/bin/env python3
"""Stage 4/5 verification: read PNGs back with pypng (and Pillow as a second
mainstream library), compare against the written samples, and diff the 8-bit
regression image against the pre-change baseline pixel by pixel."""

import array
import os
import sys

import png
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "output")


def color_type_of(info):
    if info["greyscale"]:
        return 4 if info["alpha"] else 0
    return 6 if info["alpha"] else 2


PNG_COLOR_TYPES = {0: "grayscale", 2: "RGB", 4: "gray+alpha", 6: "RGBA"}
PILLOW_MODES = {0: "I;16", 2: "RGB", 4: "I;16L", 6: "RGBA"}


def load_manifest():
    rows = []
    with open(os.path.join(OUT, "manifest.txt")) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            name, w, h, comp, bd, png_path, raw_path, rc = line.split("|")
            rows.append({
                "name": name,
                "w": int(w), "h": int(h), "comp": int(comp),
                "bitdepth": int(bd),
                "png": os.path.join(HERE, png_path) if not os.path.isabs(png_path) else png_path,
                "raw": os.path.join(HERE, raw_path) if raw_path else "",
                "rc": int(rc),
            })
    return rows


def read_expected_raw(path, count):
    """Sidecar .raw files are native-endian unsigned shorts (host layout);
    pypng also hands back decoded rows in host byte order."""
    a = array.array("H")
    with open(path, "rb") as f:
        a.fromfile(f, count)
    return a


def verify_16bit_case(row):
    print(f"\n== verify {row['name']} ==")
    reader = png.Reader(filename=row["png"])
    width, height, rows, info = reader.read()
    planes = info["planes"]
    row_bytes = width * planes * (info["bitdepth"] // 8)
    color_type = color_type_of(info)
    print(f"pypng      : width={width} height={height} bitdepth={info['bitdepth']} "
          f"color_type={color_type}({PNG_COLOR_TYPES[color_type]}) "
          f"channels={planes} row_bytes={row_bytes}")
    assert width == row["w"] and height == row["h"], "dimension mismatch"
    assert info["bitdepth"] == 16, "expected 16-bit samples"
    assert planes == row["comp"], "channel count mismatch"
    assert row_bytes == row["w"] * row["comp"] * 2, "row byte count mismatch"

    got = array.array("H")
    got.frombytes(b"".join(rows))

    expected = read_expected_raw(row["raw"], row["w"] * row["h"] * row["comp"])
    assert len(got) == len(expected), f"sample count {len(got)} != {len(expected)}"
    max_err = 0
    mismatches = 0
    sample_idx = [0, 1, len(got) // 2, len(got) - 2, len(got) - 1]
    for i, (g, e) in enumerate(zip(got, expected)):
        d = abs(g - e)
        if d:
            mismatches += 1
            if d > max_err:
                max_err = d
    print("samples    : written_vs_readback (index: written -> readback)")
    for i in sample_idx:
        print(f"             [{i:>7}] {expected[i]:5d} -> {got[i]:5d}")
    print(f"max_error  : {max_err}   mismatched_samples: {mismatches} / {len(got)}")
    assert mismatches == 0, "16-bit sample roundtrip mismatch"

    # Second mainstream decoder for good measure.
    with Image.open(row["png"]) as im:
        im.load()
        extrema = im.getextrema()
        print(f"Pillow     : mode={im.mode} size={im.size} extrema={extrema} (decoded OK)")
        # Pillow normalizes gray+alpha to RGBA in some builds; both are acceptable decodes.
        assert im.mode in (PILLOW_MODES[color_type], "RGBA"), f"unexpected Pillow mode {im.mode}"


def verify_errors(rows):
    print("\n== error handling ==")
    for row in rows:
        if not row["name"].startswith("err_"):
            continue
        exists = os.path.exists(row["png"])
        print(f"{row['name']:>16}: rc={row['rc']} file_exists={exists} size="
              f"{os.path.getsize(row['png']) if exists else 0}")
        assert not exists, f"{row['name']} must not produce a file"
        assert row["rc"] <= 0, "expected failure return"


def verify_regression(rows):
    print("\n== 8-bit regression vs pre-change baseline ==")
    row = next(r for r in rows if r["name"] == "regression_8bit")
    baseline = os.path.join(OUT, "baseline_8bit.png")

    rb = png.Reader(filename=baseline).read()
    rn = png.Reader(filename=row["png"]).read()
    wb, hb, rows_b, info_b = rb
    wn, hn, rows_n, info_n = rn
    print(f"baseline   : {baseline} {wb}x{hb} bitdepth={info_b['bitdepth']} "
          f"color_type={color_type_of(info_b)} channels={info_b['planes']}")
    print(f"current    : {row['png']} {wn}x{hn} bitdepth={info_n['bitdepth']} "
          f"color_type={color_type_of(info_n)} channels={info_n['planes']}")
    assert (wb, hb) == (wn, hn) and info_b["planes"] == info_n["planes"]

    flat_b = array.array("B")
    flat_n = array.array("B")
    flat_b.frombytes(b"".join(rows_b))
    flat_n.frombytes(b"".join(rows_n))
    assert len(flat_b) == len(flat_n)
    diffs = sum(1 for a, b in zip(flat_b, flat_n) if a != b)
    byte_identical = open(baseline, "rb").read() == open(row["png"], "rb").read()
    print(f"pixels     : total={len(flat_b)} per-pixel byte diffs={diffs} "
          f"(expect 0)   byte-identical files: {byte_identical}")
    assert diffs == 0


def main():
    rows = load_manifest()
    for row in rows:
        if row["bitdepth"] == 16 and row["rc"] == 0 and row["raw"]:
            verify_16bit_case(row)
    verify_errors(rows)
    verify_regression(rows)
    print("\nALL CHECKS PASSED")


if __name__ == "__main__":
    main()
