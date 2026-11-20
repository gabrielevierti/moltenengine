#!/usr/bin/env python3
"""Turns the "Fiat 500 x Outlaw" model (by Subin, CC-BY-4.0, from Sketchfab) into what the
Driving example uses: one small .glb with the car in the engine's own terms (meters, Y up,
facing -Z, standing on y = 0 with the middle of its wheelbase at the origin), its parts
joined by material, one wheel on its own around its hub, and its pictures made smaller.

    python3 Tools/import_fiat500.py /path/to/fiat_500_x_outlaw /path/to/Driving/Assets/Models/Fiat500

Writes fiat500.glb, Textures/*.jpg, and layout.json (where the wheels, the lamps and the
exhaust are), which Tools/make_templates.py reads."""
import json, os, struct, sys
import numpy as np
from PIL import Image

SCALE = 0.0028          # the model's units to meters: its wheelbase becomes 1.84 m, the real car's
COMPONENTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
TYPES = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}

def main(source, target):
    gltf = json.load(open(os.path.join(source, "scene.gltf")))
    binary = open(os.path.join(source, gltf["buffers"][0]["uri"]), "rb").read()

    def accessor(index):
        a = gltf["accessors"][index]
        view = gltf["bufferViews"][a["bufferView"]]
        kind, count = np.dtype(TYPES[a["componentType"]]), COMPONENTS[a["type"]]
        start = view.get("byteOffset", 0) + a.get("byteOffset", 0)
        stride = view.get("byteStride", 0) or kind.itemsize * count
        raw = np.frombuffer(binary, dtype=np.uint8, count=stride * (a["count"] - 1) + kind.itemsize * count, offset=start)
        rows = np.lib.stride_tricks.as_strided(raw, shape=(a["count"], kind.itemsize * count), strides=(stride, 1))
        return np.ascontiguousarray(rows).view(kind).reshape(a["count"], count)

    # Where every node is in the model.
    def local(node):
        if "matrix" in node:
            return np.array(node["matrix"], dtype=np.float64).reshape(4, 4).T
        m = np.eye(4)
        if "scale" in node:
            m = np.diag(list(node["scale"]) + [1.0]) @ m
        if "rotation" in node:
            x, y, z, w = node["rotation"]
            r = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 0],
                          [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 0],
                          [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y), 0], [0, 0, 0, 1]])
            m = r @ m
        if "translation" in node:
            t = np.eye(4)
            t[:3, 3] = node["translation"]
            m = t @ m
        return m
    world = {}
    def walk(index, parent):
        world[index] = parent @ local(gltf["nodes"][index])
        for child in gltf["nodes"][index].get("children", []):
            walk(child, world[index])
    for root in gltf["scenes"][gltf.get("scene", 0)]["nodes"]:
        walk(root, np.eye(4))

    # Every primitive, in the model's space.
    pieces = []
    for index, node in enumerate(gltf["nodes"]):
        if "mesh" not in node:
            continue
        for primitive in gltf["meshes"][node["mesh"]]["primitives"]:
            m = world[index]
            positions = accessor(primitive["attributes"]["POSITION"]).astype(np.float64)
            positions = positions @ m[:3, :3].T + m[:3, 3]
            normals = accessor(primitive["attributes"]["NORMAL"]).astype(np.float64) @ np.linalg.inv(m[:3, :3])
            normals /= np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-12)
            uvs = accessor(primitive["attributes"]["TEXCOORD_0"]).astype(np.float64) if "TEXCOORD_0" in primitive["attributes"] else np.zeros((len(positions), 2))
            indices = accessor(primitive["indices"]).astype(np.uint32).reshape(-1)
            if np.linalg.det(m[:3, :3]) < 0:                     # a mirrored node turns its triangles around
                indices = indices.reshape(-1, 3)[:, ::-1].reshape(-1)
            pieces.append(dict(node=node["name"], material=gltf["materials"][primitive["material"]]["name"], p=positions, n=normals, uv=uvs, i=indices))

    def named(prefix):
        return [piece for piece in pieces if piece["node"].startswith(prefix)]
    def origin_of(name):
        return next(world[i][:3, 3] for i, node in enumerate(gltf["nodes"]) if node.get("name") == name)

    front, rear = origin_of("WheelFR"), origin_of("WheelR")
    ground = min(piece["p"][:, 1].min() for piece in named("WheelFR"))
    middle = np.array([0.0, ground, (front[2] + rear[2]) * 0.5])
    def to_car(points, center):
        """The model faces +Z; the engine's cars face -Z: half a turn about Y, then meters."""
        moved = (points - center) * SCALE
        return np.stack([-moved[:, 0], moved[:, 1], -moved[:, 2]], axis=1)
    def turn(normals):
        return np.stack([-normals[:, 0], normals[:, 1], -normals[:, 2]], axis=1)

    # The body: everything that does not turn, joined by material. The two rear wheels and the
    # left front one are left out: one wheel, the front right, is used four times.
    body = {}
    for piece in pieces:
        if piece["node"].startswith(("WheelFR", "WheelFL", "WheelR_")):
            continue
        body.setdefault(piece["material"], []).append(piece)
    parts = []          # (name, material, positions, normals, uvs, indices)
    def joined(name, material, group, center):
        offset, p, n, uv, i = 0, [], [], [], []
        for piece in group:
            p.append(to_car(piece["p"], center)); n.append(turn(piece["n"])); uv.append(piece["uv"]); i.append(piece["i"] + offset)
            offset += len(piece["p"])
        parts.append((name, material, np.concatenate(p), np.concatenate(n), np.concatenate(uv), np.concatenate(i)))
    for material, group in body.items():
        joined("Body " + material, material, group, middle)
    for piece in named("WheelFR"):
        joined("Wheel " + piece["material"], piece["material"], [piece], front)

    # ---- the .glb
    blob, views, accessors, meshes, nodes = bytearray(), [], [], [], []
    def add(array, target_kind=None):
        while len(blob) % 4:
            blob.append(0)
        data = np.ascontiguousarray(array).tobytes()
        views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data), **({"target": target_kind} if target_kind else {})})
        blob.extend(data)
        return len(views) - 1
    materials = sorted({part[1] for part in parts})
    for name, material, p, n, uv, i in parts:
        p32, n32, uv32 = p.astype(np.float32), n.astype(np.float32), uv.astype(np.float32)
        attributes = {}
        for key, array, kind in (("POSITION", p32, "VEC3"), ("NORMAL", n32, "VEC3"), ("TEXCOORD_0", uv32, "VEC2")):
            entry = {"bufferView": add(array, 34962), "componentType": 5126, "count": len(array), "type": kind}
            if key == "POSITION":
                entry["min"], entry["max"] = p32.min(axis=0).tolist(), p32.max(axis=0).tolist()
            accessors.append(entry)
            attributes[key] = len(accessors) - 1
        accessors.append({"bufferView": add(i.astype(np.uint32), 34963), "componentType": 5125, "count": len(i), "type": "SCALAR"})
        meshes.append({"name": name, "primitives": [{"attributes": attributes, "indices": len(accessors) - 1, "material": materials.index(material)}]})
        nodes.append({"name": name, "mesh": len(meshes) - 1})
    document = {"asset": {"version": "2.0", "generator": "Molten Tools/import_fiat500.py",
                          "copyright": "Based on \"Fiat 500 x Outlaw\" by Subin (https://sketchfab.com/subink), CC-BY-4.0"},
                "scene": 0, "scenes": [{"nodes": list(range(len(nodes)))}], "nodes": nodes, "meshes": meshes,
                "materials": [{"name": name} for name in materials], "accessors": accessors, "bufferViews": views, "buffers": [{"byteLength": len(blob)}]}
    text = json.dumps(document, separators=(",", ":")).encode()
    text += b" " * (-len(text) % 4)
    while len(blob) % 4:
        blob.append(0)
    os.makedirs(os.path.join(target, "Textures"), exist_ok=True)
    with open(os.path.join(target, "fiat500.glb"), "wb") as out:
        out.write(struct.pack("<4sII", b"glTF", 2, 12 + 8 + len(text) + 8 + len(blob)))
        out.write(struct.pack("<I4s", len(text), b"JSON") + text)
        out.write(struct.pack("<I4s", len(blob), b"BIN\0") + bytes(blob))

    # ---- the pictures, small enough for a phone, and what each material is made of
    look = {}
    for material in gltf["materials"]:
        if material["name"] not in materials:
            continue
        pbr = material.get("pbrMetallicRoughness", {})
        entry = {"baseColor": pbr.get("baseColorFactor", [1, 1, 1, 1]), "metallic": pbr.get("metallicFactor", 1.0), "roughness": pbr.get("roughnessFactor", 1.0)}
        for key, slot in (("baseColorTexture", pbr.get("baseColorTexture")), ("metallicRoughnessTexture", pbr.get("metallicRoughnessTexture")),
                          ("normalTexture", material.get("normalTexture"))):
            if not slot:
                continue
            uri = gltf["images"][gltf["textures"][slot["index"]]["source"]]["uri"]
            name = os.path.splitext(os.path.basename(uri))[0] + ".jpg"
            picture = Image.open(os.path.join(source, uri)).convert("RGB")
            most = 2048 if material["name"] == "Body_Paint" and key == "baseColorTexture" else 1024
            if max(picture.size) > most:
                picture = picture.resize((most * picture.width // max(picture.size), most * picture.height // max(picture.size)), Image.LANCZOS)
            picture.save(os.path.join(target, "Textures", name), quality=95 if key == "normalTexture" else 88)
            entry[key] = name
        look[material["name"]] = entry

    def car_point(point):
        moved = (np.array(point) - middle) * SCALE
        return [round(float(-moved[0]), 4), round(float(moved[1]), 4), round(float(-moved[2]), 4)]
    def box_of(group):
        low = np.min([piece["p"].min(axis=0) for piece in group], axis=0)
        high = np.max([piece["p"].max(axis=0) for piece in group], axis=0)
        a, b = car_point(low), car_point(high)
        return [[min(a[k], b[k]) for k in range(3)], [max(a[k], b[k]) for k in range(3)]]
    tyre = next(piece for piece in named("WheelFR") if piece["material"] == "Tyre")
    layout = {
        "parts": [{"name": name, "material": material, "triangles": int(len(i) // 3)} for name, material, p, n, uv, i in parts],
        "materials": look,
        "wheelRadius": round(float((tyre["p"][:, 1].max() - tyre["p"][:, 1].min()) * 0.5 * SCALE), 4),
        "wheelWidth": round(float((tyre["p"][:, 0].max() - tyre["p"][:, 0].min()) * SCALE), 4),
        "frontAxle": car_point(front), "rearAxle": car_point(rear),             # of the wheel on the car's left
        "body": box_of([piece for piece in pieces if piece["node"].startswith("CarBody")]),
        "headlamps": box_of([piece for piece in pieces if piece["material"] == "Headlamp_Glass"]),
        "rearLights": box_of([piece for piece in pieces if piece["material"] == "RearLight"]),
        "exhaust": box_of([piece for piece in pieces if piece["material"] == "Fire"]),
        "credit": "This work is based on \"Fiat 500 x Outlaw\" (https://sketchfab.com/3d-models/fiat-500-x-outlaw-f412bdec4b494692b9d5d9c03f4178ad) "
                  "by Subin (https://sketchfab.com/subink) licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/)",
    }
    json.dump(layout, open(os.path.join(target, "layout.json"), "w"), indent=1)
    print("wrote", os.path.join(target, "fiat500.glb"), "%.1f MB," % (os.path.getsize(os.path.join(target, "fiat500.glb")) / 1e6), len(parts), "parts,",
          sum(part["triangles"] for part in layout["parts"]), "triangles")

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
