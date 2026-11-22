// The viewer: what the asset selected in the Explorer is, shown large. A picture to zoom
// into, a model to turn around, a material on a ball with its settings, a sound to play,
// bend, cut and save, a file of text to read.
#include "Editor.hpp"
#include "Assets/ImportSettings.hpp"
#include "Assets/AssetDependencies.hpp"
#include "Audio/AudioEdit.hpp"
#include "Rendering/Particles.hpp"
#include "Assets/Gltf.hpp"
#include "Assets/MeshData.hpp"
#include "Audio/Audio.hpp"
#include "Core/TextFile.hpp"
#include "Game/Component.hpp"
#include "Script/Convert.hpp"
#include "Panels/Widgets.hpp"
#include "Platform.hpp"
#include "SoftRender.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>

namespace molten::editor {

namespace {

std::string loweredExtension(const std::string& path) {
    std::string extension = std::filesystem::path(path).extension().string();
    for (char& c : extension) c = char(std::tolower(static_cast<unsigned char>(c)));
    return extension;
}

/// Writes a clip as a WAV file of 16-bit samples.
bool writeWav(const std::string& path, const AudioClip& clip) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    auto u32 = [&](uint32_t value) { file.write(reinterpret_cast<const char*>(&value), 4); };
    auto u16 = [&](uint16_t value) { file.write(reinterpret_cast<const char*>(&value), 2); };
    uint32_t bytes = uint32_t(clip.samples.size() * 2);
    file.write("RIFF", 4);
    u32(36 + bytes);
    file.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(uint16_t(clip.channels));
    u32(uint32_t(clip.sampleRate));
    u32(uint32_t(clip.sampleRate * clip.channels * 2));
    u16(uint16_t(clip.channels * 2));
    u16(16);
    file.write("data", 4);
    u32(bytes);
    for (float sample : clip.samples) {
        int16_t value = int16_t(std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
        file.write(reinterpret_cast<const char*>(&value), 2);
    }
    return bool(file);
}

/// The viewer of what is chosen in the Explorer: not a window of its own but the right part of
/// the Explorer, there only while something in it is chosen.
class AssetViewer {
public:
    void draw(EditorContext& editor) {
        const AssetInfo* asset = editor.selectedAsset.empty() ? nullptr : editor.assets.find(editor.selectedAsset);
        if (!asset) return;
        if (asset->isFolder()) {
            ImGui::TextUnformatted(asset->name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("Folder");
            drawCloseButton(editor);
            ImGui::Separator();
            drawUnused(editor);
            return;
        }
        // Opened when it is chosen, and again when its file is saved by another program: a
        // script changed in a code editor is the changed script here. (Not a sound that has
        // changes of its own waiting to be saved.)
        std::error_code ignored;
        auto written = std::filesystem::last_write_time(editor.assets.absolutePath(asset->path), ignored);
        if (shown_ != asset->path || (written != shownTime_ && !isChanged_)) {
            open(editor, *asset);
            shownTime_ = written;
        }
        ImGui::TextUnformatted(asset->name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s   %s", AssetDatabase::typeName(asset->type), ui::byteSize(asset->size).c_str());
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 176);
        if (ImGui::SmallButton("Open Outside")) platform::openWithDefaultApplication(editor.assets.absolutePath(asset->path));
        ui::tooltip("In the program your Mac uses for this kind of file");
        ImGui::SameLine();
        if (ImGui::SmallButton("Finder")) platform::revealInFinder(editor.assets.absolutePath(asset->path));
        drawCloseButton(editor);
        ImGui::Separator();
        // What can be done with it, at the top, for the kinds that do something.
        if (asset->type == AssetType::Scene) {
            if (ImGui::Button("Open Scene")) editor.requestOpenScene(editor.assets.absolutePath(asset->path));
            ImGui::Spacing();
        } else if (asset->type == AssetType::Controller) {
            if (ImGui::Button("Open in Animator")) editor.openPanel("Animator");
            ImGui::Spacing();
        } else if (asset->type == AssetType::Animation) {
            if (ImGui::Button("Open in Timeline")) editor.openPanel("Timeline");
            ui::tooltip("It shows the animations of the entity chosen in the scene: choose the one it was made for");
            ImGui::Spacing();
        } else if (asset->type == AssetType::Timeline) {
            if (ImGui::Button("Open in Timeline")) editor.openPanel("Timeline");
            ImGui::Spacing();
        }
        drawDependencies(editor, *asset);
        drawImportSettings(editor, *asset);

        std::string extension = loweredExtension(asset->path);
        bool isPicture = extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" || extension == ".bmp";
        if (asset->type == AssetType::Texture && isPicture) drawPicture(editor, *asset);
        else if (asset->type == AssetType::Mesh && mesh_) drawModel(editor, *asset);
        else if (asset->type == AssetType::Material) drawMaterial(editor, *asset);
        else if (asset->type == AssetType::Audio && clip_) drawSound(editor, *asset);
        else if (asset->type == AssetType::Graph) drawGraph(editor, *asset);
        else drawText(editor, *asset);
    }

private:
    /// Closes the viewer: nothing of the Explorer chosen any more.
    void drawCloseButton(EditorContext& editor) {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::GetFrameHeight());
        if (ImGui::SmallButton("x")) editor.selectedAsset.clear();
        ui::tooltip("Close the viewer");
    }

    // ---------------------------------------------------------------- what uses what

    /// Read again when the project's files have changed, at most every two seconds.
    const AssetDependencies& dependencies(EditorContext& editor) {
        uint64_t signature = editor.assets.assets().size();
        for (const AssetInfo& asset : editor.assets.assets()) signature = signature * 1099511628211ull + uint64_t(asset.modified) + std::hash<std::string>{}(asset.path);
        double now = ImGui::GetTime();
        if (signature != dependencySignature_ && now - dependenciesRead_ > 2.0) {
            std::vector<std::string> files;
            for (const AssetInfo& asset : editor.assets.assets()) {
                if (!asset.isFolder()) files.push_back(asset.path);
            }
            dependencies_.build(editor.assets.absolutePath(""), files);
            dependencySignature_ = signature;
            dependenciesRead_ = now;
        }
        return dependencies_;
    }

