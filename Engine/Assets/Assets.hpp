// Assets.hpp
// Loads textures, meshes, fonts and sounds from the Assets folder, and remembers them:
// asking for the same file twice gives back the same object, already loaded.
//
// Everything is handed out as a std::shared_ptr. An asset stays alive for as long as
// something uses it (a material, a component, the cache here).
#pragma once

#include "Assets/Font.hpp"
#include "Audio/AudioClip.hpp"
#include "Rendering/Material.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace molten {

/// A font ready to draw with: the glyph table and the atlas texture.
struct Font {
    FontMetrics metrics;
    std::shared_ptr<Texture> atlas;
};

class Assets {
public:
    Assets(GraphicsDevice& device, std::string directory);

    /// The full path of a file in the Assets folder. With a project open, the project's
    /// Assets folder is looked in first and the engine's own second, so a project can use
    /// the built-in font and textures and also replace them.
    std::string path(const std::string& name) const;

    /// The Assets folder of the open project; empty for none. Forgets everything loaded
    /// from files, since the same name may now mean another file.
    void setProjectDirectory(std::string directory);
    const std::string& projectDirectory() const { return projectDirectory_; }
    /// The engine's own Assets folder.
    const std::string& engineDirectory() const { return directory_; }

    /// Forgets one file (or, with an empty name, every file) so that the next request
    /// reads it from disk again: what an editor calls when a file changes.
    void reload(const std::string& name = {});

    // ---- from files, by name relative to the Assets folder

    /// An image. Use RGBA8_sRGB for colors (the default) and RGBA8 for data such as normal maps.
    std::shared_ptr<Texture> texture(const std::string& name, TextureFormat format = TextureFormat::RGBA8_sRGB);
    /// A Wavefront OBJ model, or one of the built-in shapes: "cube", "sphere", "plane", "cylinder".
    std::shared_ptr<Mesh> mesh(const std::string& name);
    /// A material that is a file of its own (a `.material`), shared by everything that
    /// names it: change it and every mesh that uses it changes. Null if there is no such file.
    std::shared_ptr<Material> material(const std::string& name);
    /// Makes `material` the asset `name` from now on (the editor, when a material is saved
    /// as a file for the first time).
    void adoptMaterial(const std::string& name, std::shared_ptr<Material> material);
    /// A font baked by Tools/bake_font.py: `name`.png and `name`.txt.
    std::shared_ptr<Font> font(const std::string& name);
    /// A WAV, FLAC or MP3 file.
    std::shared_ptr<AudioClip> sound(const std::string& name);

    // ---- made in code

    std::shared_ptr<Mesh> makeMesh(const MeshData& data);
    /// A texture from pixels computed by the program: four bytes each (red, green, blue,
    /// alpha), row by row from the top. For noise, gradients, anything drawn by code.
    std::shared_ptr<Texture> makeTexture(const std::vector<uint8_t>& pixels, int width, int height,
                                         TextureFormat format = TextureFormat::RGBA8_sRGB);
    std::shared_ptr<Material> makeMaterial(Vec4 baseColor, float metallic = 0.0f, float roughness = 0.6f);

    /// Shared unit shapes: a cube with sides of 1, a sphere and a cylinder of diameter 1,
    /// a 1 x 1 plane facing up. Scale them with the entity's Transform.
    std::shared_ptr<Mesh> cube();
    std::shared_ptr<Mesh> sphere();
    std::shared_ptr<Mesh> plane();
    std::shared_ptr<Mesh> cylinder();

    /// The files in a folder of Assets whose names end with `extension`, sorted, as names
    /// that texture() and mesh() accept: listFiles("Textures", ".png").
    std::vector<std::string> listFiles(const std::string& folder, const std::string& extension) const;

private:
    GraphicsDevice& device_;
    std::string directory_;
    std::string projectDirectory_;
    std::unordered_map<std::string, std::shared_ptr<Texture>> textures_;
    std::unordered_map<std::string, std::shared_ptr<Mesh>> meshes_;
    std::unordered_map<std::string, std::shared_ptr<struct GltfModel>> models_;     // glTF files read, for their parts
    std::unordered_map<std::string, std::shared_ptr<Material>> materials_;
    std::unordered_map<std::string, std::shared_ptr<Font>> fonts_;
    std::unordered_map<std::string, std::shared_ptr<AudioClip>> sounds_;
    std::shared_ptr<Mesh> cube_, sphere_, plane_, cylinder_;
};

}  // namespace molten
