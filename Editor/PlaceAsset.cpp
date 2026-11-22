// PlaceAsset.cpp
// What an asset dropped into the scene becomes, from the Explorer onto the Scene view or the
// Hierarchy: a model, an entity for each of its parts with its materials made into material
// files (and its pictures found among the project's, or written out of the model); a prefab, a
// copy of it. As Unity does it, so that what was made in Blender or bought on a store is in the
// scene in one move, looking as it did there.
#include "Editor.hpp"
#include "Assets/AssetDatabase.hpp"
#include "Assets/Gltf.hpp"
#include "Core/TextFile.hpp"
#include "Animation/Animator.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>

namespace fs = std::filesystem;

namespace molten::editor {

namespace {

std::string lowered(std::string text) {
    for (char& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

/// A name that can be a file's: what a file system or a scene line would choke on taken out.
std::string fileSafe(std::string name) {
    for (char& c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '"' || c == '*' || c == '?' || c == '<' || c == '>' || c == '|' || c == '#') c = '_';
    }
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
    return name.empty() ? std::string("Material") : name;
}

/// The picture a model's material names, as an asset of the project: the file beside the model
/// where it says, or else a picture of that name anywhere in the project (pictures are often moved
/// into a folder of their own); or one carried inside the model, written out into Textures.
std::string pictureAsset(EditorContext& editor, const std::string& modelPath, const GltfModel& model, const GltfPicture& picture,
                         const std::string& stem) {
    AssetDatabase& assets = editor.assets;
    if (picture.image >= 0 && size_t(picture.image) < model.images.size()) {
        const GltfImage& image = model.images[size_t(picture.image)];
        if (image.bytes.empty()) return {};
        std::string path = "Textures/" + fileSafe(stem + " " + image.name) + image.extension;
        if (!assets.find(path)) {
            std::error_code code;
            fs::create_directories(fs::path(assets.absolutePath("Textures")), code);
            std::ofstream out(assets.absolutePath(path), std::ios::binary);
            out.write(reinterpret_cast<const char*>(image.bytes.data()), std::streamsize(image.bytes.size()));
            if (!out) return {};
        }
        return path;
    }
    if (picture.file.empty()) return {};
    std::string named = picture.file;
    std::replace(named.begin(), named.end(), '\\', '/');
    fs::path folder = fs::path(modelPath).parent_path();
    std::string beside = (folder / named).lexically_normal().generic_string();
    if (assets.find(beside)) return beside;
    std::string name = lowered(fs::path(named).filename().string());
    for (const AssetInfo& asset : assets.assets()) {
        if (asset.type == AssetType::Texture && lowered(fs::path(asset.path).filename().string()) == name) return asset.path;
    }
    return {};
}

/// A picture beside another named as glTF exports name them ("Hull_baseColor.png" beside
/// "Hull_normal.png", "Hull_metallicRoughness.png"): what an OBJ's material does not say but the
/// files do.
std::string siblingOf(EditorContext& editor, const std::string& colorPath, const char* instead) {
    std::string lower = lowered(colorPath);
    size_t at = lower.rfind("_basecolor");
    if (at == std::string::npos) at = lower.rfind("_albedo");
    if (at == std::string::npos) at = lower.rfind("_diffuse");
    if (at == std::string::npos) return {};
    std::string stem = colorPath.substr(0, at);
    for (const char* extension : {".png", ".jpg", ".jpeg"}) {
        std::string candidate = stem + instead + extension;
        if (editor.assets.find(candidate)) return candidate;
    }
    return {};
}

/// The material file for a model's material: made the first time (Materials/<model> <name>.material),
/// kept as it is after that, so that what is changed in it stays changed.
std::string materialAsset(EditorContext& editor, const std::string& modelPath, const GltfModel& model, int index, const std::string& stem) {
    if (index < 0 || size_t(index) >= model.materials.size()) return {};
    const GltfMaterial& material = model.materials[size_t(index)];
    std::string path = "Materials/" + fileSafe(stem + " " + (material.name.empty() ? "Material " + std::to_string(index + 1) : material.name)) + ".material";
    if (editor.assets.find(path)) return path;
    char line[160];
    std::string text = "molten-material 1\nshader lit\n";
    std::snprintf(line, sizeof(line), "baseColor %g %g %g %g\n", material.baseColor.x, material.baseColor.y, material.baseColor.z, material.baseColor.w);
    text += line;
    std::string color = pictureAsset(editor, modelPath, model, material.colorMap, stem);
    std::string normal = pictureAsset(editor, modelPath, model, material.normalMap, stem);
    std::string metalRough = pictureAsset(editor, modelPath, model, material.metalRoughMap, stem);
    std::string glow = pictureAsset(editor, modelPath, model, material.emissiveMap, stem);
    std::string occlusion = pictureAsset(editor, modelPath, model, material.occlusionMap, stem);
    if (!color.empty() && normal.empty()) normal = siblingOf(editor, color, "_normal");
    if (!color.empty() && metalRough.empty()) metalRough = siblingOf(editor, color, "_metallicRoughness");
    if (!color.empty() && glow.empty()) glow = siblingOf(editor, color, "_emissive");
    // With a picture of its metal and roughness, the numbers multiply it: as glTF has them.
    float metallic = metalRough.empty() ? material.metallic : 1.0f, roughness = metalRough.empty() ? material.roughness : 1.0f;
    std::snprintf(line, sizeof(line), "metallic %g\nroughness %g\n", metallic, roughness);
    text += line;
    Vec3 emissive = material.emissive;
    if (!glow.empty() && emissive.x + emissive.y + emissive.z <= 0.0f) emissive = Vec3{1, 1, 1};
    std::snprintf(line, sizeof(line), "emissive %g %g %g\n", emissive.x, emissive.y, emissive.z);
    text += line;
    if (material.isBlended) text += "alpha blend 0.5\n";
    else if (material.isMasked) {
        std::snprintf(line, sizeof(line), "alpha mask %g\n", material.alphaCutoff);
        text += line;
    }
    if (material.isDoubleSided) text += "doubleSided 1\n";
    auto picture = [&](const char* keyword, const std::string& asset) {
        if (!asset.empty()) text += std::string(keyword) + " \"" + asset + "\"\n";
    };
    picture("baseColorTexture", color);
    picture("normalTexture", normal);
    picture("metallicRoughnessTexture", metalRough);
    picture("emissiveTexture", glow);
    picture("occlusionTexture", occlusion);
    std::error_code code;
    fs::create_directories(fs::path(editor.assets.absolutePath("Materials")), code);
    if (!text::writeFile(editor.assets.absolutePath(path), text)) return {};
    return path;
}

}  // namespace

std::vector<std::string> importDropped(EditorContext& editor, const std::vector<std::string>& files, const std::string& folder) {
    std::vector<std::string> imported;
    int failed = 0;
    std::function<void(const fs::path&, const std::string&, int)> bring = [&](const fs::path& source, const std::string& into, int depth) {
        std::error_code code;
        if (fs::is_directory(source, code)) {
            if (depth > 12) return;
            std::string error;
            std::string made = editor.assets.createFolder(into, source.filename().string(), &error);
            if (made.empty()) {
                failed++;
                return;
            }
            for (const fs::directory_entry& entry : fs::directory_iterator(source, code)) {
                std::string name = entry.path().filename().string();
                if (name.empty() || name[0] == '.') continue;            // .DS_Store and the like
                bring(entry.path(), made, depth + 1);
            }
            return;
        }
        std::string error;
        std::string made = editor.assets.importFile(source.string(), into, &error);
        if (made.empty()) failed++;
        else imported.push_back(made);
    };
    for (const std::string& file : files) bring(fs::path(file), folder, 0);
    editor.assets.refresh();
    if (!imported.empty()) {
        editor.selectedAsset = imported.back();
        editor.notify("Imported " + (imported.size() == 1 ? imported[0] : std::to_string(imported.size()) + " files") +
                      (folder.empty() ? std::string() : " into " + folder));
    }
    if (failed > 0) editor.notify("Could not import " + std::to_string(failed) + (failed == 1 ? " file" : " files"), ConsoleMessage::Level::Warning);
    return imported;
}

Vec3 inFrontOfCamera(EditorContext& editor) {
    const SceneDocument& scene = editor.scene;
    return scene.cameraPosition + scene.cameraForward() * std::clamp(scene.orbitDistance, 2.0f, 60.0f);
}

Entity placeModel(EditorContext& editor, const std::string& path, Vec3 position, Entity parent) {
    SceneDocument& document = editor.scene;
    Scene& scene = editor.engine.scene;
    Assets& assets = editor.engine.assets();
    const std::string extension = lowered(fs::path(path).extension().string());
    const std::string stem = fs::path(path).stem().string();
    auto asOneMesh = [&]() {
        Entity made = document.createFromMesh(path, position);
        if (made.isValid() && scene.isAlive(parent)) document.reparent(made, parent);
        return made;
    };
    std::string error;
    std::optional<GltfModel> model = GltfModel::load(assets.path(path), &error);
    if (!model) {
        if (extension == ".obj") return asOneMesh();
        editor.notify("Cannot read " + path + (error.empty() ? std::string() : ": " + error), ConsoleMessage::Level::Error);
        return Entity{};
    }
    std::vector<GltfModel::Part> parts = model->parts();
    bool hasMaterials = false;
    for (const GltfModel::Part& part : parts) hasMaterials = hasMaterials || part.material >= 0;
    // An OBJ with nothing in it but its shape: one mesh, as ever.
    if (extension == ".obj" && parts.size() <= 1 && !hasMaterials) return asOneMesh();
    if (parts.empty()) {
        editor.notify(path + " has nothing to draw", ConsoleMessage::Level::Warning);
        return Entity{};
    }

    // The materials, made into files once (and the pictures they need found or written out).
    const size_t texturesBefore = editor.assets.assets().size();
    std::vector<std::shared_ptr<Material>> looks(model->materials.size());
    for (size_t m = 0; m < model->materials.size(); m++) {
        std::string file = materialAsset(editor, path, *model, int(m), stem);
        if (!file.empty()) editor.assets.refresh();
        if (!file.empty()) looks[m] = assets.material(file);
    }
    if (editor.assets.assets().size() != texturesBefore) editor.assets.refresh();
    auto lookOf = [&](int material) {
        return material >= 0 && size_t(material) < looks.size() && looks[size_t(material)] ? looks[size_t(material)] : document.defaultMaterial();
    };

    // A model with bones moves as a whole: one entity, its animations played by an Animator.
    const bool isWhole = parts.size() == 1 || model->hasSkins();
    std::vector<std::string> meshNames;
    if (isWhole) {
        meshNames.push_back(path);
    } else {
        for (size_t k = 0; k < parts.size(); k++) meshNames.push_back(path + "#" + std::to_string(k));
    }
    // It stands on the point: its lowest part's bottom there.
    float lowest = 1e30f;
    std::vector<std::shared_ptr<Mesh>> meshes;
    for (const std::string& name : meshNames) {
        std::shared_ptr<Mesh> mesh = assets.mesh(name);
        meshes.push_back(mesh);
        if (mesh) lowest = std::min(lowest, mesh->bounds().min.y);
    }
    if (lowest > 1e29f) {
        editor.notify("Cannot load " + path, ConsoleMessage::Level::Error);
        return Entity{};
    }
    Entity root = scene.create(stem);
    Transform place;
    place.position = position - Vec3{0, lowest, 0};
    scene.add(root, place);
    document.adopt(root);
    if (isWhole) {
        if (meshes[0]) scene.add(root, MeshRenderer{meshes[0], lookOf(parts[0].material)});
        if (!model->animations.empty()) scene.add(root, Animator{});
    } else {
        // One entity for each part, named as the model names it, in its place in the model.
        std::vector<std::string> used;
        for (size_t k = 0; k < parts.size(); k++) {
            if (!meshes[k]) continue;
            std::string name = parts[k].name;
            if (name.empty() || std::count(used.begin(), used.end(), name) > 0) {
                int material = parts[k].material;
                name = material >= 0 && size_t(material) < model->materials.size() && !model->materials[size_t(material)].name.empty()
                           ? model->materials[size_t(material)].name
                           : "Part " + std::to_string(k + 1);
            }
            used.push_back(name);
            Entity piece = scene.create(name);
            Transform at;
            at.parent = root;
            scene.add(piece, at);
            scene.add(piece, MeshRenderer{meshes[k], lookOf(parts[k].material)});
            document.adopt(piece);
        }
    }
    scene.updateTransforms();
    if (scene.isAlive(parent)) document.reparent(root, parent);
    document.select(root);
    document.markChanged();
    return root;
}

Entity placeAsset(EditorContext& editor, const std::string& path, Vec3 position, Entity parent) {
    const AssetInfo* asset = editor.assets.find(path);
    if (!asset) return Entity{};
    Entity made;
    if (asset->type == AssetType::Mesh) {
        if (!isModelFile(path)) {
            editor.notify("Molten does not read this kind of model: " + path, ConsoleMessage::Level::Warning);
            return Entity{};
        }
        return placeModel(editor, path, position, parent);
    }
    if (asset->type == AssetType::Prefab) {
        made = editor.scene.instantiatePrefab(path, position);
        if (!made.isValid()) editor.notify("Cannot read " + path, ConsoleMessage::Level::Error);
        else if (editor.engine.scene.isAlive(parent)) editor.scene.reparent(made, parent);
    }
    return made;
}

}  // namespace molten::editor