    void listAssets(EditorContext& editor, const std::vector<std::string>& paths) {
        for (const std::string& path : paths) {
            if (ImGui::Selectable(path.c_str(), false)) {
                editor.selectedAsset = path;
                editor.currentFolder = std::filesystem::path(path).parent_path().generic_string();
            }
        }
    }

    void drawDependencies(EditorContext& editor, const AssetInfo& asset) {
        const AssetDependencies& found = dependencies(editor);
        const std::vector<std::string>& users = found.usedBy(asset.path);
        const std::vector<std::string>& uses = found.uses(asset.path);
        char title[96];
        std::snprintf(title, sizeof(title), "Used by %zu, uses %zu###dependencies", users.size(), uses.size());
        if (!ImGui::CollapsingHeader(title)) return;
        if (users.empty()) ImGui::TextDisabled("Nothing names it: a scene, a material or a script.");
        else listAssets(editor, users);
        if (!uses.empty()) {
            ImGui::SeparatorText("It uses");
            listAssets(editor, uses);
        }
        ImGui::Spacing();
    }

    void drawUnused(EditorContext& editor) {
        if (!editor.hasProject) return;
        ImGui::Spacing();
        if (ImGui::Button("Find Assets Nothing Uses")) {
            unused_ = dependencies(editor).unused();
            hasLookedForUnused_ = true;
        }
        if (!hasLookedForUnused_) return;
        if (unused_.empty()) {
            ImGui::TextDisabled("Every picture, model, sound and material is used.");
            return;
        }
        ImGui::TextWrapped("No scene, prefab, material or script names these. Code that builds a name from pieces "
                           "(\"engine_\" + rpm + \".wav\") cannot be read: look before deleting.");
        listAssets(editor, unused_);
    }

    AssetDependencies dependencies_;
    uint64_t dependencySignature_ = 0;
    double dependenciesRead_ = -100;
    std::vector<std::string> unused_;
    bool hasLookedForUnused_ = false;

    void open(EditorContext& editor, const AssetInfo& asset) {
        stop(editor);
        shown_ = asset.path;
        isChanged_ = false;
        undo_.clear();
        mesh_.reset();
        clip_.reset();
        parts_.clear();
        if (job_.valid()) job_.wait();
        modelTriangles_.reset();
        thinTriangles_.reset();
        modelPicture_.reset();
        shownLook_ = {};
        text_.clear();
        shownCodeFor_.clear();
        zoom_ = 1;
        pan_ = ImVec2(0, 0);
        yaw_ = 0.7f;
        pitch_ = 0.35f;
        distance_ = 2.6f;
        std::string path = editor.assets.absolutePath(asset.path);
        if (asset.type == AssetType::Mesh && loweredExtension(asset.path) == ".obj") {
            if (std::optional<std::string> contents = text::readFile(path)) {
                mesh_ = MeshData::parseOBJ(*contents);
                if (mesh_) ImportSettings::load(path).apply(*mesh_);        // shown as the game will have it
                if (mesh_) {
                    auto triangles = std::make_shared<std::vector<SoftTriangle>>();
                    for (size_t t = 0; t + 2 < mesh_->indices.size(); t += 3) {
                        triangles->push_back({mesh_->positions[mesh_->indices[t]], mesh_->positions[mesh_->indices[t + 1]], mesh_->positions[mesh_->indices[t + 2]],
                                              Vec3{0.62f, 0.64f, 0.68f}, 0});
                    }
                    useTriangles(triangles);
                }
                // The parts the file names, and how many faces each has.
                std::string line;
                for (size_t at = 0; at <= contents->size(); at++) {
                    char c = at < contents->size() ? (*contents)[at] : '\n';
                    if (c != '\n') {
                        line += c;
                        continue;
                    }
                    if (line.rfind("o ", 0) == 0 || line.rfind("g ", 0) == 0) parts_.push_back({line.substr(2), 0});
                    else if (line.rfind("f ", 0) == 0) {
                        if (parts_.empty()) parts_.push_back({std::filesystem::path(asset.path).stem().string(), 0});
                        parts_.back().second++;
                    }
                    line.clear();
                }
            }
        } else if (asset.type == AssetType::Mesh && isGltfFile(asset.path)) {
            if (std::optional<GltfModel> model = GltfModel::load(path)) {
                mesh_ = model->assembled(model->restPose());
                ImportSettings::load(path).apply(*mesh_);
                // What is drawn: each part in its material's color, its picture's color mixed in.
                auto triangles = std::make_shared<std::vector<SoftTriangle>>();
                std::vector<Vec3> colors;
                for (const GltfMaterial& material : model->materials) {
                    Vec3 color{material.baseColor.x, material.baseColor.y, material.baseColor.z};
                    if (!material.baseColorTexture.empty()) color = color * averageColorOf((std::filesystem::path(path).parent_path() / material.baseColorTexture).string());
                    colors.push_back(color);
                }
                for (const GltfModel::Part& part : model->parts()) {
                    MeshData piece = model->part(part);
                    Vec3 color = part.material >= 0 && size_t(part.material) < colors.size() ? colors[size_t(part.material)] : Vec3{0.62f, 0.64f, 0.68f};
                    bool isSeenThrough = part.material >= 0 && size_t(part.material) < model->materials.size() && model->materials[size_t(part.material)].baseColor.w < 0.6f;
                    if (isSeenThrough) continue;                      // glass: what is behind it is what is seen
                    for (size_t t = 0; t + 2 < piece.indices.size(); t += 3) {
                        triangles->push_back({piece.positions[piece.indices[t]], piece.positions[piece.indices[t + 1]], piece.positions[piece.indices[t + 2]], color, 0});
                    }
                }
                useTriangles(triangles);
                // What it is made of: the nodes that carry meshes, its bones, its animations.
                for (const GltfNode& node : model->nodes) {
                    if (node.mesh < 0 || size_t(node.mesh) >= model->meshes.size()) continue;
                    int triangles = 0;
                    for (const GltfPrimitive& primitive : model->meshes[size_t(node.mesh)]) triangles += int(primitive.mesh.indices.size() / 3);
                    parts_.push_back({node.name.empty() ? "Mesh" : node.name, triangles});
                }
                for (const GltfSkin& skin : model->skins) parts_.push_back({"Skeleton: bones", int(skin.joints.size())});
                for (const GltfAnimation& animation : model->animations) parts_.push_back({"Animation: " + animation.name, int(animation.duration * 100)});
            }
        } else if (asset.type == AssetType::Audio) {
            if (std::shared_ptr<AudioClip> clip = Audio::loadClip(path)) {
                clip_ = std::make_shared<AudioClip>(*clip);       // its own copy: the tools below change it
                ImportSettings::load(path).apply(*clip_);         // heard as the game will have it
                undo_.clear();
                isChanged_ = false;
                from_ = 0;
                to_ = clip->duration();
                viewFrom_ = 0;
                viewTo_ = clip->duration();
                playPitch_ = 1;
            }
        } else if (asset.type != AssetType::Texture && asset.type != AssetType::Font && asset.size < 400000) {
            if (std::optional<std::string> contents = text::readFile(path)) text_ = *contents;
        }
    }

