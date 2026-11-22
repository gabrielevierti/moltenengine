// Materials in the editor. A material is an asset: a `.material` file that any number of
// meshes, in any scene, can use, and that is edited in the inspector when it is selected
// (or under the mesh that uses it). What it says first is its shader: which program draws
// the surface, and so which properties it has.
#include "Editor.hpp"
#include "ImGuiLayer.hpp"
#include "Platform.hpp"
#include "SoftRender.hpp"
#include "Rendering/SurfaceShader.hpp"
#include "Core/TextFile.hpp"
#include "Panels/Widgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <optional>
#include <unordered_map>

namespace molten::editor {

namespace {

/// A linear color as the screen shows it.
ImU32 screenColor(Vec3 linear, float brightness = 1.0f) {
    auto channel = [brightness](float value) { return int(std::pow(saturate(value * brightness), 1.0f / 2.2f) * 255.0f + 0.5f); };
    return IM_COL32(channel(linear.x), channel(linear.y), channel(linear.z), 255);
}

}  // namespace

namespace {

Assets* gPreviewAssets = nullptr;

std::unordered_map<std::string, SoftImage>& pictureCache() {
    static std::unordered_map<std::string, SoftImage> all;
    return all;
}
std::unordered_map<uint64_t, std::shared_ptr<Texture>>& ballCache() {
    static std::unordered_map<uint64_t, std::shared_ptr<Texture>> made;
    return made;
}

/// A picture file's pixels, small: a ball the size of a button shows no more than this.
const SoftImage* pictureOf(const std::shared_ptr<Texture>& texture) {
    if (!texture || texture->name.empty() || !gPreviewAssets) return nullptr;
    std::unordered_map<std::string, SoftImage>& all = pictureCache();
    auto found = all.find(texture->name);
    if (found != all.end()) return found->second.isEmpty() ? nullptr : &found->second;
    SoftImage& image = all[texture->name];
    std::vector<uint8_t> pixels;
    int width = 0, height = 0;
    if (Texture::readPixels(gPreviewAssets->path(texture->name), pixels, width, height) && width > 0 && height > 0) {
        // Every `step`th pixel: at most 256 across.
        int step = std::max(1, std::max(width, height) / 256);
        image.width = width / step;
        image.height = height / step;
        image.rgba.resize(size_t(image.width * image.height) * 4);
        for (int y = 0; y < image.height; y++) {
            for (int x = 0; x < image.width; x++) {
                std::memcpy(&image.rgba[size_t(y * image.width + x) * 4], &pixels[(size_t(y * step) * size_t(width) + size_t(x * step)) * 4], 4);
            }
        }
    }
    return image.isEmpty() ? nullptr : &image;
}

/// A number that changes whenever what the ball would look like does.
uint64_t lookOf(const Material& material, int size) {
    uint64_t hash = 1469598103934665603ull;
    auto add = [&](const void* data, size_t bytes) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < bytes; i++) hash = (hash ^ p[i]) * 1099511628211ull;
    };
    add(&material.baseColor, sizeof(float) * 4);
    add(&material.metallic, sizeof(float));
    add(&material.roughness, sizeof(float));
    add(&material.emissive, sizeof(float) * 3);
    add(&material.uvScale, sizeof(float) * 2);
    uint8_t flags = uint8_t(material.isUnlit) | uint8_t(material.isWater) << 1 | uint8_t(material.alphaMode == AlphaMode::Blend) << 2;
    add(&flags, 1);
    for (const std::shared_ptr<Texture>* texture : {&material.baseColorTexture, &material.metallicRoughnessTexture, &material.emissiveTexture, &material.normalTexture}) {
        if (*texture) add((*texture)->name.data(), (*texture)->name.size());
        add("|", 1);
    }
    add(&size, sizeof(size));
    return hash;
}

