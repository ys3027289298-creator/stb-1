#!/usr/bin/env python3
# Generates a fake ICC profile (deterministic, 2048 bytes, not a real profile).
import os
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fake.icc")
data = bytes(((i * 37 + 11) & 0xFF) for i in range(2048))
with open(out, "wb") as f:
    f.write(data)
print("wrote %s (%d bytes)" % (out, len(data)))
