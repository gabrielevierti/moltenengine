#include "Panels/Pickers.hpp"

#include "Animation/Animator.hpp"
#include "Animation/AnimatorController.hpp"
#include "Assets/Gltf.hpp"
#include "Core/TextFile.hpp"
#include "Game/Component.hpp"
#include "Input/Input.hpp"
#include "Panels/Widgets.hpp"
#include "Rendering/Material.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>

namespace molten::editor {

namespace {

std::string lower(std::string text) {
    for (char& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool hasExtension(const std::string& path, const std::vector<std::string>& extensions) {
    std::string extension = lower(std::filesystem::path(path).extension().string());
    return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
}

/// The glTF model an entity shows or animates, if it has one.
std::shared_ptr<const GltfModel> modelOf(EditorContext& editor, Entity owner) {
    Scene& scene = editor.engine.scene;
    std::string file;
    if (const Animator* animator = scene.get<Animator>(owner)) file = animator->model;
    if (file.empty()) {
        if (const MeshRenderer* renderer = scene.get<MeshRenderer>(owner); renderer && renderer->mesh) file = renderer->mesh->name;
    }
    size_t part = file.find('#');
    if (part != std::string::npos) file = file.substr(0, part);
    if (file.empty() || !isGltfFile(file)) return nullptr;
    return sharedGltfModel(editor.engine.assetPath(file));
}

std::optional<AnimatorController> controllerOf(EditorContext& editor, Entity owner) {
    const Animator* animator = editor.engine.scene.get<Animator>(owner);
    if (!animator || animator->controller.empty()) return std::nullopt;
    std::optional<std::string> text = text::readFile(editor.engine.assetPath(animator->controller));
    return text ? AnimatorController::parse(*text) : std::nullopt;
}

/// Every value the components of the scene name for a kind ("value", "message"): those in use.
std::set<std::string> namesInUse(EditorContext& editor, const std::string& kind) {
    std::set<std::string> names;
    Scene& scene = editor.engine.scene;
    for (const ComponentType& type : ComponentRegistry::instance().types()) {
        if (!type.get) continue;
        for (Entity entity : editor.scene.entities()) {
            void* component = type.get(scene, entity);
            if (!component) continue;
            for (const Field& field : type.fields) {
                if (field.type != FieldType::String) continue;
                std::string fieldKind = field.pick;
                if (!field.pickBy.empty()) {
                    if (const Field* by = type.findField(field.pickBy)) {
                        std::string decides = by->valueText(component);
                        for (const auto& [value, picked] : field.pickFor) {
                            if (value == decides) fieldKind = picked;
                        }
                    }
                }
                if (fieldKind != kind) continue;
                std::string value = field.valueText(component);
                if (!value.empty()) names.insert(value);
            }
        }
    }
    return names;
}

std::vector<std::string> inputActions(EditorContext& editor, bool axes) {
    InputMap map = InputMap::standard();
    if (editor.hasProject) {
        if (std::optional<std::string> text = text::readFile(editor.assets.absolutePath("Input.actions"))) map = InputMap::fromText(*text);
    }
    std::vector<std::string> names;
    for (const InputAction& action : map.actions) {
        if (action.isAxis == axes) names.push_back(action.name);
    }
    return names;
}

std::vector<std::string> tagsInUse(EditorContext& editor) {
    std::vector<std::string> tags = {"Player", "Enemy", "Pickup", "Obstacle", "Ground", "MainCamera", "Finish"};
    editor.engine.scene.each<Tag>([&](Entity, Tag& tag) {
        if (tag.value != "Untagged" && std::find(tags.begin(), tags.end(), tag.value) == tags.end()) tags.push_back(tag.value);
    });
    return tags;
}

std::vector<std::string> entityNames(EditorContext& editor) {
    std::set<std::string> seen;
    std::vector<std::string> names;
    for (Entity entity : editor.scene.entities()) {
        std::string name = editor.scene.nameOf(entity);
        if (!name.empty() && seen.insert(name).second) names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

// ---- the control

/// A list to choose from, with a search box when it is long, and "New…" for the kinds a new name
/// can be given to.
bool chooseFrom(const char* id, std::string& value, const std::vector<std::string>& choices, bool allowsNone, bool allowsNew, float width) {
    static std::string filter, typed;
    bool changed = false;
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(width);
    std::string shown = value.empty() ? std::string("None") : value;
    bool known = value.empty() || std::find(choices.begin(), choices.end(), value) != choices.end();
    if (!known) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.4f, 1.0f));
    bool open = ImGui::BeginCombo("##pick", known ? shown.c_str() : (shown + "  (missing)").c_str(), ImGuiComboFlags_HeightLarge);
    if (!known) {
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("\"%s\" is not in the scene or the project any more: choose what it should be", value.c_str());
    }
    if (open) {
        if (ImGui::IsWindowAppearing()) filter.clear();
        if (choices.size() > 10) {
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##filter", "Search", &filter);
        }
        if (allowsNone && ImGui::Selectable("None", value.empty())) {
            changed = !value.empty();
            value.clear();
        }
        std::string wanted = lower(filter);
        int shownCount = 0;
        for (const std::string& choice : choices) {
            if (!wanted.empty() && lower(choice).find(wanted) == std::string::npos) continue;
            shownCount++;
            if (ImGui::Selectable(choice.c_str(), choice == value)) {
                changed = choice != value;
                value = choice;
            }
        }
        if (choices.empty()) ImGui::TextDisabled("Nothing of this kind yet.");
        else if (shownCount == 0) ImGui::TextDisabled("Nothing matches.");
        if (allowsNew) {
            ImGui::Separator();
            ImGui::TextDisabled("A new name:");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputTextWithHint("##new", "then Return", &typed, ImGuiInputTextFlags_EnterReturnsTrue) && !typed.empty()) {
                changed = typed != value;
                value = typed;
                typed.clear();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return changed;
}

/// Several at once, ticked in a list: for "bones".
bool chooseSeveral(const char* id, std::string& value, const std::vector<std::string>& choices, float width) {
    std::vector<std::string> picked;
    {
        std::istringstream stream(value);
        std::string name;
        while (std::getline(stream, name, ',')) {
            size_t a = name.find_first_not_of(' '), b = name.find_last_not_of(' ');
            if (a != std::string::npos) picked.push_back(name.substr(a, b - a + 1));
        }
    }
    bool changed = false;
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(width);
    std::string shown = picked.empty() ? std::string("Worked out from the skeleton") : std::to_string(picked.size()) + " chosen";
    if (ImGui::BeginCombo("##several", shown.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::Selectable("None: worked out from the skeleton", picked.empty())) {
            changed = !picked.empty();
            picked.clear();
        }
        ImGui::Separator();
        for (const std::string& choice : choices) {
            bool has = std::find(picked.begin(), picked.end(), choice) != picked.end();
            if (ImGui::Checkbox(choice.c_str(), &has)) {
                if (has) picked.push_back(choice);
                else picked.erase(std::remove(picked.begin(), picked.end(), choice), picked.end());
                changed = true;
            }
        }
        if (choices.empty()) ImGui::TextDisabled("The entity has no skeleton.");
        ImGui::EndCombo();
    }
    ImGui::PopID();
    if (changed) {
        value.clear();
        for (size_t i = 0; i < picked.size(); i++) value += (i ? ", " : "") + picked[i];
    }
    return changed;
}

}  // namespace

bool pickFromList(const char* id, std::string& value, const std::vector<std::string>& choices, bool allowsNew, float width) {
    return chooseFrom(id, value, choices, true, allowsNew, width);
}

std::vector<std::string> extensionsOf(const std::string& kind) {
    if (kind == "sound") return {".wav", ".mp3", ".flac", ".ogg"};
    if (kind == "picture") return {".png", ".jpg", ".jpeg", ".tga", ".bmp"};
    if (kind == "prefab") return {".prefab"};
    if (kind == "scene") return {".scene"};
    if (kind == "font") return {".ttf", ".otf"};
    if (kind == "model") return {".gltf", ".glb", ".obj"};
    if (kind == "controller") return {".animator"};
    if (kind == "clip") return {".anim"};
    if (kind == "timeline") return {".timeline"};
    if (kind == "graph") return {".graph"};
    if (kind == "materialFile") return {".material"};
    if (kind == "terrain") return {".terrain"};
    if (kind == "foliage") return {".foliage"};
    return {};
}

std::vector<std::string> choicesFor(EditorContext& editor, Entity owner, const std::string& kind) {
    std::vector<std::string> out;
    Scene& scene = editor.engine.scene;
    if (std::vector<std::string> extensions = extensionsOf(kind); !extensions.empty()) {
        for (const AssetInfo& asset : editor.assets.assets()) {
            if (!asset.isFolder() && hasExtension(asset.path, extensions)) out.push_back(asset.path);
        }
        // And the engine's own, that every project can use (its fonts, the neutral grading table),
        // where the project has none of that name.
        std::error_code code;
        std::filesystem::path engine = editor.engine.assets().engineDirectory();
        if (!engine.empty() && std::filesystem::is_directory(engine, code)) {
            for (auto it = std::filesystem::recursive_directory_iterator(engine, code); it != std::filesystem::recursive_directory_iterator(); it.increment(code)) {
                if (code) break;
                if (!it->is_regular_file(code) || !hasExtension(it->path().string(), extensions)) continue;
                std::string relative = std::filesystem::relative(it->path(), engine, code).generic_string();
                if (relative.rfind("Branding/", 0) == 0) continue;
                if (std::find(out.begin(), out.end(), relative) == out.end()) out.push_back(relative);
            }
        }
        std::sort(out.begin(), out.end());
        return out;
    }
    if (kind == "folder") {
        for (const AssetInfo& asset : editor.assets.assets()) {
            if (asset.isFolder()) out.push_back(asset.path);
        }
        std::sort(out.begin(), out.end());
        return out;
    }
    if (kind == "entity") return entityNames(editor);
    if (kind == "target") {
        out = {"self", "other"};
        for (const std::string& name : entityNames(editor)) out.push_back(name);
        return out;
    }
    if (kind == "entityOrTag") {
        out = tagsInUse(editor);
        for (const std::string& name : entityNames(editor)) {
            if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
        }
        return out;
    }
    if (kind == "entityPrefix") {
        // "Patrol Point 1", "Patrol Point 2": "Patrol Point".
        std::set<std::string> prefixes;
        for (const std::string& name : entityNames(editor)) {
            size_t end = name.find_last_not_of("0123456789");
            if (end != std::string::npos && end + 1 < name.size()) prefixes.insert(name.substr(0, name.find_last_not_of(' ', end) + 1));
        }
        return std::vector<std::string>(prefixes.begin(), prefixes.end());
    }
    if (kind == "child") {
        for (Entity entity : editor.scene.entities()) {
            if (entity != owner && editor.scene.isDescendant(entity, owner)) out.push_back(editor.scene.nameOf(entity));
        }
        return out;
    }
    if (kind == "tag") return tagsInUse(editor);
    if (kind == "bone" || kind == "bones") {
        if (std::shared_ptr<const GltfModel> model = modelOf(editor, owner)) {
            std::set<int> joints;
            for (const GltfSkin& skin : model->skins) joints.insert(skin.joints.begin(), skin.joints.end());
            for (size_t i = 0; i < model->nodes.size(); i++) {
                if ((joints.empty() || joints.count(int(i))) && !model->nodes[i].name.empty()) out.push_back(model->nodes[i].name);
            }
        }
        return out;
    }
    if (kind == "animation") {
        // The model's own animations, then the clips made in the editor.
        if (std::shared_ptr<const GltfModel> model = modelOf(editor, owner)) {
            for (const GltfAnimation& animation : model->animations) out.push_back(animation.name);
        }
        for (const AssetInfo& asset : editor.assets.assets()) {
            if (!asset.isFolder() && isAnimationClipFile(asset.path)) out.push_back(asset.path);
        }
        return out;
    }
    if (kind == "state" || kind == "parameter") {
        auto add = [&](const AnimatorController& controller) {
            std::vector<std::string> names;
            if (kind == "state") {
                for (const auto& state : controller.states) names.push_back(state.name);
                for (const auto& layer : controller.layers) for (const auto& state : layer.states) names.push_back(state.name);
            } else {
                for (const auto& parameter : controller.parameters) names.push_back(parameter.name);
            }
            for (const std::string& name : names) {
                if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
            }
        };
        if (std::optional<AnimatorController> controller = controllerOf(editor, owner)) {
            add(*controller);
        } else if (!editor.engine.scene.isAlive(owner) || !editor.engine.scene.has<Animator>(owner)) {
            // Not for one entity (a visual script, for whatever it is put on): those of every controller.
            for (const AssetInfo& asset : editor.assets.assets()) {
                if (asset.isFolder() || !hasExtension(asset.path, {".animator"})) continue;
                if (std::optional<std::string> text = text::readFile(editor.assets.absolutePath(asset.path))) {
                    if (std::optional<AnimatorController> parsed = AnimatorController::parse(*text)) add(*parsed);
                }
            }
        }
        return out;
    }
    if (kind == "value") {
        std::set<std::string> names = namesInUse(editor, "value");
        for (const char* usual : {"score", "lives", "coins", "health"}) names.insert(usual);
        return std::vector<std::string>(names.begin(), names.end());
    }
    if (kind == "message") {
        std::set<std::string> names = namesInUse(editor, "message");
        return std::vector<std::string>(names.begin(), names.end());
    }
    if (kind == "action") return inputActions(editor, false);
    if (kind == "axis") return inputActions(editor, true);
    if (kind == "material") {
        for (const NamedMaterial& material : editor.scene.materials) out.push_back(material.name);
        for (const AssetInfo& asset : editor.assets.assets()) {
            if (asset.type == AssetType::Material) out.push_back(asset.path);
        }
        return out;
    }
    if (kind == "key") {
        for (int i = 1; i < int(Key::Count); i++) out.push_back(keyName(Key(i)));
        return out;
    }
    if (kind == "soundGroup") return {"Effects", "Music", "Voices", "Interface"};
    (void)scene;
    return out;
}

bool pickValueInline(EditorContext& editor, Entity owner, const std::string& kind, std::string& value, float width) {
    if (kind.empty()) {
        ImGui::SetNextItemWidth(width);
        return ImGui::InputText("##words", &value);
    }
    if (kind == "none") {
        ImGui::TextDisabled("-");
        return false;
    }
    // Worked out again once a second, not every frame: some kinds look through the files.
    struct Cached {
        std::vector<std::string> choices;
        double at = -10;
    };
    static std::map<std::string, Cached> cache;
    Cached& cached = cache[kind + "#" + std::to_string(owner.index) + "#" + std::to_string(owner.generation)];
    double now = ImGui::GetTime();
    if (now - cached.at > 1.0) {
        cached.choices = choicesFor(editor, owner, kind);
        cached.at = now;
    }
    const std::vector<std::string>& choices = cached.choices;
    bool changed = false;
    if (kind == "bones") {
        changed = chooseSeveral(kind.c_str(), value, choices, width);
    } else {
        bool allowsNew = kind == "value" || kind == "message" || kind == "variable" || kind == "tag";
        changed = chooseFrom(kind.c_str(), value, choices, true, allowsNew, width);
    }
    // A file can also be dropped on it from the Explorer.
    if (std::vector<std::string> extensions = extensionsOf(kind); !extensions.empty() && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ui::kAssetPayload)) {
            std::string dropped(static_cast<const char*>(payload->Data));
            if (hasExtension(dropped, extensions) && dropped != value) {
                value = dropped;
                changed = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    return changed;
}

bool pickValue(EditorContext& editor, Entity owner, const std::string& kind, const char* label, std::string& value) {
    if (kind.empty()) return ui::text(label, value);
    ui::label(label);
    ImGui::PushID(label);
    bool changed = pickValueInline(editor, owner, kind, value, -FLT_MIN);
    ImGui::PopID();
    return changed;
}

}  // namespace molten::editor
