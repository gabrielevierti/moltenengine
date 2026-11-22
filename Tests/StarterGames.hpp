// StarterGames.hpp
// The small games a new project can start from (Templates/Car Game, Plane Game, Boat Game,
// Third Person Game, Shooter Game and Online Game), made here, in code, and written as projects.
//
// Each is put together only from what the engine has (the kit's vehicles, its interface
// pieces, When/Then for its rules), with no code of its own to build: whoever starts from one
// can play it the moment it opens, and take it apart in the Inspector. Their shapes are made
// here too, as models of a few dozen flat faces, and written beside the scene.
//
// They are made by the tests because the tests are what plays them: each game is built,
// written to a folder, read back as the player would read it and driven with the keys a
// person would press (Tests/StarterGames.cpp). To write them into the engine's Templates
// folder, which is where the editor's New Project page finds them:
//
//     MOLTEN_WRITE_TEMPLATES=Templates build/moltentests starter
//
// (Their sounds are made by Tools/make_sounds.py and are not written from here: they are
// kept if they are already in the folder.)
#pragma once

#include "Core/World.hpp"
#include "Scene/SceneFile.hpp"

#include <map>
#include <string>
#include <vector>

namespace starter {

using namespace molten;

/// A shape of flat faces, as an OBJ file's text.
struct Model {
    std::vector<Vec3> points;
    std::vector<std::vector<int>> faces;        // each counter-clockwise, seen from outside

    int point(Vec3 p) {
        points.push_back(p);
        return int(points.size()) - 1;
    }
    void face(std::vector<int> corners) { faces.push_back(std::move(corners)); }
    /// A box between two corners.
    void box(Vec3 low, Vec3 high);
    /// A skin over cross-sections, each a loop of as many points (counter-clockwise seen from
    /// the first section's side, looking at the second), with both ends closed.
    void loft(const std::vector<std::vector<Vec3>>& sections);
    /// A ring about the z axis: `radius` to the middle of its tube, `tube` the tube's own.
    void ring(float radius, float tube, int around, int across);
    /// Another model's faces, moved.
    void add(const Model& other, Vec3 offset = Vec3{0, 0, 0});
    /// How much room it encloses: above zero when its faces look outward.
    float volume() const;
    std::string obj() const;
};

/// One starter game, built: its world, and the files it needs beside its scene.
struct Game {
    std::string folder;                         // "Car Game": its folder in Templates, and its name
    std::string description;                    // a line or two for the New Project page
    World world;
    std::vector<NamedMaterial> materials;       // each a file of its own in Assets/Materials
    std::map<std::string, std::string> models;  // "Models/Hull.obj" and its text
    std::vector<std::string> sounds;            // the files of Assets/Audio it plays
    std::string inputActions;                   // Assets/Input.actions, for a game that needs more than the usual keys; empty: the usual ones
    std::map<std::string, std::vector<uint8_t>> files;      // other files of Assets, as their bytes: "Models/Figure.glb"
    std::map<std::string, std::string> texts;   // and as text: "Animations/Player.animator"
    std::map<std::string, std::string> pictures;            // a material's name and the picture of its color: "Figure", "Models/Figure.png"

    std::shared_ptr<Material> material(const std::string& name, Vec3 color, float roughness = 0.7f, float metallic = 0.0f, Vec3 emissive = Vec3{0, 0, 0});
    /// An entity under `parent` (none: at the top).
    Entity add(const std::string& name, Entity parent = Entity{}, Vec3 position = Vec3{0, 0, 0}, Quat rotation = Quat{}, Vec3 scale = Vec3{1, 1, 1});
    /// One that is drawn: `mesh` is "cube", "sphere", "plane", "cylinder" or a model of `models`.
    Entity shape(const std::string& name, Entity parent, const std::string& mesh, const std::shared_ptr<Material>& look, Vec3 position = Vec3{0, 0, 0},
                 Vec3 scale = Vec3{1, 1, 1}, Quat rotation = Quat{}, bool castsShadows = true);
    /// Keeps a model under a name, and gives the name back.
    std::string model(const std::string& name, const Model& made);
    /// What has this look lies on another surface in the same place: a road on the grass (1), the
    /// line on the road (2), a stripe along a hull (1). It is set a little apart once the game is
    /// built, so that the depth buffer has no tie to break: lifted a centimeter a level where it is
    /// flat, grown a few millimeters each way where it is not.
    void laidOver(const std::shared_ptr<Material>& look, int level) { laid[look.get()] = level; }
    std::map<const Material*, int> laid;
};

/// Their names: "Car Game", "Plane Game", "Boat Game", "Third Person Game", "Shooter Game", "Online Game".
const std::vector<std::string>& names();
/// Builds one into `game`. False for a name that is none of them.
bool build(const std::string& name, Game& game);
/// Writes a built game as a project in `directory` (made if it is not there): its project
/// file, scene, materials, models and template.txt. Sounds already in its Assets/Audio stay.
bool write(Game& game, const std::string& directory, std::string* error = nullptr);

}  // namespace starter