/// The ball of a material as a texture, made once for each look and size.
std::shared_ptr<Texture> ballOf(const Material& material, int size) {
    std::unordered_map<uint64_t, std::shared_ptr<Texture>>& made = ballCache();
    uint64_t look = lookOf(material, size);
    auto found = made.find(look);
    if (found != made.end()) return found->second;
    // Dragging a slider makes a new look every frame: the old ones are let go in bulk.
    if (made.size() > 160) made.clear();
    BallLook ball;
    ball.baseColor = material.baseColor;
    ball.metallic = material.metallic;
    ball.roughness = material.roughness;
    ball.emissive = material.emissive;
    ball.uvScale = material.uvScale;
    ball.isUnlit = material.isUnlit;
    ball.isWater = material.isWater;
    ball.isSeenThrough = material.alphaMode == AlphaMode::Blend;
    ball.baseColorPicture = pictureOf(material.baseColorTexture);
    ball.metallicRoughnessPicture = pictureOf(material.metallicRoughnessTexture);
    ball.emissivePicture = pictureOf(material.emissiveTexture);
    ball.normalPicture = pictureOf(material.normalTexture);
    SoftImage image = renderBall(ball, size);
    std::shared_ptr<Texture> texture = gPreviewAssets->makeTexture(image.rgba, image.width, image.height);
    made[look] = texture;
    return texture;
}

}  // namespace

void setMaterialPreviewAssets(Assets* assets) { gPreviewAssets = assets; }

void forgetMaterialPreviews() {
    pictureCache().clear();
    ballCache().clear();
}

void drawMaterialPreview(ImDrawList* drawList, ImVec2 center, float radius, const Material& material) {
    // The material on a ball under a sun and a sky, lit by the renderer's own surface model
    // (Editor/SoftRender.cpp), computed once for each look and kept as a picture.
    if (gPreviewAssets) {
        float scale = ImGui::GetIO().DisplayFramebufferScale.x > 0 ? ImGui::GetIO().DisplayFramebufferScale.x : 1.0f;
        // A few sizes only, so that a ball that grows with its panel is not made again at every pixel.
        float wanted = radius * 2 * scale;
        int size = wanted <= 48 ? 48 : wanted <= 96 ? 96 : wanted <= 192 ? 192 : wanted <= 384 ? 384 : 640;
        if (std::shared_ptr<Texture> ball = ballOf(material, size)) {
            drawList->AddImage(ImGuiLayer::textureId(ball.get()), ImVec2(center.x - radius, center.y - radius), ImVec2(center.x + radius, center.y + radius));
            return;
        }
    }
    // No pictures can be made (no graphics device): its color, at least.
    drawList->AddCircleFilled(center, radius, screenColor(Vec3{material.baseColor.x, material.baseColor.y, material.baseColor.z} + material.emissive, 1.0f), 32);
    drawList->AddCircle(center, radius, IM_COL32(0, 0, 0, 90), 32, 1.0f);
}

/// The properties a material's own shader declares, each with the control its kind calls
/// for, and what the compiler said if the shader is wrong.
static bool drawShaderProperties(EditorContext& editor, Material& material) {
    bool changed = false;
    // Read from the file as it is now, so that a property added a moment ago is here.
    static std::unordered_map<std::string, std::pair<std::filesystem::file_time_type, SurfaceShader>> read;
    std::string file = editor.assets.absolutePath(material.shader);
    std::error_code code;
    auto time = std::filesystem::last_write_time(file, code);
    auto& kept = read[material.shader];
    if (kept.second.name.empty() || kept.first != time) {
        std::optional<std::string> contents = text::readFile(file);
        kept.second = SurfaceShader::parse(material.shader, contents ? *contents : std::string());
        if (!contents) kept.second.error = "cannot read " + material.shader;
        kept.first = time;
    }
    const SurfaceShader& shader = kept.second;
    ui::subheading(std::filesystem::path(material.shader).stem().string().c_str());
    ImGui::PushID("shader-properties");
    for (const SurfaceProperty& property : shader.properties) {
        const Vec4* said = material.shaderValue(property.name);
        Vec4 value = said ? *said : property.value;
        bool edited = false;
        if (property.kind == SurfaceProperty::Kind::Float) {
            float number = value.x;
            edited = property.hasRange ? ui::sliderFloat(property.name.c_str(), number, property.min, property.max, ui::kNumberFormat) : ui::dragFloat(property.name.c_str(), number, 0.01f);
            value.x = number;
        } else if (property.kind == SurfaceProperty::Kind::Color) {
            edited = ui::color(property.name.c_str(), value);
        } else {
            Vec3 first{value.x, value.y, value.z};
            edited = ui::dragVec3(property.name.c_str(), first, 0.01f);
            value = Vec4{first.x, first.y, first.z, value.w};
        }
        if (edited) {
            material.setShaderValue(property.name, value);
            changed = true;
        }
    }
    if (shader.properties.empty() && shader.error.empty()) ImGui::TextDisabled("This shader declares no properties.");
    ImGui::PopID();
    if (ImGui::SmallButton("Edit Shader")) platform::openWithDefaultApplication(file);
    ImGui::SameLine();
    if (ImGui::SmallButton("Show in Explorer")) editor.selectedAsset = material.shader;
    // What is wrong with it, from whoever knows: the file's own block, or the compiler.
    std::string wrong = shader.error;
    if (wrong.empty() && !editor.engine.renderer().surfaceShaderError().empty()) wrong = editor.engine.renderer().surfaceShaderError();
    if (!wrong.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
        ImGui::TextWrapped("%s", wrong.c_str());
        ImGui::PopStyleColor();
        ImGui::TextDisabled("Until it compiles, materials that use a shader of the project are drawn as plain lit surfaces.");
    }
    return changed;
}

