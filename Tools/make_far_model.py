#!/usr/bin/env python3
"""Writes the far version of a glTF model: the same meshes, in the same order and with the
same materials, with a small share of their triangles. What a LOD Group draws from far away.

    python3 Tools/make_far_model.py <model.glb> <far.glb> [share] [smallest piece, in meters]

`share` is the part of the triangles kept (0.04: four in a hundred), never fewer than 40 a
mesh. Each new corner takes its texture coordinates from the nearest corner of the original,
so the far model wears the same textures. Needs pyfqmr and scipy (pip install pyfqmr scipy)."""
import json, struct, sys

import numpy as np
import pyfqmr
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import connected_components
from scipy.spatial import cKDTree

COMPONENTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}
TYPES = {5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}


def read_glb(path):
    data = open(path, "rb").read()
    length = struct.unpack("<I", data[12:16])[0]
    gltf = json.loads(data[20:20 + length])
    rest = 20 + length
    binary = b""
    if rest < len(data):
        chunk_length = struct.unpack("<I", data[rest:rest + 4])[0]
        binary = data[rest + 8:rest + 8 + chunk_length]
    return gltf, binary


def accessor(gltf, binary, index):
    a = gltf["accessors"][index]
    view = gltf["bufferViews"][a["bufferView"]]
    start = view.get("byteOffset", 0) + a.get("byteOffset", 0)
    kind = TYPES[a["componentType"]]
    width = COMPONENTS[a["type"]]
    values = np.frombuffer(binary, dtype=kind, count=a["count"] * width, offset=start)
    return values.reshape(a["count"], width) if width > 1 else values