    void stop(EditorContext& editor) {
        if (voice_ != 0) editor.engine.audio().stop(voice_);
        voice_ = 0;
    }

    // ---------------------------------------------------------------- import settings

    /// How the file becomes what the game uses, for the kinds of file that have a say in it.
    /// Kept in the file's .meta; the file itself is not touched. A change is applied at once:
    /// the file is loaded again and the scene with it.
    void drawImportSettings(EditorContext& editor, const AssetInfo& asset) {
        std::string extension = loweredExtension(asset.path);
        bool isPicture = asset.type == AssetType::Texture && (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" || extension == ".bmp");
        bool isModel = asset.type == AssetType::Mesh, isSound = asset.type == AssetType::Audio;
        if (!isPicture && !isModel && !isSound) return;
        std::string file = editor.assets.absolutePath(asset.path);
        if (settingsFor_ != asset.path) {
            settingsFor_ = asset.path;
            settings_ = ImportSettings::load(file);
        }
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 3));
        bool isOpen = ImGui::CollapsingHeader(settings_.isDefault() ? "Import Settings" : "Import Settings  (changed)###import");
        ImGui::PopStyleVar();
        if (!isOpen) return;
        ImportSettings before = settings_;
        ImGui::PushID("import");
        if (isPicture) {
            int use = int(settings_.use);
            if (ui::combo("Read As", use, {"What the material asks", "Color", "Data"})) settings_.use = ImportSettings::Use(use);
            ui::tooltip("Color: a picture a person painted or photographed (sRGB).\nData: numbers in a picture, such as a normal map or a mask: not to be converted.\n"
                        "A material already asks for the right one for each of its slots; this is for when a file is in the wrong one.");
            const int sizes[] = {0, 4096, 2048, 1024, 512, 256, 128, 64, 32};
            int size = 0;
            for (int i = 0; i < 9; i++) {
                if (sizes[i] == settings_.maxSize) size = i;
            }
            if (ui::combo("Largest Size", size, {"As it is", "4096", "2048", "1024", "512", "256", "128", "64", "32"})) settings_.maxSize = sizes[size];
            ui::tooltip("Its longer side is brought down to this: less memory, and less detail up close");
            int filter = settings_.isPixelated ? 1 : 0;
            if (ui::combo("Drawn", filter, {"Smooth", "Sharp pixels"})) settings_.isPixelated = filter == 1;
            ui::tooltip("Sharp pixels: for pixel art, where each pixel should stay a square");
            ui::checkbox("Smaller Copies For Far Away", settings_.mipmaps);
            ui::tooltip("Mipmaps: without them a picture shimmers in the distance. Off for interface art.");
            ui::checkbox("Upside Down", settings_.flipsVertically);
        } else if (isModel) {
            ui::dragFloat("Scale", settings_.scale, 0.01f, 0.0001f, 10000.0f);
            ui::tooltip("0.01 for a model made in centimeters, 0.0254 for inches");
            int up = settings_.zIsUp ? 1 : 0;
            if (ui::combo("Up Is", up, {"Y", "Z"})) settings_.zIsUp = up == 1;
            ui::tooltip("Z for models from programs where Z points up: they arrive lying on their back otherwise");
            ui::checkbox("Centered", settings_.isCentered);
            ui::tooltip("Moves it so that the middle of its box is where its handle is");
            ui::checkbox("Smooth Normals", settings_.smoothsNormals);
            ui::tooltip("Works its normals out again so that faces that meet are shaded as one curved surface");
            ui::checkbox("Faces Turned Around", settings_.flipsFaces);
            ui::tooltip("For a model that arrives inside out");
        } else {
            ui::checkbox("Mono", settings_.isMono);
            ui::tooltip("Two channels made one: half the memory, and right for a sound placed in the world");
            const int rates[] = {0, 48000, 44100, 32000, 22050, 16000, 11025};
            int rate = 0;
            for (int i = 0; i < 7; i++) {
                if (rates[i] == settings_.sampleRate) rate = i;
            }
            if (ui::combo("Samples A Second", rate, {"As it is", "48000", "44100", "32000", "22050", "16000", "11025"})) settings_.sampleRate = rates[rate];
            ui::tooltip("Fewer: less memory, and a duller sound");
            ui::checkbox("Normalized", settings_.isNormalized);
            ui::tooltip("As loud as it can be without distorting");
            ui::sliderFloat("Volume", settings_.volume, 0.0f, 2.0f);
        }
        if (!settings_.isDefault() && ImGui::SmallButton("Back to the Usual")) settings_ = ImportSettings{};
        ImGui::PopID();
        // Applied when the change is finished (a slider let go), not at every step of it.
        if (settings_.toText() != before.toText()) isWaitingToApply_ = true;
        if (isWaitingToApply_ && !ImGui::IsAnyItemActive()) {
            isWaitingToApply_ = false;
            if (settings_.save(file)) {
                stop(editor);
                editor.engine.renderer().waitUntilIdle();       // what is replaced may be in a frame on its way
                // Everything that was loaded is let go and the scene read again: it asks for the files anew.
                editor.scene.reloadGameCode([&] { editor.engine.assets().reload(); });
                forgetAssetPreviews();
                shown_.clear();                                 // and the Viewer opens it again
            } else {
                editor.notify("Cannot write the settings of " + asset.path, ConsoleMessage::Level::Error);
            }
        }
        ImGui::Separator();
    }

