// SceneFile.hpp
// Scenes as text files: what an editor saves and a game loads.
//
// The format is one statement per line, made to be read by people and compared by git:
//
//     molten-scene 1
//     material "Stone"
//       baseColor 1 1 1 1
//       roughness 0.8
//       baseColorTexture "Textures/tiles_basecolor.png"
//     entity "Crate"
//       transform 0 2 0  0 0 0 1  1 1 1        position, rotation (a quaternion), scale
//       mesh "cube" 0                          mesh name, index of its material in this file
//       collider box 0.5 0.5 0.5  0 0 0  0
//       body dynamic 1 0.1 0.6 0.05 1
//
// Meshes and textures are stored by name; the file never contains their data. Lines a
// reader does not know are skipped, so files from a newer version still load.
#pragma once

#include "Physics/Physics.hpp"
#include "Rendering/Material.hpp"
#include "Rendering/Particles.hpp"
#include "Rendering/Renderer.hpp"
#include "Scene/Components.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace molten {

/// A material with the name it has in the editor and in scene files.
struct NamedMaterial {
    std::string name;
    std::shared_ptr<Material> material;
};

/// Marks an entity that belongs to a tool and not to the scene (an editor's own camera):
/// it is never written to a file.
struct EditorOnly {};

/// How the names in a file become assets. Usually both lead to Assets (see sceneAssets());
/// either may be empty, and then meshes or textures are simply left out.
struct SceneAssets {
    std::function<std::shared_ptr<Mesh>(const std::string&)> mesh;
    std::function<std::shared_ptr<Texture>(const std::string&, TextureFormat)> texture;
};

class Assets;
SceneAssets sceneAssets(Assets& assets);

/// The whole scene as text. Materials that are used by an entity but missing from
/// `materials` are written too, under a made-up name.
std::string writeScene(Scene& scene, const std::vector<NamedMaterial>& materials, const RenderSettings& settings);

/// Adds what the text describes to `scene` (clear it first to replace it) and replaces
/// `materials`. Returns false, and logs why, if the text is not a scene file at all.
bool readScene(const std::string& text, Scene& scene, std::vector<NamedMaterial>& materials, RenderSettings& settings,
               const SceneAssets& assets);

bool saveSceneFile(const std::string& path, Scene& scene, const std::vector<NamedMaterial>& materials, const RenderSettings& settings);
bool loadSceneFile(const std::string& path, Scene& scene, std::vector<NamedMaterial>& materials, RenderSettings& settings,
                   const SceneAssets& assets);

}  // namespace molten
