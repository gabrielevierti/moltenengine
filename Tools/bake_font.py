#!/usr/bin/env python3
"""Bakes a font file into the two files the engine's text rendering needs.

    python3 Tools/bake_font.py /path/to/Font.otf Assets/Fonts/Default

writes Assets/Fonts/Default.png (the atlas) and Assets/Fonts/Default.txt (the metrics).
Needs Pillow, NumPy and SciPy:  pip install pillow numpy scipy

The atlas is a signed distance field. Each texel stores how far it is from the
nearest outline of the glyph, instead of how much of the texel the glyph covers:

    0.5        exactly on the outline
    above 0.5  inside the glyph
    below 0.5  outside

`--spread` texels on each side of the outline map onto the 0...1 range. The fragment
shader (Shaders/Canvas.metal) turns the distance back into a sharp, anti-aliased edge
at whatever size the text is drawn, which a plain bitmap font cannot do.

The metrics file is plain text, one record per line:

    info <bakedSize> <lineHeight> <distanceRange> <atlasWidth> <atlasHeight>
    <codepoint> <x> <y> <width> <height> <offsetX> <offsetY> <advance>

All numbers are in atlas texels. (x, y, width, height) is the glyph's rectangle in
the atlas; (offsetX, offsetY) goes from the pen position at the top of the line to the
rectangle's top left corner; advance is how far the pen moves afterwards.
"""
import argparse

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

# Basic Latin, the Latin-1 letters and signs, and a few symbols interfaces like to use.
DEFAULT_CHARACTERS = (
    [chr(c) for c in range(32, 127)]
    + [chr(c) for c in range(161, 256)]
    + list("–—‘’“”•…€←↑→↓")
)


def signed_distance(mask):
    """Distance in pixels to the outline: positive inside the glyph, negative outside."""
    inside = ndimage.distance_transform_edt(mask)
    outside = ndimage.distance_transform_edt(~mask)
    # The transform measures to the center of the nearest pixel on the other side;
    # the outline itself is half a pixel closer.
    return np.where(mask, inside - 0.5, -(outside - 0.5))


def bake(font_path, output, size, spread, supersample, atlas_width, characters):
    # Glyphs are drawn several times larger than needed and reduced afterwards:
    # distances measured on the large image are accurate to a fraction of a texel.
    font = ImageFont.truetype(font_path, size * supersample)
    ascent, descent = font.getmetrics()
    line_height = (ascent + descent) / supersample
    pad = spread * supersample

    def render(character):
        left, top, right, bottom = font.getbbox(character)
        width = -(-(right - left + 2 * pad) // supersample) * supersample      # round up to whole texels
        height = -(-(bottom - top + 2 * pad) // supersample) * supersample
        image = Image.new("L", (width, height), 0)
        ImageDraw.Draw(image).text((pad - left, pad - top), character, font=font, fill=255)
        return np.asarray(image) >= 128, left, top

    # What the font draws for a character it does not have.
    missing_mask, _, _ = render("￿")

    glyphs = []
    for character in characters:
        advance = font.getlength(character) / supersample
        left, top, right, bottom = font.getbbox(character)
        if right <= left or bottom <= top or character.isspace():
            glyphs.append({"char": character, "advance": advance, "field": None})
            continue
        mask, left, top = render(character)
        if mask.shape == missing_mask.shape and np.array_equal(mask, missing_mask):
            print(f"  not in this font, skipped: U+{ord(character):04X}")
            continue
        distance = signed_distance(mask) / supersample                 # in atlas texels
        field = np.clip(0.5 + distance / (2 * spread), 0.0, 1.0)
        h, w = field.shape
        field = field.reshape(h // supersample, supersample, w // supersample, supersample).mean(axis=(1, 3))
        glyphs.append({
            "char": character, "advance": advance, "field": field,
            "offset_x": left / supersample - spread, "offset_y": top / supersample - spread,
        })

    # Pack the rectangles in rows ("shelves"), tallest first so rows waste little space.
    x = y = row_height = 1
    for glyph in sorted((g for g in glyphs if g["field"] is not None), key=lambda g: -g["field"].shape[0]):
        h, w = glyph["field"].shape
        if x + w + 1 > atlas_width:
            x, y, row_height = 1, y + row_height + 1, 0
        glyph["x"], glyph["y"] = x, y
        x += w + 1
        row_height = max(row_height, h)
    used_height = y + row_height + 1
    atlas_height = 64
    while atlas_height < used_height:
        atlas_height *= 2

    atlas = np.zeros((atlas_height, atlas_width), dtype=np.float64)
    for glyph in glyphs:
        if glyph["field"] is not None:
            h, w = glyph["field"].shape
            atlas[glyph["y"]:glyph["y"] + h, glyph["x"]:glyph["x"] + w] = glyph["field"]
    Image.fromarray(np.round(atlas * 255).astype(np.uint8), "L").save(output + ".png", optimize=True)

    with open(output + ".txt", "w") as metrics:
        metrics.write(f"info {size} {line_height:.3f} {2 * spread} {atlas_width} {atlas_height}\n")
        for glyph in glyphs:
            if glyph["field"] is None:
                metrics.write(f"{ord(glyph['char'])} 0 0 0 0 0 0 {glyph['advance']:.3f}\n")
            else:
                h, w = glyph["field"].shape
                metrics.write(f"{ord(glyph['char'])} {glyph['x']} {glyph['y']} {w} {h} "
                              f"{glyph['offset_x']:.3f} {glyph['offset_y']:.3f} {glyph['advance']:.3f}\n")
    print(f"{len(glyphs)} glyphs at {size} px in a {atlas_width} x {atlas_height} atlas -> {output}.png, {output}.txt")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Bake a font into a signed distance field atlas.")
    parser.add_argument("font", help="a .ttf or .otf file")
    parser.add_argument("output", help="output path without extension, for example Assets/Fonts/Default")
    parser.add_argument("--size", type=int, default=44, help="glyph size in atlas texels (default 44)")
    parser.add_argument("--spread", type=int, default=4, help="texels of distance stored on each side of the outline (default 4)")
    parser.add_argument("--supersample", type=int, default=4, help="how much larger glyphs are drawn before reducing (default 4)")
    parser.add_argument("--width", type=int, default=1024, help="atlas width in texels (default 1024)")
    parser.add_argument("--characters", default=None, help="the characters to include (default: Latin-1 and a few symbols)")
    arguments = parser.parse_args()
    bake(arguments.font, arguments.output, arguments.size, arguments.spread, arguments.supersample, arguments.width,
         list(arguments.characters) if arguments.characters else DEFAULT_CHARACTERS)
