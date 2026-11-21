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
#include <optional>
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

/// A copy of a prefab placed in a scene, by reference: the scene keeps only this entity (its
/// name, its place, the prefab's file); what the prefab holds is put under it each time the
/// scene is read, so that a change to the prefab shows in every scene that has it. Prefabs
/// may hold such copies of other prefabs.
/// A value of a prefab's copy that is not the prefab's: changed on this copy only, kept in the
/// scene as `override "Crate/Lid" MeshRenderer material "Materials/Red.material"`. The path is
/// of names below the copy (its prefab's root first); the value as a variant's `set` takes it.
struct PrefabOverride {
    std::string path, component, field, value;
};

struct PrefabInstance {
    std::string prefab;                 // "Prefabs/Wheel.prefab"
    std::vector<PrefabOverride> overrides;
    /// Sets (or replaces) one; false if nothing changed.
    bool setOverride(const PrefabOverride& change);
    bool removeOverride(const std::string& path, const std::string& component, const std::string& field);
    const PrefabOverride* findOverride(const std::string& path, const std::string& component, const std::string& field) const;
};

/// Changes one value of an entity found by its path below `root`, as variants and prefab
/// overrides do. False if there is no such entity, component or field.
bool applyPrefabValue(Scene& scene, Entity root, const PrefabOverride& change, const struct SceneAssets& assets);
/// The entity found by a path of names below `root` ("Body/Hat"; "" is root itself); invalid if none.
Entity entityAtPath(Scene& scene, Entity root, const std::string& path);
/// The path of names from `root` down to `entity` ("" for root itself); empty and false if it is not below it.
bool pathBelow(const Scene& scene, Entity root, Entity entity, std::string& path);
/// Marks what a PrefabInstance put under itself: it comes from the prefab, and is not written.
struct FromPrefab {};

/// A prefab variant: a prefab made from another (its base) with some values changed, as text:
///
///   molten-variant 1
///   base "Prefabs/Enemy.prefab"
///   name "Fast Enemy"                                    the root's name
///   set "" NavAgent speed "6"                            on the root
///   set "Body" MeshRenderer material "Materials/Red.material"
///   set "Body/Hat" Transform scale "1.5 1.5 1.5"
///
/// An entity is named by the path of names below the root. Transform takes position, rotation
/// (degrees about x, y and z) and scale; MeshRenderer takes material; any component a game or the
/// kit declares takes its fields by name.
bool isPrefabVariant(const std::string& text);
/// How the names in a file become assets. Usually both lead to Assets (see sceneAssets());
/// either may be empty, and then meshes or textures are simply left out.
struct SceneAssets {
    std::function<std::shared_ptr<Mesh>(const std::string&)> mesh;
    std::function<std::shared_ptr<Texture>(const std::string&, TextureFormat)> texture;
    /// A material that is an asset of its own, by its file's name in Assets.
    std::function<std::shared_ptr<Material>(const std::string&)> material;
    /// A prefab's text, by its file's name in Assets: for prefabs inside prefabs, and variants.
    std::function<std::optional<std::string>(const std::string&)> prefabText;
};

/// A material as the text of a `.material` file, and back. `texture` loads the textures
/// the file names; without it they are left out.
std::string writeMaterial(const Material& material);
bool readMaterial(const std::string& text, Material& material,
                  const std::function<std::shared_ptr<Texture>(const std::string&, TextureFormat)>& texture = nullptr);

class Assets;
SceneAssets sceneAssets(Assets& assets);

/// The whole scene as text. Materials that are used by an entity but missing from
/// `materials` are written too, under a made-up name.
///
/// With `only`, just those entities are written (a parent outside the list is forgotten),
/// with the materials they use and without the scene's settings: this is how a prefab
/// is made of part of a scene.
std::string writeScene(Scene& scene, const std::vector<NamedMaterial>& materials, const RenderSettings& settings,
                       const std::vector<Entity>* only = nullptr);

/// Adds what the text describes to `scene` (clear it first to replace it) and replaces
/// `materials`. Returns false, and logs why, if the text is not a scene file at all.
/// `created`, if given, receives the entities that were added, in file order.
bool readScene(const std::string& text, Scene& scene, std::vector<NamedMaterial>& materials, RenderSettings& settings,
               const SceneAssets& assets, std::vector<Entity>* created = nullptr);

bool saveSceneFile(const std::string& path, Scene& scene, const std::vector<NamedMaterial>& materials, const RenderSettings& settings);
bool loadSceneFile(const std::string& path, Scene& scene, std::vector<NamedMaterial>& materials, RenderSettings& settings,
                   const SceneAssets& assets);

}  // namespace molten
