#include "Assets/Assets.hpp"

#include "Audio/Audio.hpp"
#include "Core/Log.hpp"
#include "Scene/SceneFile.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>

namespace molten {

namespace {

std::optional<std::string> readTextFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

}  // namespace

Assets::Assets(GraphicsDevice& device, std::string directory) : device_(device), directory_(std::move(directory)) {}

std::string Assets::path(const std::string& name) const {
    if (!projectDirectory_.empty()) {
        std::string inProject = projectDirectory_ + "/" + name;
        std::error_code ignored;
        // A file that exists in neither place belongs to the project: that is where new files go.
        if (std::filesystem::exists(inProject, ignored) || !std::filesystem::exists(directory_ + "/" + name, ignored)) return inProject;
    }
    return directory_ + "/" + name;
}

void Assets::setProjectDirectory(std::string directory) {
    projectDirectory_ = std::move(directory);
    reload();
}

void Assets::reload(const std::string& name) {
    if (name.empty()) {
        textures_.clear();
        meshes_.clear();
        sounds_.clear();
        return;                         // fonts stay: the engine's own font is in use by the canvas
    }
    meshes_.erase(name);
    sounds_.erase(name);
    for (auto it = textures_.begin(); it != textures_.end();) {
        // Texture keys are "name|format".
        it = it->first.compare(0, name.size() + 1, name + "|") == 0 ? textures_.erase(it) : std::next(it);
    }
}

std::shared_ptr<Texture> Assets::texture(const std::string& name, TextureFormat format) {
    // The same image may be wanted both as color and as data: those are different textures.
    std::string key = name + "|" + std::to_string(int(format));
    auto found = textures_.find(key);
    if (found != textures_.end()) return found->second;
    std::shared_ptr<Texture> texture = Texture::load(device_, path(name), format);
    if (texture) texture->name = name;
    textures_[key] = texture;           // a failed load is remembered too, so it is reported once
    return texture;
}

std::shared_ptr<Mesh> Assets::mesh(const std::string& name) {
    if (name == "cube") return cube();
    if (name == "sphere") return sphere();
    if (name == "plane") return plane();
    if (name == "cylinder") return cylinder();
    auto found = meshes_.find(name);
    if (found != meshes_.end()) return found->second;
    std::shared_ptr<Mesh> mesh;
    if (std::optional<std::string> text = readTextFile(path(name))) {
        if (std::optional<MeshData> data = MeshData::parseOBJ(*text)) {
            mesh = std::make_shared<Mesh>(device_, *data);
            mesh->name = name;
        } else {
            logError("no triangles in model %s", name.c_str());
        }
    } else {
        logError("cannot read model %s", path(name).c_str());
    }
    meshes_[name] = mesh;
    return mesh;
}

std::shared_ptr<Font> Assets::font(const std::string& name) {
    auto found = fonts_.find(name);
    if (found != fonts_.end()) return found->second;
    std::shared_ptr<Font> font;
    std::optional<std::string> text = readTextFile(path(name + ".txt"));
    std::optional<FontMetrics> metrics = text ? FontMetrics::parse(*text) : std::nullopt;
    // One channel is enough for a distance field, and it must not be mipmapped:
    // a blurred distance is no longer a distance.
    std::shared_ptr<Texture> atlas = metrics ? Texture::load(device_, path(name + ".png"), TextureFormat::R8, false) : nullptr;
    if (metrics && atlas) {
        font = std::make_shared<Font>(Font{*metrics, atlas});
    } else {
        logError("cannot load font %s (needs %s.png and %s.txt)", name.c_str(), name.c_str(), name.c_str());
    }
    fonts_[name] = font;
    return font;
}

std::shared_ptr<AudioClip> Assets::sound(const std::string& name) {
    auto found = sounds_.find(name);
    if (found != sounds_.end()) return found->second;
    std::shared_ptr<AudioClip> clip = Audio::loadClip(path(name));
    sounds_[name] = clip;
    return clip;
}

SceneAssets sceneAssets(Assets& assets) {
    SceneAssets result;
    result.mesh = [&assets](const std::string& name) { return assets.mesh(name); };
    result.texture = [&assets](const std::string& name, TextureFormat format) { return assets.texture(name, format); };
    return result;
}

std::shared_ptr<Mesh> Assets::makeMesh(const MeshData& data) {
    return std::make_shared<Mesh>(device_, data);
}

std::shared_ptr<Texture> Assets::makeTexture(const std::vector<uint8_t>& pixels, int width, int height, TextureFormat format) {
    if (width <= 0 || height <= 0 || pixels.size() < size_t(width) * size_t(height) * (format == TextureFormat::R8 ? 1 : 4)) return nullptr;
    return Texture::fromPixels(device_, pixels.data(), width, height, format, true);
}

std::shared_ptr<Material> Assets::makeMaterial(Vec4 baseColor, float metallic, float roughness) {
    auto material = std::make_shared<Material>();
    material->baseColor = baseColor;
    material->metallic = metallic;
    material->roughness = roughness;
    return material;
}

std::shared_ptr<Mesh> Assets::cube() {
    if (!cube_) {
        cube_ = makeMesh(MeshData::cube());
        cube_->name = "cube";
    }
    return cube_;
}

std::shared_ptr<Mesh> Assets::sphere() {
    if (!sphere_) {
        sphere_ = makeMesh(MeshData::sphere(0.5f, 48, 24));
        sphere_->name = "sphere";
    }
    return sphere_;
}

std::shared_ptr<Mesh> Assets::plane() {
    if (!plane_) {
        plane_ = makeMesh(MeshData::plane());
        plane_->name = "plane";
    }
    return plane_;
}

std::shared_ptr<Mesh> Assets::cylinder() {
    if (!cylinder_) {
        cylinder_ = makeMesh(MeshData::cylinder());
        cylinder_->name = "cylinder";
    }
    return cylinder_;
}

std::vector<std::string> Assets::listFiles(const std::string& folder, const std::string& extension) const {
    std::vector<std::string> names;
    for (const std::string& root : {projectDirectory_, directory_}) {
        if (root.empty()) continue;
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(root + "/" + folder, error)) {
            std::string file = entry.path().filename().string();
            bool matches = file.size() >= extension.size() && file.compare(file.size() - extension.size(), extension.size(), extension) == 0;
            if (entry.is_regular_file() && matches) names.push_back(folder + "/" + file);
        }
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

}  // namespace molten
