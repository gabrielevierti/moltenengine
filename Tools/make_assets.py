#!/usr/bin/env python3
"""Generates the textures and the model in Assets/. Everything is made by code, so the
repository needs no art that somebody else owns.

    python3 Tools/make_assets.py

Needs Pillow and NumPy:  pip install pillow numpy

The textures follow the glTF conventions the engine's materials expect:
  *_basecolor.png   sRGB color, alpha is opacity
  *_normal.png      tangent-space normal map, green points to the top of the image
  *_orm.png         occlusion in red, roughness in green, metalness in blue (linear data)
"""
import math
import os

import numpy as np
from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Assets")
SIZE = 512


def save(name, array):
    path = os.path.join(ROOT, name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    Image.fromarray(np.clip(np.round(array * 255), 0, 255).astype(np.uint8)).save(path, optimize=True)
    print("wrote", os.path.relpath(path, os.path.join(ROOT, "..")))


def grid():
    """u and v for every texel: 0...1, v growing downward."""
    axis = (np.arange(SIZE) + 0.5) / SIZE
    return np.meshgrid(axis, axis)


def smoothstep(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0, 1)
    return t * t * (3 - 2 * t)


def value_noise(cells, seed):
    """Smooth random values that tile seamlessly: `cells` random cells across the image."""
    rng = np.random.default_rng(seed)
    lattice = rng.random((cells, cells))
    u, v = grid()
    x, y = u * cells, v * cells
    x0, y0 = np.floor(x).astype(int) % cells, np.floor(y).astype(int) % cells
    x1, y1 = (x0 + 1) % cells, (y0 + 1) % cells
    fx, fy = smoothstep(0, 1, x - np.floor(x)), smoothstep(0, 1, y - np.floor(y))
    top = lattice[y0, x0] * (1 - fx) + lattice[y0, x1] * fx
    bottom = lattice[y1, x0] * (1 - fx) + lattice[y1, x1] * fx
    return top * (1 - fy) + bottom * fy


def normal_map(height, strength):
    """A normal map from a height field (which must tile). Green is up."""
    dx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * 0.5 * SIZE
    dy = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * 0.5 * SIZE       # along v, which points down
    # The surface z = height(x, y) has the normal (-dh/dx, -dh/dy, 1). With y up and v down, dh/dy = -dh/dv.
    normal = np.stack([-dx * strength, dy * strength, np.ones_like(height)], axis=-1)
    normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
    return normal * 0.5 + 0.5


def tiles():
    """Square stone tiles with beveled edges and darker joints between them."""
    count = 4
    u, v = grid()
    cell_u, cell_v = u * count - np.floor(u * count), v * count - np.floor(v * count)
    edge = np.minimum(np.minimum(cell_u, 1 - cell_u), np.minimum(cell_v, 1 - cell_v))  # distance to the tile's border
    tile = smoothstep(0.015, 0.06, edge)                                               # 0 in the joint, 1 on the tile
    index = (np.floor(u * count) + np.floor(v * count) * count).astype(int)
    shade = np.random.default_rng(7).uniform(0.86, 1.0, count * count)[index]          # each tile slightly different
    grain = value_noise(64, 1) * 0.5 + value_noise(16, 2) * 0.5

    base = np.array([0.80, 0.73, 0.62])                                                 # a warm, light stone
    color = base * (shade * (0.92 + 0.08 * grain))[..., None]
    color = color * tile[..., None] + np.array([0.20, 0.18, 0.16]) * (1 - tile[..., None])
    save("Textures/tiles_basecolor.png", color)

    height = tile * 0.02 + grain * 0.0015
    save("Textures/tiles_normal.png", normal_map(height, 6.0))

    occlusion = 0.55 + 0.45 * tile
    roughness = (0.38 + 0.2 * grain) * tile + 0.95 * (1 - tile)
    save("Textures/tiles_orm.png", np.stack([occlusion, roughness, np.zeros_like(tile)], axis=-1))


def grate():
    """A metal grate with round holes: the alpha channel cuts the holes out."""
    count = 8
    u, v = grid()
    cell_u, cell_v = u * count - np.floor(u * count) - 0.5, v * count - np.floor(v * count) - 0.5
    distance = np.sqrt(cell_u ** 2 + cell_v ** 2)
    metal = smoothstep(0.30, 0.33, distance)                                           # 0 in the hole, 1 on the metal
    brushed = value_noise(128, 3)
    color = np.array([0.56, 0.57, 0.6]) * (0.9 + 0.1 * brushed)[..., None]
    save("Textures/grate_basecolor.png", np.concatenate([color, metal[..., None]], axis=-1))
    rim = smoothstep(0.33, 0.42, distance)                                             # the edge of each hole is pressed down
    save("Textures/grate_normal.png", normal_map(rim * 0.012, 6.0))
    roughness = 0.32 + 0.15 * brushed
    save("Textures/grate_orm.png", np.stack([np.ones_like(metal), roughness, np.ones_like(metal)], axis=-1))


def checker():
    """The classic prototyping checkerboard, with a thin line every half meter."""
    u, v = grid()
    squares = (np.floor(u * 2) + np.floor(v * 2)) % 2
    lines = np.maximum(np.abs((u * 4) % 1 - 0.5), np.abs((v * 4) % 1 - 0.5)) > 0.485
    shade = np.where(squares > 0, 0.78, 0.62) * np.where(lines, 0.8, 1.0)
    save("Textures/checker_basecolor.png", np.stack([shade, shade, shade], axis=-1))


def gem():
    """A cut gemstone as a Wavefront OBJ file: a flat table on top, a crown, a pointed pavilion.

    Every face has its own vertices and its own normal, which is what makes facets look flat.
    """
    sides = 8
    table_radius, girdle_radius = 0.26, 0.5
    table_y, girdle_y, tip_y = 0.22, 0.05, -0.5

    def ring(radius, y, phase=0.0):
        return [(radius * math.cos(2 * math.pi * (i + phase) / sides), y, radius * math.sin(2 * math.pi * (i + phase) / sides))
                for i in range(sides)]

    table = ring(table_radius, table_y, 0.5)
    girdle = ring(girdle_radius, girdle_y)
    tip = (0.0, tip_y, 0.0)
    faces = [list(reversed(table))]                                    # the table, seen from above
    for i in range(sides):
        j = (i + 1) % sides
        faces.append([girdle[i], table[i], girdle[j]])                 # crown: a triangle standing on the girdle
        faces.append([table[i], table[j], girdle[j]])                  # crown: a triangle hanging from the table
        faces.append([girdle[j], tip, girdle[i]])                      # pavilion
    lines = ["# A gemstone. Generated by Tools/make_assets.py.", "o Gem"]
    positions, normals, uvs, face_lines = [], [], [], []
    for face in faces:
        a, b, c = (np.array(p) for p in face[:3])
        normal = np.cross(b - a, c - a)
        normal /= np.linalg.norm(normal)
        center = np.mean(np.array(face), axis=0)
        if np.dot(normal, center - np.array([0, -0.1, 0])) < 0:        # make every face counter-clockwise seen from outside
            face = list(reversed(face))
            normal = -normal
        normals.append(normal)
        indices = []
        for p in face:
            positions.append(p)
            uvs.append((0.5 + p[0], 0.5 + p[2]))
            indices.append(f"{len(positions)}/{len(uvs)}/{len(normals)}")
        face_lines.append("f " + " ".join(indices))
    lines += [f"v {p[0]:.5f} {p[1]:.5f} {p[2]:.5f}" for p in positions]
    lines += [f"vt {t[0]:.5f} {t[1]:.5f}" for t in uvs]
    lines += [f"vn {n[0]:.5f} {n[1]:.5f} {n[2]:.5f}" for n in normals]
    lines += face_lines
    path = os.path.join(ROOT, "Models", "gem.obj")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as file:
        file.write("\n".join(lines) + "\n")
    print("wrote", os.path.relpath(path, os.path.join(ROOT, "..")), f"({len(faces)} faces)")


if __name__ == "__main__":
    tiles()
    grate()
    checker()
    gem()
