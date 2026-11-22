// ModelFormats.hpp
// The kinds of model file other than glTF, read into the same GltfModel (nodes, meshes in parts,
// materials and their pictures; for FBX also skeletons and animations), so that everything that
// takes a model takes them all: the Explorer, the scene, mesh colliders, the Animator.
//
//   FBX (.fbx) and Wavefront OBJ with its .mtl (.obj): read by ufbx (github.com/ufbx/ufbx, MIT).
//   Collada (.dae): its geometry, the tree of nodes and their places, materials' colors and pictures.
//   STL (.stl, text or binary) and PLY (.ply, text or binary): one mesh each, no materials.
//
// Every one is brought to the engine's way: meters, Y up, triangles counter-clockwise from
// outside, texture coordinates from the top left. The model's import settings (scale, Z up...)
// are applied on top, as for any model.
#pragma once

#include "Assets/Gltf.hpp"

#include <optional>
#include <string>

namespace molten {

std::optional<GltfModel> loadWithUfbx(const std::string& path, std::string* error = nullptr);
std::optional<GltfModel> loadCollada(const std::string& path, std::string* error = nullptr);
std::optional<GltfModel> loadStl(const std::string& path, std::string* error = nullptr);
std::optional<GltfModel> loadPly(const std::string& path, std::string* error = nullptr);

/// From text or bytes already in memory (tests, and files read some other way).
std::optional<GltfModel> parseCollada(const std::string& text, std::string* error = nullptr);
std::optional<GltfModel> parseStl(const std::string& bytes, std::string* error = nullptr);
std::optional<GltfModel> parsePly(const std::string& bytes, std::string* error = nullptr);

}  // namespace molten
