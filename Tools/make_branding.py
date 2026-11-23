#!/usr/bin/env python3
"""Draws Molten's logo, its mark and its app icon, and renders every picture the engine and the
website use from them.

The mark is the same ribbon as always: a band of metal bent into an M, cold steel on the left,
molten on the right, its lower edge glowing hot, its end turned over to show the other side.
It used to exist only as a small picture; drawn here it is sharp at any size.

    python3 Tools/make_branding.py            # writes Assets/Branding and Website/images

Needs rsvg-convert (librsvg) to turn the drawings into pictures.
"""

import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# ---- the ribbon, in a 1126 x 520 frame (the mark's own picture is that shape)

# The top edge, from the bottom left over both humps down to the bottom right.
TOP = ("M 34 428 C 120 330, 220 84, 332 82 C 440 80, 505 214, 576 214 "
       "C 650 214, 700 80, 792 80 C 890 80, 960 330, 1066 430")
# The lower edge, the hot one, from the bottom right back to the bottom left.
LOWER = ("M 1066 430 C 965 345, 880 214, 800 214 C 715 214, 650 382, 576 382 "
         "C 500 382, 420 238, 302 238 C 200 238, 110 360, 34 428")
# The front of the band: between the two edges.
FRONT = TOP + " " + LOWER.replace("M 1066 430 ", "") + " Z"
# The end of the band turned over: its other side, under the hot edge on the right.
BACK = ("M 676 290 C 700 280, 725 276, 744 280 C 790 292, 830 360, 880 404 "
        "C 920 432, 990 434, 1066 430 C 965 345, 880 214, 800 214 C 755 214, 712 250, 676 290 Z")
# Where the hot edge runs over the front (left of the turn it is the band's edge; right of it, the fold).
HOT = LOWER

DEFS = """
  <linearGradient id="steel" gradientUnits="userSpaceOnUse" x1="34" y1="0" x2="1066" y2="0">
    <stop offset="0" stop-color="#2e2f34"/>
    <stop offset="0.14" stop-color="#7d7e85"/>
    <stop offset="0.27" stop-color="#d2d3d8"/>
    <stop offset="0.34" stop-color="#a9aab0"/>
    <stop offset="0.45" stop-color="#5c5d63"/>
    <stop offset="0.53" stop-color="#9a5a3a"/>
    <stop offset="0.60" stop-color="#ff7a2e"/>
    <stop offset="0.68" stop-color="#ff8c34"/>
    <stop offset="0.72" stop-color="#ffb04a"/>
    <stop offset="0.78" stop-color="#ff6a22"/>
    <stop offset="0.90" stop-color="#e8481a"/>
    <stop offset="1" stop-color="#ff7a18"/>
  </linearGradient>
  <!-- Light from above: the top of the band lit, down toward the hot edge in shadow. -->
  <linearGradient id="light" gradientUnits="userSpaceOnUse" x1="0" y1="80" x2="0" y2="430">
    <stop offset="0" stop-color="#ffffff" stop-opacity="0.22"/>
    <stop offset="0.35" stop-color="#ffffff" stop-opacity="0"/>
    <stop offset="1" stop-color="#140400" stop-opacity="0.55"/>
  </linearGradient>
  <linearGradient id="underside" gradientUnits="userSpaceOnUse" x1="680" y1="260" x2="1066" y2="430">
    <stop offset="0" stop-color="#5a1406"/>
    <stop offset="0.35" stop-color="#b8361a"/>
    <stop offset="0.75" stop-color="#ff6020"/>
    <stop offset="1" stop-color="#ff9a24"/>
  </linearGradient>
  <linearGradient id="hotline" gradientUnits="userSpaceOnUse" x1="34" y1="0" x2="1066" y2="0">
    <stop offset="0" stop-color="#ff7a3a" stop-opacity="0.2"/>
    <stop offset="0.18" stop-color="#ff8a3a"/>
    <stop offset="0.5" stop-color="#ffd27a"/>
    <stop offset="0.75" stop-color="#ffe9a8"/>
    <stop offset="1" stop-color="#ffb347"/>
  </linearGradient>
  <linearGradient id="glowline" gradientUnits="userSpaceOnUse" x1="34" y1="0" x2="1066" y2="0">
    <stop offset="0" stop-color="#ff5a14" stop-opacity="0.1"/>
    <stop offset="0.28" stop-color="#ff5a14" stop-opacity="0.75"/>
    <stop offset="1" stop-color="#ff5a14"/>
  </linearGradient>
  <filter id="halo" x="-20%" y="-40%" width="140%" height="180%"><feGaussianBlur stdDeviation="26"/></filter>
  <filter id="glow" x="-10%" y="-30%" width="120%" height="160%"><feGaussianBlur stdDeviation="9"/></filter>
  <filter id="soft" x="-5%" y="-10%" width="110%" height="120%"><feGaussianBlur stdDeviation="1.6"/></filter>
"""