    // ---------------------------------------------------------------- pictures

    void drawPicture(EditorContext& editor, const AssetInfo& asset) {
        std::shared_ptr<Texture> texture = editor.engine.assets().texture(asset.path);
        if (!texture) {
            ImGui::TextDisabled("This picture could not be read.");
            return;
        }
        float width = float(texture->handle()->width()), height = float(texture->handle()->height());
        ImGui::Text("%d x %d pixels", int(width), int(height));
        ImGui::SameLine(0, 16);
        ImGui::SetNextItemWidth(160);
        ImGui::SliderFloat("##zoom", &zoom_, 0.1f, 16.0f, "Zoom %.2fx", ImGuiSliderFlags_Logarithmic);
        ImGui::SameLine();
        if (ImGui::SmallButton("Fit")) {
            zoom_ = 1;
            pan_ = ImVec2(0, 0);
        }
        ImVec2 at = ImGui::GetCursorScreenPos(), room = ImGui::GetContentRegionAvail();
        if (room.x < 16 || room.y < 16) return;
        ImGui::InvisibleButton("##picture", room, ImGuiButtonFlags_MouseButtonLeft);
        if (ImGui::IsItemHovered()) zoom_ = std::clamp(zoom_ * std::pow(1.15f, ImGui::GetIO().MouseWheel), 0.1f, 16.0f);
        if (ImGui::IsItemActive()) {
            pan_.x += ImGui::GetIO().MouseDelta.x;
            pan_.y += ImGui::GetIO().MouseDelta.y;
        }
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(at, ImVec2(at.x + room.x, at.y + room.y), true);
        // Squares behind it, so that what is transparent shows as such.
        for (float y = 0; y < room.y; y += 16) {
            for (float x = 0; x < room.x; x += 16) {
                bool isDark = (int(x / 16) + int(y / 16)) % 2 == 0;
                drawList->AddRectFilled(ImVec2(at.x + x, at.y + y), ImVec2(at.x + x + 16, at.y + y + 16), isDark ? IM_COL32(44, 46, 52, 255) : IM_COL32(56, 58, 64, 255));
            }
        }
        float fit = std::min(room.x / width, room.y / height) * zoom_;
        ImVec2 size(width * fit, height * fit);
        ImVec2 min(at.x + (room.x - size.x) * 0.5f + pan_.x, at.y + (room.y - size.y) * 0.5f + pan_.y);
        drawList->AddImage(ImGuiLayer::textureId(texture.get()), min, ImVec2(min.x + size.x, min.y + size.y));
        drawList->PopClipRect();
    }

    // ---------------------------------------------------------------- models

    /// The mean color of a picture, in linear light: what a part with it looks like from afar.
    static Vec3 averageColorOf(const std::string& file) {
        std::vector<uint8_t> pixels;
        int width = 0, height = 0;
        if (!Texture::readPixels(file, pixels, width, height) || pixels.empty()) return Vec3{1, 1, 1};
        double sum[3] = {0, 0, 0};
        int count = 0;
        size_t step = std::max<size_t>(1, size_t(width) * size_t(height) / 4096);
        for (size_t i = 0; i < size_t(width) * size_t(height); i += step) {
            for (int c = 0; c < 3; c++) sum[c] += std::pow(pixels[i * 4 + size_t(c)] / 255.0, 2.2);
            count++;
        }
        return count ? Vec3{float(sum[0] / count), float(sum[1] / count), float(sum[2] / count)} : Vec3{1, 1, 1};
    }

    /// The triangles of the model shown, and a thinner set of them to turn it with.
    void useTriangles(std::shared_ptr<std::vector<SoftTriangle>> triangles) {
        modelTriangles_ = triangles;
        const size_t most = 40000;
        if (triangles->size() <= most) {
            thinTriangles_ = triangles;
            return;
        }
        auto thin = std::make_shared<std::vector<SoftTriangle>>();
        size_t step = triangles->size() / most + 1;
        for (size_t i = 0; i < triangles->size(); i += step) thin->push_back((*triangles)[i]);
        thinTriangles_ = thin;
    }

    struct Look {
        float yaw = -1, pitch = 0, distance = 0;
        int width = 0, height = 0;
        bool isWhole = false;
        bool operator==(const Look& other) const {
            return yaw == other.yaw && pitch == other.pitch && distance == other.distance && width == other.width && height == other.height && isWhole == other.isWhole;
        }
    };

    void drawModel(EditorContext& editor, const AssetInfo& asset) {
        const MeshData& mesh = *mesh_;
        AABB bounds = mesh.bounds();
        Vec3 extent = bounds.halfExtents() * 2.0f;
        ImGui::TextDisabled("%d triangles, %.2f x %.2f x %.2f m", int(mesh.indices.size() / 3), extent.x, extent.y, extent.z);
        ImGui::SameLine();
        if (ImGui::SmallButton("Add to Scene")) {
            if (!editor.scene.createFromMesh(asset.path, Vec3{0, 0, 0}).isValid()) editor.notify("Cannot load " + asset.path, ConsoleMessage::Level::Error);
        }
        // What it is made of, folded away: the picture is what a small panel is for.
        if (ImGui::TreeNodeEx("##parts", ImGuiTreeNodeFlags_SpanAvailWidth, "Parts (%d)", int(parts_.size()))) {
            ImGui::BeginChild("##partlist", ImVec2(0, std::min(20.0f * float(parts_.size()) + 8, 140.0f)), ImGuiChildFlags_Borders);
            for (const auto& part : parts_) {
                ImGui::TextUnformatted(part.first.c_str());
                ImGui::SameLine(std::max(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 60, 120.0f));
                ImGui::TextDisabled("%d", part.second);
            }
            ImGui::EndChild();
            ImGui::TreePop();
        }
        ImVec2 at = ImGui::GetCursorScreenPos(), room = ImGui::GetContentRegionAvail();
        room.y = std::max(room.y, 120.0f);
        if (room.x < 32) return;
        ImGui::InvisibleButton("##model", room);
        bool isTurning = ImGui::IsItemActive();
        if (isTurning) {
            yaw_ += ImGui::GetIO().MouseDelta.x * 0.01f;
            pitch_ = std::clamp(pitch_ + ImGui::GetIO().MouseDelta.y * 0.01f, -1.4f, 1.4f);
        }
        if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0) distance_ = std::clamp(distance_ * std::pow(0.88f, ImGui::GetIO().MouseWheel), 1.1f, 12.0f);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(at, ImVec2(at.x + room.x, at.y + room.y), IM_COL32(24, 26, 31, 255), 4.0f);

