#!/usr/bin/env python3
"""The Fiat in other colours, for the rival cars: its body's texture with the olive green it
is painted in changed for another colour, keeping the wear on it and the decals over it.

    python3 Tools/repaint_fiat.py <Textures folder>

Writes Body_Paint_baseColor_<name>.jpg for each colour, at 1024 pixels (a rival is seen
smaller than the car the player drives)."""
import os, sys

import numpy as np
from PIL import Image

COLOURS = {
    "red": (172, 26, 22),
    "blue": (24, 64, 168),
    "white": (214, 212, 204),
    "yellow": (226, 168, 22),
}


def main():
    folder = sys.argv[1]
    source = Image.open(os.path.join(folder, "Body_Paint_baseColor.jpg")).convert("RGB").resize((1024, 1024), Image.LANCZOS)
    pixels = np.asarray(source).astype(np.float32)
    # The paint: the commonest colour, and what is near it in hue (lighter or darker with wear).
    flat = (pixels // 8).reshape(-1, 3).astype(np.int32)
    keys = flat[:, 0] * 1024 + flat[:, 1] * 32 + flat[:, 2]
    commonest = np.bincount(keys).argmax()
    paint = np.array([commonest // 1024, (commonest // 32) % 32, commonest % 32], np.float32) * 8 + 4
    brightness = pixels.mean(axis=2, keepdims=True)
    base_brightness = paint.mean()
    chroma = pixels / np.maximum(brightness, 1)
    base_chroma = paint / base_brightness
    distance = np.linalg.norm(chroma - base_chroma, axis=2)
    # Fully the new colour where the hue is the paint's, fading out toward the decals' colours.
    weight = np.clip(1.0 - (distance - 0.1) / 0.15, 0, 1)[..., None]
    for name, colour in COLOURS.items():
        colour = np.array(colour, np.float32)
        repainted = colour * (brightness / base_brightness)
        mixed = repainted * weight + pixels * (1 - weight)
        out = Image.fromarray(np.clip(mixed, 0, 255).astype(np.uint8))
        target = os.path.join(folder, "Body_Paint_baseColor_%s.jpg" % name)
        out.save(target, quality=88)
        print("wrote", target)


if __name__ == "__main__":
    main()