def ribbon(transform=""):
    """The mark's drawing, in its 1126 x 520 frame, moved by `transform`."""
    return f"""<g transform="{transform}">
  <path d="{FRONT}" fill="#ff5a1a" opacity="0.32" filter="url(#halo)"/>
  <path d="{BACK}" fill="url(#underside)"/>
  <path d="{FRONT}" fill="url(#steel)"/>
  <path d="{FRONT}" fill="url(#light)"/>
  <path d="{HOT}" fill="none" stroke="url(#glowline)" stroke-width="22" opacity="0.85" filter="url(#glow)"/>
  <path d="{HOT}" fill="none" stroke="url(#hotline)" stroke-width="7" opacity="0.8" filter="url(#soft)"/>
  <path d="{HOT}" fill="none" stroke="url(#hotline)" stroke-width="3"/>
  <path d="{TOP}" fill="none" stroke="#ffffff" stroke-opacity="0.5" stroke-width="2"/>
</g>"""


def svg(width, height, body, extra_defs=""):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" width="{width}" height="{height}">\n'
            f"<defs>{DEFS}{extra_defs}</defs>\n{body}\n</svg>\n")


# ---- the words: wide, geometric capitals drawn as strokes (no font needed to show them)

def letters(text, x, y, height, weight, color, spacing, wide=1.0):
    """Capitals of the engine's lettering, `height` tall, from (x, y) at their top left. Returns
    the drawing and where the text ends."""
    w = height * 0.92 * wide            # a letter's width
    shapes = {
        "M": lambda: f"M0,{height} L0,0 L{w/2},{height*0.62} L{w},0 L{w},{height}",
        "O": None,
        "L": lambda: f"M0,0 L0,{height} L{w*0.8},{height}",
        "T": lambda: f"M0,0 L{w},0 M{w/2},0 L{w/2},{height}",
        "E": lambda: f"M{w*0.82},0 L0,0 L0,{height} L{w*0.82},{height} M0,{height/2} L{w*0.72},{height/2}",
        "N": lambda: f"M0,{height} L0,0 L{w*0.9},{height} L{w*0.9},0",
        "G": None,
        "I": lambda: f"M0,0 L0,{height}",
    }
    advance = {"M": w, "O": w * 1.04, "L": w * 0.66, "T": w, "E": w * 0.82, "N": w * 0.9, "G": w * 1.0, "I": 0}
    out = []
    at = x
    common = f'fill="none" stroke="{color}" stroke-width="{weight}" stroke-linecap="butt" stroke-linejoin="miter" stroke-miterlimit="1.6"'
    for c in text:
        if c == " ":
            at += w * 0.5 + spacing
            continue
        if c == "O":
            r = advance["O"] / 2
            out.append(f'<rect x="{at:.1f}" y="{y}" width="{advance["O"]:.1f}" height="{height}" rx="{min(r, height/2):.1f}" fill="none" stroke="{color}" stroke-width="{weight}"/>')
        elif c == "G":
            g = advance["G"]
            rx, ry = g / 2, height / 2
            # An O open at the right, its bar going in.
            out.append(f'<path transform="translate({at:.1f},{y})" d="M{g*0.93:.1f},{height*0.2:.1f} A{rx:.1f},{ry:.1f} 0 1 0 {g:.1f},{height*0.56:.1f} '
                       f'L{g:.1f},{height*0.56:.1f} L{g*0.56:.1f},{height*0.56:.1f}" {common}/>')
        else:
            out.append(f'<path transform="translate({at:.1f},{y})" d="{shapes[c]()}" {common}/>')
        at += advance[c] + spacing
    return "\n".join(out), at - spacing


def wordmark(x, y, scale=1.0, ink=("url(#word)", "#b4b4bc", "#8e8e96")):
    """MOLTEN in wide, heavy capitals; ENGINE spread under it to the same width; what it is made of
    under that, spread the same way."""
    big, end = letters("MOLTEN", x, y, 128 * scale, 19 * scale, ink[0], 30 * scale, wide=1.5)
    # ENGINE: thin letters whose spacing makes the word as wide as MOLTEN.
    height = 44 * scale
    w = height * 0.92
    natural = w * 0.82 + w * 0.9 + w * 1.0 + 0 + w * 0.9 + w * 0.82   # E N G I N E
    spacing = (end - x - natural) / 5
    small, _ = letters("ENGINE", x + 2 * scale, y + 184 * scale, height, 5 * scale, ink[1], spacing)
    # METAL • C++ • iOS: five pieces from one end to the other.
    pieces = [("METAL", ink[2], "start"), ("\u2022", "#ff6a1a", "middle"), ("C++", ink[2], "middle"),
              ("\u2022", "#ff6a1a", "middle"), ("iOS", ink[2], "end")]
    tag = []
    for i, (text, color, anchor) in enumerate(pieces):
        px = x + (end - x) * i / 4
        tag.append(f'<text x="{px:.1f}" y="{y + 306 * scale:.1f}" text-anchor="{anchor}" font-family="Helvetica Neue, Helvetica, Arial, sans-serif" '
                   f'font-size="{40 * scale:.1f}" letter-spacing="{14 * scale:.1f}" fill="{color}">{text}</text>')
    return big + "\n" + small + "\n" + "\n".join(tag), end