bool drawMaterialProperties(EditorContext& editor, Material& material) {
    bool changed = false;
    // The engine's three, then every `.shader` file of the project: surface shaders written by hand.
    std::vector<std::string> choices = {"Lit", "Unlit", "Water"};
    std::vector<std::string> files;
    for (const AssetInfo& asset : editor.assets.assets()) {
        if (asset.type == AssetType::Shader && std::filesystem::path(asset.path).extension() == ".shader") files.push_back(asset.path);
    }
    if (!material.shader.empty() && std::find(files.begin(), files.end(), material.shader) == files.end()) files.push_back(material.shader);
    for (const std::string& file : files) choices.push_back(std::filesystem::path(file).stem().string());
    choices.push_back("New Shader...");
    int shader = material.isWater ? 2 : material.isUnlit ? 1 : 0;
    for (size_t i = 0; i < files.size(); i++) {
        if (files[i] == material.shader) shader = int(3 + i);
    }
    if (ui::combo("Shader", shader, choices)) {
        if (shader == int(choices.size()) - 1) {
            // A new one, beside the others, that works as it is: to open and change.
            std::string path = editor.assets.uniquePath("Shaders", "New Shader", ".shader");
            std::error_code ignored;
            std::filesystem::create_directories(std::filesystem::path(editor.assets.absolutePath(path)).parent_path(), ignored);
            if (text::writeFile(editor.assets.absolutePath(path), newSurfaceShaderText())) {
                editor.assets.refresh();
                material.shader = path;
                material.shaderValues.clear();
                editor.notify("Made " + path + ": open it from the Explorer to write it");
            }
            shader = 0;
        } else {
            material.shader = shader >= 3 ? files[size_t(shader - 3)] : std::string();
            if (shader >= 3) shader = 0;        // its own shader starts from a lit surface
        }
        material.isUnlit = shader == 1;
        material.isWater = shader == 2;
        if (material.isWater && material.baseColor.w > 0.95f) {
            // What water starts as: blue-green, and a lake's clarity.
            material.baseColor = Vec4{0.03f, 0.2f, 0.26f, 0.8f};
            material.alphaMode = AlphaMode::Blend;
        }
        changed = true;
    }
    ui::tooltip("Lit: a surface that light falls on, described by color, metal and roughness.\nUnlit: its own color and nothing else, for signs, glows and flat-shaded art.\n"
                "Water: seen into, with waves, the sky mirrored in it and foam where it meets the shore.\n"
                "Below them, the project's own shaders (.shader files): each is given the surface as the settings here make it, and changes it.");
    if (material.isWater) {
        ui::subheading("Water");
        Vec3 deep{material.baseColor.x, material.baseColor.y, material.baseColor.z};
        if (ui::color("Color", deep)) {
            material.baseColor = Vec4{deep.x, deep.y, deep.z, material.baseColor.w};
            changed = true;
        }
        ui::tooltip("The color of deep water: what is left of white light after a long way through it.\nShallow water is clear, and takes this color the deeper it gets.");
        float murk = material.baseColor.w;
        if (ui::sliderFloat("Murkiness", murk, 0.05f, 1.0f)) {
            material.baseColor.w = murk;
            changed = true;
        }
        ui::tooltip("How soon the bottom is lost from sight: 0.3 a clear sea that shows it ten meters down, 0.8 a lake, 1 a pond.");
        ui::subheading("Waves");
        changed |= ui::dragFloat("Size", material.waveScale, 0.01f, 0.02f, 20.0f);
        ui::tooltip("Larger numbers make smaller waves");
        changed |= ui::sliderFloat("Height", material.waveHeight, 0.0f, 4.0f);
    }
    if (!material.shader.empty()) changed |= drawShaderProperties(editor, material);

    if (material.isWater) return changed;          // water is its color, its murkiness and its waves: nothing else to set
    {
        ui::subheading("Surface");
        changed |= ui::color("Base Color", material.baseColor);
        if (!material.isUnlit) {
            changed |= ui::sliderFloat("Metallic", material.metallic, 0.0f, 1.0f);
            changed |= ui::sliderFloat("Roughness", material.roughness, 0.0f, 1.0f);
        }
        changed |= ui::color("Emissive", material.emissive, true);
        changed |= ui::dragVec2("Texture Repeat", material.uvScale, 0.01f);
        changed |= ui::dragFloat("Sway", material.sway, 0.005f, 0.0f, 3.0f);
        ui::tooltip("Moved by the scene's wind (Rendering > Wind), the more the higher above the model's base: 1 for grass, 0.1 for a tree. 0: still");
    }
    {
        ui::subheading("Textures");
        changed |= textureSlot(editor, "Base Color", material.baseColorTexture, TextureFormat::RGBA8_sRGB);
        if (!material.isUnlit) {
            changed |= textureSlot(editor, "Normal", material.normalTexture, TextureFormat::RGBA8);
            if (material.normalTexture) changed |= ui::sliderFloat("Normal Strength", material.normalScale, 0.0f, 2.0f);
            changed |= textureSlot(editor, "Metal & Rough", material.metallicRoughnessTexture, TextureFormat::RGBA8);
            changed |= textureSlot(editor, "Occlusion", material.occlusionTexture, TextureFormat::RGBA8);
            if (material.occlusionTexture) changed |= ui::sliderFloat("Occlusion Strength", material.occlusionStrength, 0.0f, 1.0f);
        }
        changed |= textureSlot(editor, "Emissive", material.emissiveTexture, TextureFormat::RGBA8_sRGB);
    }
    {
        ui::subheading("Drawing");
        int mode = int(material.alphaMode);
        if (ui::combo("Transparency", mode, {"Opaque", "Cut Out", "Blended"})) {
            material.alphaMode = AlphaMode(mode);
            changed = true;
        }
        if (material.alphaMode == AlphaMode::Mask) changed |= ui::sliderFloat("Cut Below", material.alphaCutoff, 0.0f, 1.0f);
        changed |= ui::checkbox("Two Sided", material.isDoubleSided);
    }
    return changed;
}