        // The picture is made away from the frame, by the software renderer: the thin set of
        // triangles while it turns, all of them once it is still.
        float pixels = std::min(ImGui::GetIO().DisplayFramebufferScale.x, 2.0f);
        Look wanted;
        wanted.yaw = yaw_;
        wanted.pitch = pitch_;
        wanted.distance = distance_;
        wanted.width = std::max(16, int(room.x * pixels));
        wanted.height = std::max(16, int(room.y * pixels));
        float scale = std::sqrt(std::min(1.0f, 700000.0f / float(wanted.width * wanted.height)));
        wanted.width = int(float(wanted.width) * scale);
        wanted.height = int(float(wanted.height) * scale);
        wanted.isWhole = !isTurning;
        if (job_.valid() && job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            SoftImage image = job_.get();
            if (!image.isEmpty()) modelPicture_ = editor.engine.assets().makeTexture(image.rgba, image.width, image.height);
            shownLook_ = jobLook_;
        }
        if (!job_.valid() && !(shownLook_ == wanted) && modelTriangles_) {
            std::shared_ptr<const std::vector<SoftTriangle>> triangles = wanted.isWhole ? modelTriangles_ : thinTriangles_;
            Vec3 center = bounds.center();
            float radius = std::max(simd::length(bounds.halfExtents()), 1e-4f);
            SoftView view;
            view.target = center;
            view.fovY = 0.55f;
            Vec3 direction{std::sin(yaw_) * std::cos(pitch_), std::sin(pitch_), std::cos(yaw_) * std::cos(pitch_)};
            view.eye = center + direction * (radius / std::sin(view.fovY * 0.5f) * distance_ / 2.6f);
            view.skyZenith = Vec3{0.12f, 0.14f, 0.18f};
            view.skyHorizon = Vec3{0.24f, 0.26f, 0.3f};
            view.skyGround = Vec3{0.09f, 0.1f, 0.12f};
            view.sunIntensity = 2.8f;
            view.sunDirection = simd::normalize(Vec3{-0.4f, -0.8f, -0.45f});
            jobLook_ = wanted;
            job_ = std::async(std::launch::async, [triangles, view, wanted] { return renderTriangles(*triangles, view, wanted.width, wanted.height); });
        }
        if (modelPicture_) drawList->AddImageRounded(ImGuiLayer::textureId(modelPicture_.get()), at, ImVec2(at.x + room.x, at.y + room.y), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 4.0f);
        else drawList->AddText(ImVec2(at.x + 10, at.y + 10), IM_COL32(150, 156, 168, 255), "Drawing...");
        drawList->AddText(ImVec2(at.x + 8, at.y + room.y - 20), IM_COL32(150, 156, 168, 200), "Drag to turn, scroll to come closer");
    }

    // ---------------------------------------------------------------- materials

