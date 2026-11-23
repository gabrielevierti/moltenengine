#!/usr/bin/env python3
"""Makes the editor's icon font from Lucide (https://lucide.dev, ISC license).

The editor draws every icon as a letter of one font, so they are sharp at any size and share one
line weight. Only the icons listed below go into the font; to add one, put its Lucide name in
ICONS and run this again:

    npm pack lucide-static && tar xzf lucide-static-*.tgz
    python3 Tools/make_icon_font.py package

It writes Assets/Fonts/Icons.ttf (with its license beside it) and Editor/Icons.hpp, which names
each icon for the code: icon::Move, icon::Grid3x3...
"""
import json
import os
import re
import subprocess
import sys

ICONS = """
activity anchor aperture app-window apple arrow-down arrow-left arrow-right arrow-up arrow-up-down atom
audio-lines badge-check bell blocks bolt book-open box box-select boxes braces brush bug building
cable camera car check chevron-down chevron-left chevron-right chevron-up circle circle-alert circle-check
circle-dashed circle-dot circle-help circle-x clapperboard clock cloud cloud-lightning cloud-rain cloud-sun
code coins columns-2 component cone container copy cpu crosshair cuboid cylinder diamond download droplet
droplets ellipsis ellipsis-vertical eraser expand external-link eye eye-off file file-code file-code-2 film
filter flag flame flashlight focus folder folder-open folder-tree footprints frame gamepad-2 gantt-chart gauge
gem git-branch globe grid-3x3 hammer hand hard-drive headphones heart hexagon history house image import
info keyboard lamp-ceiling landmark laptop layers layout-dashboard layout-template lightbulb link list-filter
list-tree lock lock-open magnet maximize-2 memory-stick message-square mic minimize-2 minus monitor moon
mountain mouse-pointer-2 move move-3d music navigation network octagon-alert orbit package paintbrush palette
panel-bottom panel-left panel-right panels-top-left pause pencil person-standing pipette plane play plus
puzzle pyramid radio rectangle-horizontal redo-2 refresh-cw rocket rotate-3d rotate-cw route rows-2 sailboat
save scale scale-3d scaling scan scan-eye search settings settings-2 shapes share-2 shield ship skull
sliders-horizontal smartphone snowflake sparkle sparkles speaker spline sprout square square-dashed
square-stack star step-forward sticker sun sun-medium sun-snow sunset swords tag target tent terminal
terminal-square thermometer timer torus trash-2 tree-pine trees triangle triangle-alert trophy type undo-2
upload user video view volume-2 volume-x wand-sparkles waves weight wifi wind workflow wrench x zap
"""


def identifier(name):
    """grid-3x3 -> Grid3x3"""
    return ''.join(part[:1].upper() + part[1:] for part in re.split(r'[-_]', name))


def main():
    package = sys.argv[1] if len(sys.argv) > 1 else 'package'
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    codepoints = json.load(open(os.path.join(package, 'font', 'codepoints.json')))
    names = ICONS.split()
    missing = [n for n in names if n not in codepoints]
    if missing:
        sys.exit('not in Lucide: ' + ', '.join(missing))

    font = os.path.join(root, 'Assets', 'Fonts', 'Icons.ttf')
    unicodes = ','.join('U+%04X' % codepoints[n] for n in names)
    subprocess.run(['pyftsubset', os.path.join(package, 'font', 'lucide.ttf'), '--unicodes=' + unicodes,
                    '--output-file=' + font, '--no-hinting', '--layout-features='], check=True)
    with open(os.path.join(package, 'LICENSE')) as source, open(os.path.join(root, 'Assets', 'Fonts', 'Icons-LICENSE.txt'), 'w') as out:
        out.write('The editor\'s icons are Lucide icons (https://lucide.dev).\n\n' + source.read())

    lines = [
        '// Icons.hpp',
        '// The editor\'s icons: letters of Assets/Fonts/Icons.ttf (Lucide, ISC license), as UTF-8 text.',
        '// Made by Tools/make_icon_font.py; do not change by hand: add the icon there and run it again.',
        '//',
        '// Put one in a label ("Lighting" with its icon before it: std::string(icon::Sun) + "  Lighting"),',
        '// or draw one at any size with ui::drawIcon (Panels/Widgets.hpp).',
        '#pragma once',
        '',
        '#include <string_view>',
        '',
        'namespace molten::editor::icon {',
        '',
    ]
    def utf8(cp):
        return ''.join('\\x%02x' % b for b in chr(cp).encode('utf-8'))
    for name in names:
        lines.append('inline constexpr const char* %s = "%s";' % (identifier(name), utf8(codepoints[name])))
    lines += [
        '',
        '/// An icon by its Lucide name ("grid-3x3"); nullptr if it is not in the font.',
        'inline const char* named(std::string_view name) {',
        '    struct Entry { std::string_view name; const char* glyph; };',
        '    static constexpr Entry all[] = {',
    ]
    for name in names:
        lines.append('        {"%s", %s},' % (name, identifier(name)))
    lines += [
        '    };',
        '    for (const Entry& entry : all) {',
        '        if (entry.name == name) return entry.glyph;',
        '    }',
        '    return nullptr;',
        '}',
        '',
        '}  // namespace molten::editor::icon',
        '',
    ]
    with open(os.path.join(root, 'Editor', 'Icons.hpp'), 'w') as out:
        out.write('\n'.join(lines))
    print('%d icons -> %s (%d bytes)' % (len(names), font, os.path.getsize(font)))


if __name__ == '__main__':
    main()