WORD_DEFS = """
  <linearGradient id="word" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#ffffff"/><stop offset="1" stop-color="#c4c4cc"/>
  </linearGradient>
  <linearGradient id="wordDark" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#2a2b31"/><stop offset="1" stop-color="#16171b"/>
  </linearGradient>
  <radialGradient id="tile" cx="0.5" cy="0.3" r="0.8">
    <stop offset="0" stop-color="#2e3038"/><stop offset="1" stop-color="#121318"/>
  </radialGradient>
  <radialGradient id="ember" cx="0.55" cy="0.62" r="0.5">
    <stop offset="0" stop-color="#ff5a1a" stop-opacity="0.22"/><stop offset="1" stop-color="#ff5a1a" stop-opacity="0"/>
  </radialGradient>
"""


def mark_svg():
    return svg(1126, 520, ribbon())


def logo_svg(on_light=False):
    # The mark, a line, the words: 2400 x 536. Light words for a dark page; dark ones for a light page.
    ink = ("url(#wordDark)", "#4a4b53", "#6a6b73") if on_light else ("url(#word)", "#b4b4bc", "#8e8e96")
    words, end = wordmark(1010, 104, 1.0, ink)
    line = "#000000" if on_light else "#ffffff"
    body = (ribbon("translate(40,40) scale(0.80)") +
            f'\n<line x1="930" y1="70" x2="930" y2="466" stroke="{line}" stroke-opacity="0.16" stroke-width="2"/>\n' + words)
    return svg(2400, 536, body, WORD_DEFS)


def icon_svg():
    # A dark tile with an ember's warmth, the mark across it: what the Dock and the App Store show.
    body = ('<rect x="100" y="100" width="824" height="824" rx="185" ry="185" fill="url(#tile)"/>\n'
            '<rect x="100" y="100" width="824" height="824" rx="185" ry="185" fill="url(#ember)"/>\n'
            '<rect x="101" y="101" width="822" height="822" rx="184" ry="184" fill="none" stroke="#ffffff" stroke-opacity="0.10" stroke-width="2"/>\n'
            + ribbon("translate(118,330) scale(0.70)"))
    return svg(1024, 1024, body, WORD_DEFS)


def square_mark_svg():
    # The mark alone, centered in a square, for where a square picture is wanted.
    return svg(1024, 1024, ribbon("translate(28,370) scale(0.86)"))


def render(svg_text, out, width, height=None):
    with tempfile.NamedTemporaryFile("w", suffix=".svg", delete=False) as f:
        f.write(svg_text)
        name = f.name
    try:
        args = ["rsvg-convert", "-w", str(width)] + (["-h", str(height)] if height else []) + ["-o", out, name]
        subprocess.run(args, check=True)
    finally:
        os.unlink(name)


def main():
    if not shutil.which("rsvg-convert"):
        sys.exit("rsvg-convert is needed (brew install librsvg)")
    branding = os.path.join(ROOT, "Assets", "Branding")
    website = os.path.join(ROOT, "Website")
    os.makedirs(branding, exist_ok=True)
    # The drawings themselves, for anyone who wants the logo at another size.
    for name, text in [("molten-mark.svg", mark_svg()), ("molten-logo.svg", logo_svg()), ("molten-icon.svg", icon_svg())]:
        with open(os.path.join(branding, name), "w") as f:
            f.write(text)
    render(mark_svg(), os.path.join(branding, "mark-hd.png"), 2252, 1040)
    render(square_mark_svg(), os.path.join(branding, "mark.png"), 1024, 1024)
    render(logo_svg(), os.path.join(branding, "logo.png"), 2400, 536)
    render(icon_svg(), os.path.join(branding, "icon.png"), 1024, 1024)
    if os.path.isdir(website):
        images = os.path.join(website, "images")
        os.makedirs(images, exist_ok=True)
        render(icon_svg(), os.path.join(images, "icon.png"), 512, 512)
        render(mark_svg(), os.path.join(images, "mark.png"), 2252, 1040)   # shown at 1126 x 520: sharp on a Retina screen
        render(icon_svg(), os.path.join(images, "favicon.png"), 64, 64)
        render(icon_svg(), os.path.join(website, "favicon.png"), 64, 64)
    print("branding written to", branding)


if __name__ == "__main__":
    main()