def main():
    source, target = sys.argv[1], sys.argv[2]
    share = float(sys.argv[3]) if len(sys.argv) > 3 else 0.04
    min_size = float(sys.argv[4]) if len(sys.argv) > 4 else 0.04        # meters: smaller pieces are left out
    gltf, binary = read_glb(source)
    out = bytearray()
    accessors, views = [], []

    def add(values, kind, component_type, target_kind=None):
        nonlocal out
        while len(out) % 4:
            out += b"\0"
        raw = np.ascontiguousarray(values).tobytes()
        view = {"buffer": 0, "byteOffset": len(out), "byteLength": len(raw)}
        if target_kind:
            view["target"] = target_kind
        views.append(view)
        out += raw
        item = {"bufferView": len(views) - 1, "componentType": component_type, "count": int(len(values)), "type": kind}
        if kind == "VEC3":
            item["min"] = [float(v) for v in values.min(axis=0)]
            item["max"] = [float(v) for v in values.max(axis=0)]
        accessors.append(item)
        return len(accessors) - 1

    before = after = 0
    meshes = []
    for mesh in gltf["meshes"]:
        primitives = []
        for primitive in mesh["primitives"]:
            attributes = primitive["attributes"]
            positions = accessor(gltf, binary, attributes["POSITION"]).astype(np.float64)
            indices = accessor(gltf, binary, primitive["indices"]).astype(np.int64).reshape(-1, 3)
            before += len(indices)
            wanted = max(40, int(len(indices) * share))
            # Corners at the same place made one: a model has copies of a corner along every
            # seam of its textures and every hard edge, and each copy is a border that would hold.
            _, first, welded = np.unique(np.round(positions, 4), axis=0, return_index=True, return_inverse=True)
            welded = welded.reshape(-1)
            corners, faces = positions[first], welded[indices]
            faces = faces[(faces[:, 0] != faces[:, 1]) & (faces[:, 1] != faces[:, 2]) & (faces[:, 0] != faces[:, 2])]
            # Pieces too small to see from far away (bolts, screws, the small parts of a badge)
            # go: each is a closed shape the simplifier cannot make smaller than a few triangles.
            pieces = len(corners)
            graph = coo_matrix((np.ones(len(faces) * 3), (np.repeat(faces[:, 0], 3), faces.reshape(-1))), shape=(pieces, pieces))
            count, island = connected_components(graph, directed=False)
            if count > 1:
                low = np.full((count, 3), np.inf)
                high = np.full((count, 3), -np.inf)
                np.minimum.at(low, island, corners)
                np.maximum.at(high, island, corners)
                size = np.linalg.norm(high - low, axis=1)
                keep = size[island[faces[:, 0]]] >= min_size
                if keep.sum() >= 4:
                    faces = faces[keep]
            simplifier = pyfqmr.Simplify()
            simplifier.setMesh(corners, faces)
            simplifier.simplify_mesh(target_count=wanted, aggressiveness=7, preserve_border=False, verbose=False)
            new_positions, new_faces, new_normals = simplifier.getMesh()
            if len(new_faces) > wanted * 2:
                # Some shapes hold (the simplifier will not fold a triangle over): then the
                # corners are gathered on a grid, each cell's into one, finer until it is enough.
                low, high = corners.min(axis=0), corners.max(axis=0)
                cell = float(np.linalg.norm(high - low)) / 400.0
                best = (new_positions, new_faces)
                for _ in range(24):
                    keys = np.floor((corners - low) / cell).astype(np.int64)
                    _, first_of_cell, which = np.unique(keys, axis=0, return_index=True, return_inverse=True)
                    which = which.reshape(-1)
                    kept = which[faces][(which[faces][:, 0] != which[faces][:, 1]) & (which[faces][:, 1] != which[faces][:, 2]) & (which[faces][:, 0] != which[faces][:, 2])]
                    _, unique_rows = np.unique(np.sort(kept, axis=1), axis=0, return_index=True)
                    kept = kept[np.sort(unique_rows)]
                    # Each cell's corner where the corners in it are, on average.
                    sums = np.zeros((len(first_of_cell), 3))
                    np.add.at(sums, which, corners)
                    counts = np.bincount(which, minlength=len(first_of_cell))[:, None]
                    best = (sums / np.maximum(counts, 1), kept)
                    if len(kept) <= wanted * 1.5:
                        break
                    cell *= 1.35
                new_positions, new_faces = best
            if len(new_faces) == 0:
                new_positions, new_faces = positions, indices
                new_normals = accessor(gltf, binary, attributes["NORMAL"]).astype(np.float64) if "NORMAL" in attributes else np.zeros_like(positions)
            after += len(new_faces)
            # Normals smooth over each corner, from the faces around it (the simplifier's are per face).
            normals = np.zeros_like(new_positions)
            a, b, c = new_positions[new_faces[:, 0]], new_positions[new_faces[:, 1]], new_positions[new_faces[:, 2]]
            face = np.cross(b - a, c - a)
            for k in range(3):
                np.add.at(normals, new_faces[:, k], face)
            lengths = np.linalg.norm(normals, axis=1, keepdims=True)
            normals = np.where(lengths > 1e-12, normals / np.maximum(lengths, 1e-12), np.array([0.0, 1.0, 0.0]))
            built = {"POSITION": add(new_positions.astype(np.float32), "VEC3", 5126, 34962),
                     "NORMAL": add(normals.astype(np.float32), "VEC3", 5126, 34962)}
            if "TEXCOORD_0" in attributes:
                uv = accessor(gltf, binary, attributes["TEXCOORD_0"])
                _, nearest = cKDTree(positions).query(new_positions)
                built["TEXCOORD_0"] = add(np.asarray(uv)[nearest].astype(np.float32), "VEC2", 5126, 34962)
            made = {"attributes": built, "indices": add(new_faces.astype(np.uint32).reshape(-1), "SCALAR", 5125, 34963)}
            if "material" in primitive:
                made["material"] = primitive["material"]
            primitives.append(made)
        meshes.append({"name": mesh.get("name", "") + " (far)", "primitives": primitives})

    far = dict(gltf)
    far["meshes"] = meshes
    far["accessors"] = accessors
    far["bufferViews"] = views
    while len(out) % 4:
        out += b"\0"
    far["buffers"] = [{"byteLength": len(out)}]
    text = json.dumps(far, separators=(",", ":")).encode()
    while len(text) % 4:
        text += b" "
    total = 12 + 8 + len(text) + 8 + len(out)
    with open(target, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(text), 0x4E4F534A) + text)
        f.write(struct.pack("<II", len(out), 0x004E4942) + bytes(out))
    print("far model: %d triangles, from %d (%.1f%%), in %s" % (after, before, 100.0 * after / max(before, 1), target))


if __name__ == "__main__":
    main()