bool saveMaterialAsset(EditorContext& editor, const Material& material) {
    if (material.assetName.empty()) return false;
    return text::writeFile(editor.assets.absolutePath(material.assetName), writeMaterial(material));
}

std::shared_ptr<Material> createMaterialAsset(EditorContext& editor, const std::string& folder, const std::string& name, const Material* from) {
    namespace fs = std::filesystem;
    std::error_code code;
    if (!folder.empty()) fs::create_directories(editor.assets.absolutePath(folder), code);
    std::string path = editor.assets.uniquePath(folder, name.empty() ? "Material" : name, ".material");
    auto material = std::make_shared<Material>(from ? *from : Material{});
    material->assetName = path;
    if (!text::writeFile(editor.assets.absolutePath(path), writeMaterial(*material))) {
        editor.notify("Cannot write " + path, ConsoleMessage::Level::Error);
        return nullptr;
    }
    editor.engine.assets().adoptMaterial(path, material);
    editor.assets.refresh();
    return material;
}

std::string materialDisplayName(EditorContext& editor, const std::shared_ptr<Material>& material) {
    if (!material) return "None";
    if (!material->assetName.empty()) return std::filesystem::path(material->assetName).stem().string();
    int index = editor.scene.materialIndex(material);
    return index >= 0 ? editor.scene.materials[size_t(index)].name : std::string("Material");
}