    void drawMaterial(EditorContext& editor, const AssetInfo& asset) {
        std::shared_ptr<Material> material = editor.engine.assets().material(asset.path);
        if (!material) {
            ImGui::TextWrapped("This file could not be read as a material.");
            return;
        }
        // Beside its settings where there is room for both; above them in a narrow panel,
        // so that the settings always have the width their names and sliders need.
        ImVec2 room = ImGui::GetContentRegionAvail();
        bool isNarrow = room.x < 620;
        float side = isNarrow ? std::clamp(std::min(room.x - 16, room.y * 0.38f), 90.0f, 220.0f) : std::clamp(std::min(room.x - 400, room.y - 8), 120.0f, 340.0f);
        if (isNarrow) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max((room.x - side) * 0.5f, 0.0f));
        ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + side, at.y + side), IM_COL32(24, 26, 31, 255), 6.0f);
        drawMaterialPreview(ImGui::GetWindowDrawList(), ImVec2(at.x + side * 0.5f, at.y + side * 0.5f), side * 0.44f, *material);
        ImGui::Dummy(ImVec2(side, side));
        if (!isNarrow) ImGui::SameLine(0, 14);
        ImGui::BeginChild("##settings", ImVec2(0, 0));
        // Which settings there are depends on the shader the material uses.
        if (drawMaterialProperties(editor, *material)) saveMaterialAsset(editor, *material);
        ImGui::Spacing();
        ImGui::TextDisabled("Changes are saved to the file as you make them.");
        ImGui::EndChild();
    }

    // ---------------------------------------------------------------- sounds

    void drawSound(EditorContext& editor, const AssetInfo& asset) {
        const AudioClip& clip = *clip_;
        float duration = clip.duration();
        Mixer& mixer = editor.engine.audio().mixer();
        bool isPlaying = voice_ != 0 && mixer.isPlaying(voice_);
        if (!isPlaying) voice_ = 0;

        if (ImGui::Button(isPlaying ? "Stop" : "Play")) {
            if (isPlaying) {
                stop(editor);
            } else {
                // What is between the two cuts, as a clip of its own.
                auto piece = std::make_shared<AudioClip>(cut());
                PlayOptions options;
                options.pitch = playPitch_;
                options.loops = loops_;
                voice_ = editor.engine.audio().play(piece, options);
                startedAt_ = ImGui::GetTime();
            }
        }
        ImGui::SameLine();
        ImGui::Checkbox("Loop", &loops_);
        ImGui::SameLine(0, 16);
        ImGui::SetNextItemWidth(std::min(200.0f, ImGui::GetContentRegionAvail().x - 44));
        // Bent while it plays: hear what a pitch does before using it in the game.
        if (ImGui::SliderFloat("##pitch", &playPitch_, 0.25f, 4.0f, "Pitch %.2f", ImGuiSliderFlags_Logarithmic) && voice_ != 0) mixer.setPitch(voice_, playPitch_);
        ImGui::SameLine();
        if (ImGui::SmallButton("1.0")) {
            playPitch_ = 1;
            if (voice_ != 0) mixer.setPitch(voice_, 1);
        }
        ImGui::TextDisabled("%.2f s   %d Hz   %s", duration, clip.sampleRate, clip.channels == 1 ? "mono" : "stereo");

        // ---- the wave, with the two cuts
        ImVec2 at = ImGui::GetCursorScreenPos();
        // The tools below need their room: the wave takes what is left, and never less than a strip.
        ImVec2 room(ImGui::GetContentRegionAvail().x, std::clamp(ImGui::GetContentRegionAvail().y - 330, 70.0f, std::min(420.0f, std::max(ImGui::GetContentRegionAvail().x * 0.55f, 90.0f))));
        ImGui::InvisibleButton("##wave", room);
        bool isHovered = ImGui::IsItemHovered();
        float span = std::max(viewTo_ - viewFrom_, 1e-4f);
        auto timeAt = [&](float x) { return viewFrom_ + (x - at.x) / room.x * span; };
        auto xAt = [&](float time) { return at.x + (time - viewFrom_) / span * room.x; };
        if (isHovered && ImGui::GetIO().MouseWheel != 0) {
            // Zoom around the pointer.
            float focus = timeAt(ImGui::GetIO().MousePos.x), factor = std::pow(0.85f, ImGui::GetIO().MouseWheel);
            float newSpan = std::clamp(span * factor, 0.005f, duration);
            viewFrom_ = std::clamp(focus - (focus - viewFrom_) * newSpan / span, 0.0f, duration - newSpan);
            viewTo_ = viewFrom_ + newSpan;
        }
        if (ImGui::IsItemActive()) {
            float time = std::clamp(timeAt(ImGui::GetIO().MousePos.x), 0.0f, duration);
            if (ImGui::IsItemActivated()) dragsEnd_ = std::abs(time - to_) < std::abs(time - from_);
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::GetIO().KeyAlt) {
                float shift = -ImGui::GetIO().MouseDelta.x / room.x * span;
                shift = std::clamp(shift, -viewFrom_, duration - viewTo_);
                viewFrom_ += shift;
                viewTo_ += shift;
            } else if (dragsEnd_) {
                to_ = std::max(time, from_ + 0.005f);
            } else {
                from_ = std::min(time, to_ - 0.005f);
            }
        }
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(at, ImVec2(at.x + room.x, at.y + room.y), IM_COL32(24, 26, 31, 255), 4.0f);
        drawList->PushClipRect(at, ImVec2(at.x + room.x, at.y + room.y), true);
        float middle = at.y + room.y * 0.5f, half = room.y * 0.46f;
        size_t frames = clip.samples.size() / size_t(std::max(clip.channels, 1));
        for (int x = 0; x < int(room.x); x++) {
            // The loudest sample that falls under this column, up and down.
            size_t first = size_t(std::clamp(timeAt(at.x + float(x)), 0.0f, duration) * float(clip.sampleRate));
            size_t last = size_t(std::clamp(timeAt(at.x + float(x) + 1), 0.0f, duration) * float(clip.sampleRate));
            last = std::min(std::max(last, first + 1), frames);
            float low = 0, high = 0;
            size_t stride = std::max<size_t>(1, (last - first) / 64);
            for (size_t frame = first; frame < last; frame += stride) {
                float sample = clip.samples[frame * size_t(clip.channels)];
                low = std::min(low, sample);
                high = std::max(high, sample);
            }
            float time = timeAt(at.x + float(x));
            bool isKept = time >= from_ && time <= to_;
            drawList->AddLine(ImVec2(at.x + float(x), middle - high * half), ImVec2(at.x + float(x), middle - low * half + 1),
                              isKept ? IM_COL32(255, 122, 47, 255) : IM_COL32(96, 100, 110, 255));
        }
        for (float edge : {from_, to_}) {
            float x = xAt(edge);
            drawList->AddLine(ImVec2(x, at.y), ImVec2(x, at.y + room.y), IM_COL32(120, 190, 255, 255), 2.0f);
            drawList->AddTriangleFilled(ImVec2(x - 6, at.y), ImVec2(x + 6, at.y), ImVec2(x, at.y + 9), IM_COL32(120, 190, 255, 255));
        }
        if (isPlaying) {
            float length = std::max(to_ - from_, 0.001f);
            float played = float(ImGui::GetTime() - startedAt_) * playPitch_;
            float time = from_ + (loops_ ? std::fmod(played, length) : std::min(played, length));
            drawList->AddLine(ImVec2(xAt(time), at.y), ImVec2(xAt(time), at.y + room.y), IM_COL32(255, 255, 255, 220), 1.5f);
        }
        drawList->PopClipRect();

        // ---- the stretch
        ImGui::SetNextItemWidth(120);
        ImGui::DragFloat("##from", &from_, 0.005f, 0.0f, to_ - 0.005f, "From %.3f s");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        ImGui::DragFloat("##to", &to_, 0.005f, from_ + 0.005f, duration, "To %.3f s");
        from_ = std::clamp(from_, 0.0f, duration);
        to_ = std::clamp(to_, from_, duration);
        ImGui::SameLine();
        if (ImGui::Button("Whole")) {
            from_ = 0, to_ = duration;
            viewFrom_ = 0, viewTo_ = duration;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%.3f s chosen. Drag in the wave to move the nearer edge; scroll to zoom, Alt-drag to slide.", to_ - from_);
        ImGui::PopStyleColor();

        // ---- the tools: each changes the chosen stretch of this copy, and can be undone
        // The tools flow like words: each goes beside the last if it fits, and under it if not.
        bool startsRow = true;
        auto beside = [&](float width) {
            float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
            if (!startsRow && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width <= right) ImGui::SameLine();
            startsRow = false;
        };
        auto tool = [&](const char* label, const char* help) {
            beside(ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2);
            bool isPressed = ImGui::Button(label);
            ui::tooltip(help);
            if (isPressed) remember(editor);
            return isPressed;
        };
        ImGui::SeparatorText("Level");
        startsRow = true;
        beside(150);
        ImGui::SetNextItemWidth(150);
        ImGui::SliderFloat("##gain", &gainDb_, -24.0f, 24.0f, "%+.1f dB");
        if (tool("Apply Gain", "Makes the stretch louder or quieter by this much")) audio::gain(*clip_, from_, to_, audio::decibels(gainDb_));
        if (tool("Normalize", "As loud as it can be without distorting")) audio::normalize(*clip_, from_, to_);
        if (tool("Fade In", "From silence, over the stretch")) audio::fadeIn(*clip_, from_, to_);
        if (tool("Fade Out", "To silence, over the stretch")) audio::fadeOut(*clip_, from_, to_);
        if (tool("Silence", "Makes the stretch silent and keeps its length")) audio::silence(*clip_, from_, to_);

        ImGui::SeparatorText("Shape");
        startsRow = true;
        if (tool("Keep Only This", "Throws away everything outside the stretch")) {
            audio::trim(*clip_, from_, to_);
            wholeAgain();
        }
        if (tool("Cut Out", "Takes the stretch out and joins what is left")) {
            audio::remove(*clip_, from_, to_);
            wholeAgain();
        }
        if (tool("Reverse", "Plays the stretch backward")) audio::reverse(*clip_, from_, to_);
        if (tool("Bake Pitch", "Makes the pitch set above part of the sound: higher is also shorter")) {
            audio::changeSpeed(*clip_, playPitch_);
            playPitch_ = 1;
            wholeAgain();
        }
        beside(110);
        ImGui::SetNextItemWidth(110);
        ImGui::DragFloat("##loop", &loopBlend_, 0.005f, 0.01f, 2.0f, "Blend %.2f s");
        if (tool("Make Loop", "Blends the end into the beginning, so that it can play around and around without a click")) {
            audio::makeLoop(*clip_, loopBlend_);
            wholeAgain();
        }
        if (tool(clip.channels > 1 ? "To Mono" : "To Stereo", "One channel from two, or two from one")) {
            if (clip_->channels > 1) audio::toMono(*clip_);
            else audio::toStereo(*clip_);
        }

        ImGui::SeparatorText("Tone");
        startsRow = true;
        beside(150);
        ImGui::SetNextItemWidth(150);
        ImGui::SliderFloat("##cutoff", &cutoff_, 40.0f, 12000.0f, "%.0f Hz", ImGuiSliderFlags_Logarithmic);
        if (tool("Low Pass", "Keeps what is below this pitch: duller, farther away")) audio::lowPass(*clip_, from_, to_, cutoff_);
        if (tool("High Pass", "Keeps what is above this pitch: thinner, without the rumble")) audio::highPass(*clip_, from_, to_, cutoff_);

        ImGui::Separator();
        ImGui::BeginDisabled(undo_.empty());
        if (ImGui::Button("Undo")) {
            stop(editor);
            *clip_ = std::move(undo_.back());
            undo_.pop_back();
            isChanged_ = !undo_.empty();
            wholeAgain();
        }
        ImGui::EndDisabled();
        ImGui::SameLine(0, 16);
        bool isCut = from_ > 0.0005f || to_ < clip_->duration() - 0.0005f;
        ImGui::BeginDisabled(!isCut && !isChanged_ && std::abs(playPitch_ - 1) < 0.005f);
        if (ImGui::Button("Save as New Sound")) save(editor, asset, false);
        ui::tooltip("Writes what is between the edges, as it sounds now, to a new file beside this one");
        ImGui::SameLine();
        if (ImGui::Button("Save Over This File")) save(editor, asset, true);
        ui::tooltip("Replaces this file with what is between the edges, as a WAV. Everything that uses the sound gets the new one.");
        ImGui::EndDisabled();
        if (isChanged_) {
            ImGui::SameLine(0, 16);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Changed, not saved");
        }
    }

    /// Keeps the clip as it is now, to come back to. The few last ones only: sounds are large.
    void remember(EditorContext& editor) {
        stop(editor);
        undo_.push_back(*clip_);
        if (undo_.size() > 12) undo_.erase(undo_.begin());
        isChanged_ = true;
    }

    /// After a change of length: the edges and the view back to the whole clip.
    void wholeAgain() {
        from_ = 0;
        to_ = clip_->duration();
        viewFrom_ = 0;
        viewTo_ = std::max(clip_->duration(), 0.001f);
    }

    /// What is between the two cuts.
    AudioClip cut() const {
        AudioClip piece;
        piece.sampleRate = clip_->sampleRate;
        piece.channels = clip_->channels;
        size_t channels = size_t(std::max(clip_->channels, 1));
        size_t first = std::min(size_t(from_ * float(clip_->sampleRate)) * channels, clip_->samples.size());
        size_t last = std::min(size_t(to_ * float(clip_->sampleRate)) * channels, clip_->samples.size());
        if (last > first) piece.samples.assign(clip_->samples.begin() + long(first), clip_->samples.begin() + long(last));
        return piece;
    }

    void save(EditorContext& editor, const AssetInfo& asset, bool replaces) {
        AudioClip piece = cut();
        // The pitch, made part of the sound: read through it faster or slower.
        if (std::abs(playPitch_ - 1) >= 0.005f) audio::changeSpeed(piece, playPitch_);
        std::filesystem::path original(asset.path);
        std::string folder = original.parent_path().generic_string(), stem = original.stem().string();
        std::string relative = (folder.empty() ? "" : folder + "/") + stem + ".wav";
        bool isWav = loweredExtension(asset.path) == ".wav";
        if (!replaces || !isWav) {
            // A file of another kind is never written over: its WAV goes beside it.
            for (int number = 1; number < 1000; number++) {
                relative = (folder.empty() ? "" : folder + "/") + stem + (replaces ? "_edited" : "_cut") + (number > 1 ? std::to_string(number) : "") + ".wav";
                if (!std::filesystem::exists(editor.assets.absolutePath(relative))) break;
            }
        }
        if (writeWav(editor.assets.absolutePath(relative), piece)) {
            editor.assets.refresh();
            editor.engine.assets().reload(relative);
            editor.notify("Saved " + relative);
            if (replaces && isWav) {
                undo_.clear();
                isChanged_ = false;
            }
        } else {
            editor.notify("Cannot write " + relative, ConsoleMessage::Level::Error);
        }
    }

    // ---------------------------------------------------------------- scripts and text

    void drawGraph(EditorContext& editor, const AssetInfo& asset) {
        if (ImGui::Button("Edit in Visual Script")) editor.editGraph(asset.path);
        ImGui::SameLine();
        std::string name = script::typeNameFor(std::filesystem::path(asset.path).stem().string());
        if (ImGui::Button("Convert to C++ Script")) {
            std::string made = convertGraphToCpp(editor, asset.path);
            if (!made.empty()) platform::openWithDefaultApplication(editor.assets.absolutePath(made));
        }
        ui::tooltip("Writes the same events and steps as a C++ component in Assets/Scripts, to go on from in code. It keeps the graph, to come back");
        // The code it would become, to read.
        if (shownCodeFor_ != asset.path) {
            shownCodeFor_ = asset.path;
            std::optional<script::Graph> graph = script::Graph::fromText(text_);
            code_ = graph ? script::graphToCpp(*graph, name, "Assets/" + asset.path) : std::string("This file could not be read as a visual script.");
        }
        if (ImGui::BeginTabBar("##forms")) {
            if (ImGui::BeginTabItem("As C++")) {
                ImGui::InputTextMultiline("##code", code_.data(), code_.size() + 1, ImVec2(-FLT_MIN, -FLT_MIN), ImGuiInputTextFlags_ReadOnly);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("As Saved")) {
                drawTextBody();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    void drawText(EditorContext& editor, const AssetInfo& asset) {
        if (asset.type == AssetType::Script) {
            // The component the script declares, if the game's code is built: its fields, and a way to use it.
            std::string name = std::filesystem::path(asset.path).stem().string();
            if (const ComponentType* type = ComponentRegistry::instance().find(name)) {
                ImGui::Text("Component %s", type->name.c_str());
                ImGui::SameLine();
                Entity selected = editor.scene.selection().empty() ? Entity{} : editor.scene.selection().front();
                ImGui::BeginDisabled(!selected.isValid() || type->has(editor.engine.scene, selected));
                if (ImGui::SmallButton("Add to Selected Object")) {
                    type->add(editor.engine.scene, selected);
                    editor.scene.markChanged();
                }
                ImGui::EndDisabled();
                std::string fields;
                for (const Field& field : type->fields) fields += (fields.empty() ? "" : ",  ") + field.name;
                ImGui::TextDisabled("Fields: %s", fields.empty() ? "none" : fields.c_str());
            } else {
                ImGui::TextDisabled("A script. Declare a component in it with MOLTEN_COMPONENT(%s, ...) to add it to objects.", name.c_str());
            }
            if (ImGui::SmallButton("Edit")) platform::openWithDefaultApplication(editor.assets.absolutePath(asset.path));
            // Made from a visual script: it can go back to one.
            bool wasChanged = false;
            if (script::graphKeptIn(text_, &wasChanged)) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Convert to Visual Script")) {
                    std::string made = convertCppToGraph(editor, asset.path);
                    if (!made.empty()) editor.editGraph(made);
                }
                ui::tooltip(wasChanged ? "It was made from a visual script, then changed: the graph it was made from comes back, without the changes"
                                       : "It was made from a visual script: the same boxes and wires come back");
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Saved changes are built into the game by themselves.");
        }
        if (text_.empty()) {
            ImGui::TextDisabled(asset.size >= 400000 ? "Too long to show here." : "Nothing to show for this kind of file.");
            return;
        }
        int lines = int(std::count(text_.begin(), text_.end(), '\n')) + 1;
        ImGui::TextDisabled("%d lines", lines);
        drawTextBody();
    }

    void drawTextBody() {
        ImGui::InputTextMultiline("##text", text_.data(), text_.size() + 1, ImVec2(-FLT_MIN, -FLT_MIN), ImGuiInputTextFlags_ReadOnly);
    }

    struct Face {
        float depth;
        ImVec2 a, b, c;
        ImU32 color;
    };

    std::string shown_;
    // pictures
    float zoom_ = 1;
    ImVec2 pan_{0, 0};
    // models
    std::optional<MeshData> mesh_;
    std::vector<std::pair<std::string, int>> parts_;
    std::vector<Face> faces_;
    float yaw_ = 0.7f, pitch_ = 0.35f, distance_ = 2.6f;
    std::shared_ptr<const std::vector<SoftTriangle>> modelTriangles_, thinTriangles_;
    std::shared_ptr<Texture> modelPicture_;
    std::future<SoftImage> job_;
    Look jobLook_, shownLook_;
    // sounds
    ImportSettings settings_;               // of the file shown
    std::string settingsFor_;
    bool isWaitingToApply_ = false;
    std::filesystem::file_time_type shownTime_{};       // when the file shown was last written
    std::shared_ptr<AudioClip> clip_;
    SoundHandle voice_ = 0;
    float from_ = 0, to_ = 0, viewFrom_ = 0, viewTo_ = 0, playPitch_ = 1;
    bool loops_ = false, dragsEnd_ = false, isChanged_ = false;
    float gainDb_ = -3.0f, cutoff_ = 1200.0f, loopBlend_ = 0.1f;
    std::vector<AudioClip> undo_;           // the clip before each of the last changes
    double startedAt_ = 0;
    // text
    std::string text_;
    std::string code_, shownCodeFor_;
};

}  // namespace

void registerAssetViewerPanel(EditorContext& editor) {
    setMaterialPreviewAssets(&editor.engine.assets());
}

void drawAssetViewer(EditorContext& editor) {
    static AssetViewer viewer;
    viewer.draw(editor);
}

}  // namespace molten::editor