bool drawMaterialSlot(EditorContext& editor, std::shared_ptr<Material>& slot) {
    bool changed = false;
    ui::label("Material");
    std::string name = materialDisplayName(editor, slot);
    float height = ImGui::GetFrameHeight();
    ImVec2 min = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    ImGui::PushID("material-slot");
    if (ImGui::Button("##slot", ImVec2(width, height))) ImGui::OpenPopup("##choose");
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (slot) drawMaterialPreview(drawList, ImVec2(min.x + height * 0.5f + 2, min.y + height * 0.5f), height * 0.5f - 3, *slot);
    drawList->PushClipRect(min, ImVec2(min.x + width - 18, min.y + height), true);
    drawList->AddText(ImVec2(min.x + height + 6, min.y + (height - ImGui::GetFontSize()) * 0.5f), ImGui::GetColorU32(slot ? ImGuiCol_Text : ImGuiCol_TextDisabled), name.c_str());
    drawList->PopClipRect();
    ImVec2 arrow(min.x + width - 12, min.y + height * 0.5f);
    drawList->AddTriangleFilled(ImVec2(arrow.x - 4, arrow.y - 2), ImVec2(arrow.x + 4, arrow.y - 2), ImVec2(arrow.x, arrow.y + 3), ImGui::GetColorU32(ImGuiCol_TextDisabled));

    // A material dragged from the assets goes in.
    std::string dropped;
    if (ui::acceptAsset(editor.assets, {AssetType::Material}, dropped)) {
        if (std::shared_ptr<Material> material = editor.engine.assets().material(dropped)) {
            slot = material;
            changed = true;
        }
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(std::max(width, 220.0f), 0), ImVec2(FLT_MAX, 380));
    if (ImGui::BeginPopup("##choose")) {
        auto row = [&](const std::string& label, const std::shared_ptr<Material>& material, const char* id) {
            ImGui::PushID(id);
            ImVec2 at = ImGui::GetCursorScreenPos();
            float rowHeight = ImGui::GetTextLineHeight() + 8;
            bool picked = ImGui::Selectable("##pick", material == slot, ImGuiSelectableFlags_None, ImVec2(0, rowHeight));
            ImDrawList* list = ImGui::GetWindowDrawList();
            if (material) drawMaterialPreview(list, ImVec2(at.x + rowHeight * 0.5f, at.y + rowHeight * 0.5f), rowHeight * 0.5f - 2, *material);
            list->AddText(ImVec2(at.x + rowHeight + 6, at.y + 4), ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
            ImGui::PopID();
            return picked;
        };
        bool any = false;
        for (const AssetInfo& asset : editor.assets.assets()) {
            if (asset.type != AssetType::Material) continue;
            if (!any) ImGui::TextDisabled("ASSETS");
            any = true;
            std::shared_ptr<Material> material = editor.engine.assets().material(asset.path);
            if (material && row(std::filesystem::path(asset.path).stem().string(), material, asset.path.c_str())) {
                slot = material;
                changed = true;
            }
        }
        if (!editor.scene.materials.empty()) {
            if (any) ImGui::Spacing();
            ImGui::TextDisabled("ONLY IN THIS SCENE");
            for (size_t i = 0; i < editor.scene.materials.size(); i++) {
                const NamedMaterial& named = editor.scene.materials[i];
                if (!named.material || !named.material->assetName.empty()) continue;
                if (row(named.name, named.material, ("scene" + std::to_string(i)).c_str())) {
                    slot = named.material;
                    changed = true;
                }
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("New Material")) {
            if (std::shared_ptr<Material> material = createMaterialAsset(editor, "Materials", "Material", nullptr)) {
                slot = material;
                changed = true;
            }
        }
        if (slot && slot->assetName.empty() && ImGui::MenuItem("Save This One as an Asset")) {
            // The same object becomes the asset: everything in the scene that uses it keeps using it.
            std::string wanted = materialDisplayName(editor, slot);
            namespace fs = std::filesystem;
            std::error_code code;
            fs::create_directories(editor.assets.absolutePath("Materials"), code);
            std::string path = editor.assets.uniquePath("Materials", wanted, ".material");
            slot->assetName = path;
            if (saveMaterialAsset(editor, *slot)) {
                editor.engine.assets().adoptMaterial(path, slot);
                auto& list = editor.scene.materials;
                list.erase(std::remove_if(list.begin(), list.end(), [&](const NamedMaterial& named) { return named.material == slot; }), list.end());
                editor.assets.refresh();
                changed = true;
            } else {
                slot->assetName.clear();
                editor.notify("Cannot write " + path, ConsoleMessage::Level::Error);
            }
        }
        if (slot && !slot->assetName.empty() && ImGui::MenuItem("Show in Assets")) {
            editor.currentFolder = std::filesystem::path(slot->assetName).parent_path().string();
            editor.selectedAsset = slot->assetName;
            editor.scene.clearSelection();
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

}  // namespace molten::editor
