// The inspector: the components of the selected entity, or the facts of the selected asset.
#include "MultiEdit.hpp"
#include "Scene/SceneFile.hpp"
#include "Editor.hpp"
#include "Assets/Gltf.hpp"
#include "Core/TextFile.hpp"
#include "Rendering/Particles.hpp"
#include "Panels/Widgets.hpp"
#include "Platform.hpp"

#include <algorithm>
#include <map>
#include <functional>
#include <filesystem>

namespace molten::editor {

namespace {

int indexOfName(const std::vector<std::string>& names, const std::string& name) {
    for (size_t i = 0; i < names.size(); i++) {
        if (names[i] == name) return int(i);
    }
    return -1;
}

bool isLoadableImage(const std::string& path) {
    std::string extension = std::filesystem::path(path).extension().string();
    for (char& c : extension) c = char(std::tolower(static_cast<unsigned char>(c)));
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" || extension == ".bmp";
}

class InspectorPanel final : public Panel {
public:
    InspectorPanel() { title = "Inspector"; }

    void draw(EditorContext& editor) override {
        SceneDocument& scene = editor.scene;
        Entity entity = scene.primary();
        if (editor.engine.scene.isAlive(entity)) {
            // Many selected: what is changed on this one is changed on all of them.
            const std::vector<Entity>& selected = scene.selection();
            if (selected.size() > 1) {
                EntityPicture before = pictureOf(editor.engine.scene, entity, scene.materials);
                drawEntity(editor, entity);
                if (editor.engine.scene.isAlive(entity)) {
                    EntityPicture after = pictureOf(editor.engine.scene, entity, scene.materials);
                    if (after.values != before.values || after.lines != before.lines) {
                        std::vector<Entity> others(selected.begin(), selected.end());
                        if (spreadChanges(editor.engine.scene, before, after, entity, others, scene.materials, sceneAssets(editor.engine.assets())) > 0) {
                            scene.markChanged();
                        }
                    }
                }
            } else {
                drawEntity(editor, entity);
            }
        } else if (!editor.selectedAsset.empty() && editor.assets.find(editor.selectedAsset)) {
            drawAsset(editor, *editor.assets.find(editor.selectedAsset));
        } else {
            ImGui::TextDisabled("Nothing selected.");
            ImGui::TextDisabled("Click something in the view, the hierarchy or the assets.");
        }
    }

private:
    // ---------------------------------------------------------------- parts of prefab copies

    /// Every value of an entity that a prefab copy can change, as text, by "Component field".
    static std::map<std::string, std::string> valuesOf(EditorContext& editor, Entity entity) {
        std::map<std::string, std::string> values;
        Scene& scene = editor.engine.scene;
        char text[128];
        if (const Transform* transform = scene.get<Transform>(entity)) {
            auto vec = [&](Vec3 v) {
                std::snprintf(text, sizeof(text), "%g %g %g", double(v.x), double(v.y), double(v.z));
                return std::string(text);
            };
            values["Transform\nposition"] = vec(transform->position);
            values["Transform\nrotation"] = vec(toEulerAngles(transform->rotation) * (180.0f / kPi));
            values["Transform\nscale"] = vec(transform->scale);
            values["Transform\nactive"] = transform->isActive ? "1" : "0";
        }
        if (const MeshRenderer* renderer = scene.get<MeshRenderer>(entity); renderer && renderer->material && !renderer->material->assetName.empty()) {
            values["MeshRenderer\nmaterial"] = renderer->material->assetName;
        }
        for (const ComponentType& type : ComponentRegistry::instance().types()) {
            void* component = type.get ? type.get(scene, entity) : nullptr;
            if (!component) continue;
            for (const Field& field : type.fields) values[type.name + "\n" + field.name] = field.valueText(component);
        }
        return values;
    }

    /// The copy a part belongs to: the nearest above it that is a prefab's copy and is in the scene's file.
    static Entity copyOf(Scene& scene, Entity part) {
        Entity at = part;
        for (int depth = 0; depth < 64 && scene.isAlive(at); depth++) {
            if (scene.has<PrefabInstance>(at) && !scene.has<FromPrefab>(at)) return at;
            const Transform* transform = scene.get<Transform>(at);
            at = transform ? transform->parent : Entity{};
        }
        return Entity{};
    }

    void drawEntity(EditorContext& editor, Entity entity) {
        Scene& scene = editor.engine.scene;
        Entity copy = scene.has<FromPrefab>(entity) ? copyOf(scene, entity) : Entity{};
        PrefabInstance* instance = scene.get<PrefabInstance>(copy);
        std::string path;
        if (!instance || !pathBelow(scene, copy, entity, path)) {
            drawEntityBody(editor, entity);
            return;
        }
        // What this part changes of its prefab, and a way to take it back.
        size_t mine = 0;
        for (const PrefabOverride& change : instance->overrides) mine += change.path == path;
        ImGui::TextColored(ImVec4(0.45f, 0.7f, 1.0f, 1), "Part of a copy of %s", instance->prefab.c_str());
        if (mine > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%zu changed here)", mine);
            ImGui::SameLine();
            if (ImGui::SmallButton("Take Back")) {
                instance->overrides.erase(std::remove_if(instance->overrides.begin(), instance->overrides.end(),
                                                         [&](const PrefabOverride& change) { return change.path == path; }),
                                          instance->overrides.end());
                editor.scene.rebuild();             // the part made again from its prefab
                return;
            }
            ui::tooltip("This part as its prefab has it again");
        }
        std::map<std::string, std::string> before = valuesOf(editor, entity);
        drawEntityBody(editor, entity);
        if (!scene.isAlive(entity)) return;
        std::map<std::string, std::string> after = valuesOf(editor, entity);
        // What changed is kept as the copy's own, so that saving the scene keeps it.
        for (const auto& [key, value] : after) {
            auto was = before.find(key);
            if (was != before.end() && was->second == value) continue;
            size_t split = key.find('\n');
            instance->setOverride(PrefabOverride{path, key.substr(0, split), key.substr(split + 1), value});
        }
    }

    void drawEntityBody(EditorContext& editor, Entity entity) {
        SceneDocument& document = editor.scene;
        Scene& scene = editor.engine.scene;

        // The top of the inspector, as in Unity: on or off, the name, then its tag and its layer.
        Transform* header = scene.get<Transform>(entity);
        if (header) {
            if (ImGui::Checkbox("##active", &header->isActive)) document.markChanged();
            ui::tooltip("Active. Off: it and everything under it is not drawn, does not collide, and its components do not run.");
            ImGui::SameLine();
        }
        std::string name = document.nameOf(entity);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("##name", &name) && !name.empty()) {
            scene.add(entity, Name{name});
            document.markChanged();
        }
        {
            Tag current = scene.get<Tag>(entity) ? *scene.get<Tag>(entity) : Tag{};
            bool tagChanged = false;
            float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            // The tags to choose from: the usual ones, and every other one the scene already uses.
            std::vector<std::string> tags = {"Untagged", "Player", "Enemy", "Pickup", "Obstacle", "Ground", "MainCamera", "Finish"};
            scene.each<Tag>([&](Entity, Tag& used) {
                if (std::find(tags.begin(), tags.end(), used.value) == tags.end()) tags.push_back(used.value);
            });
            ImGui::SetNextItemWidth(half);
            if (ImGui::BeginCombo("##tag", ("Tag: " + current.value).c_str())) {
                for (const std::string& tag : tags) {
                    if (ImGui::Selectable(tag.c_str(), tag == current.value)) {
                        current.value = tag;
                        tagChanged = true;
                    }
                }
                ImGui::Separator();
                ImGui::SetNextItemWidth(150);
                if (ImGui::InputTextWithHint("##new-tag", "New tag, then Return", &newTag_, ImGuiInputTextFlags_EnterReturnsTrue) && !newTag_.empty()) {
                    current.value = newTag_;
                    newTag_.clear();
                    tagChanged = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            static const char* const kLayers[] = {"Default", "Transparent", "Ignore Raycast", "Water", "UI", "Player", "Enemy", "Scenery"};
            std::string layerName = current.layer >= 0 && current.layer < 8 ? kLayers[current.layer] : "Layer " + std::to_string(current.layer);
            ImGui::SetNextItemWidth(half);
            if (ImGui::BeginCombo("##layer", ("Layer: " + layerName).c_str())) {
                for (int layer = 0; layer < 32; layer++) {
                    std::string label = layer < 8 ? std::string(kLayers[layer]) : "Layer " + std::to_string(layer);
                    if (ImGui::Selectable((std::to_string(layer) + "  " + label).c_str(), layer == current.layer)) {
                        current.layer = layer;
                        tagChanged = true;
                        // A collider is on its entity's layer: what it meets is decided by layers.
                        if (Collider* collider = scene.get<Collider>(entity)) collider->layer = 1u << uint32_t(layer);
                    }
                }
                ImGui::EndCombo();
            }
            if (tagChanged) {
                scene.add(entity, current);
                document.markChanged();
            }
        }
        if (document.selection().size() > 1) {
            ImGui::TextDisabled("%zu entities selected: what is changed here is changed on all of them.", document.selection().size());
        }
        ImGui::Spacing();

        if (Transform* transform = scene.get<Transform>(entity)) {
            if (ui::section("Transform")) {
                bool changed = ui::dragVec3("Position", transform->position, 0.02f);
                // Angles are shown in degrees and remembered while the rotation stays the same:
                // converting the rotation back to angles every frame would make the numbers jump.
                if (shownFor_ != entity || simd::dot(shownRotation_.vector, transform->rotation.vector) < 0.999999f) {
                    shownAngles_ = toEulerAngles(transform->rotation) * (180.0f / kPi);
                    // No "-0.0": an angle too small to show is shown as none.
                    for (int axis = 0; axis < 3; axis++) {
                        if (std::abs(shownAngles_[axis]) < 0.005f) shownAngles_[axis] = 0.0f;
                    }
                    shownRotation_ = transform->rotation;
                    shownFor_ = entity;
                }
                if (ui::dragVec3("Rotation", shownAngles_, 0.5f, "%.1f\xC2\xB0")) {
                    transform->rotation = eulerAngles(radians(shownAngles_.x), radians(shownAngles_.y), radians(shownAngles_.z));
                    shownRotation_ = transform->rotation;
                    changed = true;
                }
                changed |= ui::dragVec3("Scale", transform->scale, 0.01f);
                if (ImGui::BeginPopupContextItem("##transform-menu")) {
                    if (ImGui::MenuItem("Reset")) {
                        transform->position = Vec3{0, 0, 0};
                        transform->rotation = Quat{};
                        transform->scale = Vec3{1, 1, 1};
                        changed = true;
                    }
                    ImGui::EndPopup();
                }
                if (changed) document.markChanged();
            }
        }

        // One section per component the entity has, in the order they were registered.
        for (auto& item : editor.registry.components) {
            ComponentEditor& component = item.value;
            if (!component.has(scene, entity)) continue;
            ImGui::PushID(component.name.c_str());
            float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
            bool isOpen = ui::section(component.name.c_str());
            // On or off, for the components that can be: at the right of the header, before its menu.
            bool* enabled = nullptr;
            if (component.name == "Mesh Renderer") enabled = &scene.get<MeshRenderer>(entity)->isVisible;
            else if (component.name == "Light") enabled = &scene.get<Light>(entity)->isEnabled;
            else if (component.name == "Camera") enabled = &scene.get<Camera>(entity)->isActive;
            else if (component.name == "Particle System") enabled = &scene.get<ParticleEmitter>(entity)->isEmitting;
            if (enabled) {
                ImGui::SameLine(rightEdge - 62);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
                if (ImGui::Checkbox("##enabled", enabled)) document.markChanged();
                ImGui::PopStyleVar();
                ui::tooltip("Enabled");
            }
            // A small button at the right end of the header: what can be done to the component.
            ImGui::SameLine(rightEdge - 30);
            if (ImGui::SmallButton("...")) ImGui::OpenPopup("##component-menu");
            bool removed = false;
            if (ImGui::BeginPopup("##component-menu")) {
                if (ImGui::MenuItem("Reset")) {
                    // Back to what a new one has.
                    component.remove(scene, entity);
                    component.add(scene, entity);
                    document.markChanged();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Remove Component")) removed = true;
                ImGui::EndPopup();
            }
            if (removed) {
                component.remove(scene, entity);
                document.markChanged();
            } else if (isOpen && component.draw(editor, entity)) {
                document.markChanged();
            }
            ImGui::PopID();
        }

        // The components the game's code declared: their fields are known, so they need no editor of their own.
        for (size_t i = 0; i < ComponentRegistry::instance().types().size(); i++) {
            const ComponentType& type = ComponentRegistry::instance().types()[i];
            void* component = type.get(scene, entity);
            if (!component) continue;
            ImGui::PushID(type.name.c_str());
            float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
            bool isOpen = ui::section((ComponentRegistry::displayName(type.name) + "###" + type.name).c_str());
            {
                // On or off: a component switched off keeps its values and does nothing.
                const DisabledComponents* disabled = scene.get<DisabledComponents>(entity);
                bool isEnabled = !disabled || !disabled->has(type.name);
                ImGui::SameLine(rightEdge - 62);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
                if (ImGui::Checkbox("##enabled", &isEnabled)) {
                    DisabledComponents updated = disabled ? *disabled : DisabledComponents{};
                    updated.names.erase(std::remove(updated.names.begin(), updated.names.end(), type.name), updated.names.end());
                    if (!isEnabled) updated.names.push_back(type.name);
                    scene.add(entity, updated);
                    document.markChanged();
                    component = type.get(scene, entity);
                }
                ImGui::PopStyleVar();
                ui::tooltip("Enabled");
            }
            ImGui::SameLine(rightEdge - 30);
            if (ImGui::SmallButton("...")) ImGui::OpenPopup("##component-menu");
            bool removed = false;
            if (ImGui::BeginPopup("##component-menu")) {
                if (ImGui::MenuItem("Reset")) {
                    type.remove(scene, entity);
                    type.add(scene, entity);
                    document.markChanged();
                    component = type.get(scene, entity);
                }
                if (ImGui::MenuItem("Copy Values")) {
                    copiedType_ = type.name;
                    copiedValues_.clear();
                    for (const Field& field : type.fields) copiedValues_.push_back({field.name, field.valueText(component)});
                }
                if (ImGui::MenuItem("Paste Values", nullptr, false, copiedType_ == type.name)) {
                    for (const Field& field : type.fields) {
                        for (const auto& [name, value] : copiedValues_) {
                            if (name == field.name) field.setValueText(component, value);
                        }
                    }
                    document.markChanged();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Remove Component")) removed = true;
                ImGui::EndPopup();
            }
            if (removed) {
                type.remove(scene, entity);
                document.markChanged();
            } else if (isOpen && type.name == "VisualScript") {
                if (drawVisualScript(editor, *static_cast<VisualScript*>(component))) document.markChanged();
            } else if (isOpen) {
                if (type.fields.empty()) ImGui::TextDisabled("No fields.");
                for (const Field& field : type.fields) {
                    if (drawField(field, component)) document.markChanged();
                }
            }
            ImGui::PopID();
        }
        // The material of its mesh, as in the mesh's own file: what is changed here changes
        // everything that uses the material.
        if (MeshRenderer* renderer = scene.get<MeshRenderer>(entity); renderer && renderer->material) {
            Material& material = *renderer->material;
            std::string title = "Material: " + materialDisplayName(editor, renderer->material) + "###material";
            if (ui::section(title.c_str())) {
                ImGui::PushID("material");
                bool isAsset = !material.assetName.empty();
                ImGui::TextDisabled(isAsset ? "An asset: shared by everything that uses it." : "Only in this scene. The Material menu above can save it as an asset.");
                if (drawMaterialProperties(editor, material)) {
                    if (isAsset) saveMaterialAsset(editor, material);
                    else document.markChanged();
                }
                ImGui::PopID();
            }
        }
        if (const UnknownComponents* unknown = scene.get<UnknownComponents>(entity)) {
            if (ui::section("Unknown Components")) {
                ImGui::TextWrapped("This entity has components whose code is not loaded. They are kept as they are and saved with the scene:");
                for (const std::string& kept : unknown->lines) {
                    if (kept.find("component") != std::string::npos && kept.find("field") == std::string::npos) ImGui::BulletText("%s", kept.c_str());
                }
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        float width = std::min(ImGui::GetContentRegionAvail().x, 220.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - width) * 0.5f);
        if (ImGui::Button("Add Component", ImVec2(width, 0))) ImGui::OpenPopup("##add-component");
        ImGui::SetNextWindowSizeConstraints(ImVec2(260, 0), ImVec2(380, 460));
        if (ImGui::BeginPopup("##add-component")) {
            // Everything that can be added and is not there yet, each under its category.
            struct Choice {
                std::string category, name;
                std::function<void()> add;
            };
            std::vector<Choice> choices;
            for (auto& item : editor.registry.components) {
                if (item.value.has(scene, entity)) continue;
                const std::string& name = item.value.name;
                const char* category = name == "Mesh Renderer" ? "Mesh" : name == "Camera" || name == "Light" ? "Rendering"
                                     : name.find("Collider") != std::string::npos || name == "Rigidbody" ? "Physics" : name == "Particle System" ? "Effects" : "Miscellaneous";
                ComponentEditor* component = &item.value;
                choices.push_back({category, name, [component, &scene, entity] { component->add(scene, entity); }});
            }
            for (size_t i = 0; i < ComponentRegistry::instance().types().size(); i++) {
                const ComponentType& type = ComponentRegistry::instance().types()[i];
                if (type.has(scene, entity) || type.category == "Hidden") continue;
                auto add = type.add;
                choices.push_back({type.category, ComponentRegistry::displayName(type.name), [add, &scene, entity] { add(scene, entity); }});
            }
            if (ImGui::IsWindowAppearing()) {
                addFilter_.clear();
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##search", "Search", &addFilter_);
            ImGui::Separator();
            auto matches = [&](const std::string& name) {
                auto found = std::search(name.begin(), name.end(), addFilter_.begin(), addFilter_.end(), [](char a, char b) {
                    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                });
                return found != name.end();
            };
            const Choice* picked = nullptr;
            if (!addFilter_.empty()) {
                // Searching: one flat list of what matches, Return takes the first.
                const Choice* first = nullptr;
                for (const Choice& choice : choices) {
                    if (!matches(choice.name)) continue;
                    if (!first) first = &choice;
                    if (ImGui::MenuItem(choice.name.c_str(), choice.category.c_str())) picked = &choice;
                }
                if (!first) ImGui::TextDisabled("Nothing is called that.");
                else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)) picked = first;
            } else {
                // The order people look for things in: what is seen, then how it behaves.
                std::vector<std::string> categories = {"Mesh", "Rendering", "Physics", "Audio", "Effects", "UI", "Vehicles", "Gameplay", "Scripts"};
                for (const Choice& choice : choices) {
                    if (std::find(categories.begin(), categories.end(), choice.category) == categories.end()) categories.push_back(choice.category);
                }
                for (const std::string& category : categories) {
                    bool any = false;
                    for (const Choice& choice : choices) any = any || choice.category == category;
                    if (!any) continue;
                    if (ImGui::BeginMenu(category.c_str())) {
                        for (const Choice& choice : choices) {
                            if (choice.category == category && ImGui::MenuItem(choice.name.c_str())) picked = &choice;
                        }
                        ImGui::EndMenu();
                    }
                }
                if (choices.empty()) ImGui::TextDisabled("It has them all.");
            }
            if (picked) {
                picked->add();
                document.markChanged();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    /// The Visual Script component: which of the project's scripts, and a way to its drawing board.
    static bool drawVisualScript(EditorContext& editor, VisualScript& component) {
        std::vector<std::string> scripts;
        for (const AssetInfo& asset : editor.assets.assets()) {
            if (asset.type == AssetType::Graph) scripts.push_back(asset.path);
        }
        int index = indexOfName(scripts, component.graph);
        bool changed = false;
        if (ui::combo("Script", index, scripts)) {
            component.graph = scripts[size_t(index)];
            changed = true;
        }
        std::string dropped;
        if (ui::acceptAsset(editor.assets, {AssetType::Graph}, dropped)) {
            component.graph = dropped;
            changed = true;
        }
        if (scripts.empty()) ImGui::TextWrapped("The project has no visual scripts yet. Make one in the Visual Script window.");
        if (!component.graph.empty() && index < 0) ImGui::TextWrapped("\"%s\" is not in the project.", component.graph.c_str());
        ImGui::BeginDisabled(index < 0);
        if (ImGui::Button("Edit Script")) editor.editGraph(component.graph);
        ImGui::EndDisabled();
        return changed;
    }

    /// One field of a game component, with the control that suits its type.
    static bool drawField(const Field& field, void* component) {
        std::string label = field.label();
        void* at = field.address(component);
        bool hasRange = field.minimum < field.maximum;
        bool changed = false;
        switch (field.type) {
            case FieldType::Float:
                changed = hasRange ? ui::sliderFloat(label.c_str(), *static_cast<float*>(at), field.minimum, field.maximum, "%.3f")
                                   : ui::dragFloat(label.c_str(), *static_cast<float*>(at), 0.01f);
                break;
            case FieldType::Int:
                changed = ui::dragInt(label.c_str(), *static_cast<int*>(at), 0.2f, hasRange ? int(field.minimum) : -1000000000,
                                      hasRange ? int(field.maximum) : 1000000000);
                break;
            case FieldType::Bool: changed = ui::checkbox(label.c_str(), *static_cast<bool*>(at)); break;
            case FieldType::Vec2: changed = ui::dragVec2(label.c_str(), *static_cast<Vec2*>(at), 0.01f); break;
            case FieldType::Vec3:
                changed = field.isColor ? ui::color(label.c_str(), *static_cast<Vec3*>(at), true)
                                        : ui::dragVec3(label.c_str(), *static_cast<Vec3*>(at), 0.01f);
                break;
            case FieldType::Vec4: {
                Vec4& value = *static_cast<Vec4*>(at);
                if (field.isColor) {
                    changed = ui::color(label.c_str(), value);
                } else {
                    ui::label(label.c_str());
                    ImGui::PushID(label.c_str());
                    float v[4] = {value.x, value.y, value.z, value.w};
                    changed = ImGui::DragFloat4("##value", v, 0.01f);
                    if (changed) value = Vec4{v[0], v[1], v[2], v[3]};
                    ImGui::PopID();
                }
                break;
            }
            case FieldType::String: {
                std::string& value = *static_cast<std::string*>(at);
                if (field.options.empty()) {
                    changed = ui::text(label.c_str(), value);
                } else {
                    // One of a few words: a list, not a place to mistype them.
                    int index = -1;
                    for (size_t i = 0; i < field.options.size(); i++) {
                        if (field.options[i] == value) index = int(i);
                    }
                    if (ui::combo(label.c_str(), index, field.options)) {
                        value = field.options[size_t(index)];
                        changed = true;
                    }
                }
                break;
            }
        }
        if (!field.tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", field.tooltip.c_str());
        return changed;
    }

    void drawAsset(EditorContext& editor, const AssetInfo& asset) {
        ImGui::TextUnformatted(asset.name.c_str());
        ImGui::TextDisabled("%s", AssetDatabase::typeName(asset.type));
        ImGui::Separator();
        ui::info("Path", "Assets/" + asset.path);
        if (!asset.isFolder()) {
            ui::info("Size", ui::byteSize(asset.size));
            ui::info("Identifier", asset.guid);
        }
        ImGui::Spacing();

        if (asset.type == AssetType::Texture && isLoadableImage(asset.path)) {
            if (std::shared_ptr<Texture> texture = editor.engine.assets().texture(asset.path)) {
                float width = float(texture->handle()->width()), height = float(texture->handle()->height());
                ui::info("Pixels", std::to_string(int(width)) + " x " + std::to_string(int(height)));
                float shown = std::min(ImGui::GetContentRegionAvail().x, 512.0f);
                ImGui::Image(ImGuiLayer::textureId(texture.get()), ImVec2(shown, shown * height / std::max(width, 1.0f)));
            }
        } else if (asset.type == AssetType::Mesh) {
            bool canLoad = std::filesystem::path(asset.path).extension() == ".obj" || isGltfFile(asset.path);
            if (!canLoad) ImGui::TextWrapped("This format cannot be imported yet: only Wavefront OBJ. glTF is next on the roadmap.");
            ImGui::BeginDisabled(!canLoad);
            if (ImGui::Button("Add to Scene")) {
                Entity created = editor.scene.createFromMesh(asset.path, Vec3{0, 0, 0});
                if (!created.isValid()) editor.notify("Cannot load " + asset.path, ConsoleMessage::Level::Error);
            }
            ImGui::EndDisabled();
        } else if (asset.type == AssetType::Material) {
            if (std::shared_ptr<Material> material = editor.engine.assets().material(asset.path)) {
                ImVec2 at = ImGui::GetCursorScreenPos();
                float size = std::min(ImGui::GetContentRegionAvail().x, 120.0f);
                ImGui::Dummy(ImVec2(size, size + 6));
                drawMaterialPreview(ImGui::GetWindowDrawList(), ImVec2(at.x + size * 0.5f, at.y + size * 0.5f), size * 0.46f, *material);
                if (drawMaterialProperties(editor, *material)) saveMaterialAsset(editor, *material);
                ImGui::Spacing();
                ImGui::TextDisabled("Drag it onto something in the view, or into a mesh's Material.");
            } else {
                ImGui::TextWrapped("This file could not be read as a material.");
            }
        } else if (asset.type == AssetType::Scene) {
            if (ImGui::Button("Open Scene")) editor.requestOpenScene(editor.assets.absolutePath(asset.path));
        } else if (asset.type == AssetType::Graph) {
            if (ImGui::Button("Edit Script")) editor.editGraph(asset.path);
        }

        ImGui::Spacing();
        if (ImGui::Button("Reveal in Finder")) platform::revealInFinder(editor.assets.absolutePath(asset.path));
        if (!asset.isFolder()) {
            ImGui::SameLine();
            if (ImGui::Button("Open")) platform::openWithDefaultApplication(editor.assets.absolutePath(asset.path));
        }
    }

    Entity shownFor_;
    std::string newTag_;
    std::string addFilter_;                                             // what is typed in Add Component
    std::string copiedType_;                                            // Copy Values: of which type, and the values as text
    std::vector<std::pair<std::string, std::string>> copiedValues_;
    Vec3 shownAngles_{0, 0, 0};
    Quat shownRotation_{};
};

}  // namespace

void registerInspectorPanel(EditorContext& editor) {
    editor.registry.addPanel(nullptr, std::make_unique<InspectorPanel>());
}

std::vector<std::string> meshChoices(EditorContext& editor) {
    std::vector<std::string> names = {"cube", "sphere", "plane", "cylinder"};
    for (const AssetInfo& asset : editor.assets.assets()) {
        if (asset.type == AssetType::Mesh && (std::filesystem::path(asset.path).extension() == ".obj" || isGltfFile(asset.path))) names.push_back(asset.path);
    }
    return names;
}

bool textureSlot(EditorContext& editor, const char* name, std::shared_ptr<Texture>& slot, TextureFormat format) {
    bool changed = false;
    ui::label(name);
    ImGui::PushID(name);
    float side = ImGui::GetFrameHeight() * 1.6f;
    ImVec2 at = ImGui::GetCursorScreenPos();
    if (slot) {
        ImGui::Image(ImGuiLayer::textureId(slot.get()), ImVec2(side, side));
    } else {
        ImGui::Dummy(ImVec2(side, side));
        ImGui::GetWindowDrawList()->AddRect(at, ImVec2(at.x + side, at.y + side), ImGui::GetColorU32(ImGuiCol_TextDisabled), 3.0f);
    }
    std::string dropped;
    if (ui::acceptAsset(editor.assets, {AssetType::Texture}, dropped)) {
        slot = editor.engine.assets().texture(dropped, format);
        changed = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    std::string shown = slot ? (slot->name.empty() ? std::string("(made in code)") : slot->name) : std::string("None");
    if (ImGui::BeginCombo("##choice", shown.c_str())) {
        if (ImGui::Selectable("None", !slot) && slot) {
            slot.reset();
            changed = true;
        }
        for (const AssetInfo& asset : editor.assets.assets()) {
            if (asset.type != AssetType::Texture || !isLoadableImage(asset.path)) continue;
            bool isCurrent = slot && slot->name == asset.path;
            if (ImGui::Selectable(asset.path.c_str(), isCurrent) && !isCurrent) {
                slot = editor.engine.assets().texture(asset.path, format);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return changed;
}

// ---------------------------------------------------------------- the engine's own components

bool drawEmitterProperties(EditorContext& editor, ParticleEmitter& emitter, bool offersFiles) {
    bool changed = false;
    // Start from an effect that exists: one of the engine's, or a file of the project.
    if (ImGui::Button("Start From...")) ImGui::OpenPopup("##presets");
    if (ImGui::BeginPopup("##presets")) {
        ImGui::TextDisabled("The engine's");
        for (const std::string& name : effectPresets()) {
            if (ImGui::MenuItem(name.c_str())) {
                emitter = effectPreset(name);
                changed = true;
            }
        }
        bool any = false;
        for (const AssetInfo& asset : editor.assets.assets()) {
            if (asset.type != AssetType::Effect) continue;
            if (!any) {
                ImGui::Separator();
                ImGui::TextDisabled("The project's");
            }
            any = true;
            if (ImGui::MenuItem(asset.path.c_str())) {
                std::optional<std::string> contents = text::readFile(editor.assets.absolutePath(asset.path));
                if (contents && readEffect(*contents, emitter)) changed = true;
            }
        }
        ImGui::EndPopup();
    }
    if (offersFiles) {
        ImGui::SameLine();
        if (ImGui::Button("Save as Effect")) {
            // A file of its own, to put on other things and in other scenes.
            std::error_code ignored;
            std::filesystem::create_directories(editor.assets.absolutePath("Effects"), ignored);
            std::string path = editor.assets.uniquePath("Effects", "Effect", ".effect");
            if (text::writeFile(editor.assets.absolutePath(path), writeEffect(emitter))) {
                editor.assets.refresh();
                editor.notify("Saved " + path);
            }
        }
    }

    if (ui::section("Emission")) {
        changed |= ui::checkbox("Emitting", emitter.isEmitting);
        changed |= ui::dragFloat("Rate", emitter.rate, 0.5f, 0.0f, 5000.0f, "%.0f per second");
        changed |= ui::dragInt("Most at Once", emitter.maxParticles, 4.0f, 1, 100000);
        changed |= ui::dragInt("Burst at Start", emitter.burstAtStart, 1.0f, 0, 100000);
        changed |= ui::dragFloat("Burst Every", emitter.burstEvery, 0.01f, 0.0f, 600.0f, "%.2f s");
        ui::tooltip("0: no bursts after the first");
        if (emitter.burstEvery > 0) changed |= ui::dragInt("Particles in a Burst", emitter.burstCount, 1.0f, 0, 100000);
        changed |= ui::dragFloat("Lasts", emitter.duration, 0.01f, 0.0f, 600.0f, "%.2f s");
        ui::tooltip("It emits for this long and stops. 0: for as long as it is there.");
        if (emitter.duration > 0) changed |= ui::checkbox("Starts Over", emitter.loops);
    }
    if (ui::section("Shape")) {
        int shape = int(emitter.shape);
        if (ui::combo("Born", shape, {"In a sphere", "In a box", "On a ring"})) {
            emitter.shape = ParticleEmitter::Shape(shape);
            changed = true;
        }
        if (emitter.shape == ParticleEmitter::Shape::Box) changed |= ui::dragVec3("Box Size", emitter.boxSize, 0.02f);
        else changed |= ui::dragFloat("Radius", emitter.spawnRadius, 0.01f, 0.0f, 1000.0f);
        changed |= ui::dragVec3("Direction", emitter.direction, 0.01f);
        changed |= ui::dragDegrees("Spread", emitter.spread, 0.0f, 180.0f);
        ui::tooltip("How wide the cone they leave in is: 180 is every direction");
    }
    if (ui::section("Life and Motion")) {
        changed |= ui::dragFloat("Life, Shortest", emitter.lifetimeMin, 0.01f, 0.01f, 60.0f);
        changed |= ui::dragFloat("Life, Longest", emitter.lifetimeMax, 0.01f, 0.01f, 60.0f);
        changed |= ui::dragFloat("Speed, Slowest", emitter.speedMin, 0.02f, 0.0f, 1000.0f);
        changed |= ui::dragFloat("Speed, Fastest", emitter.speedMax, 0.02f, 0.0f, 1000.0f);
        changed |= ui::dragVec3("Gravity", emitter.gravity, 0.02f);
        changed |= ui::dragFloat("Drag", emitter.drag, 0.01f, 0.0f, 20.0f);
        changed |= ui::sliderFloat("Wind Influence", emitter.windInfluence, 0.0f, 1.0f);
        changed |= ui::dragFloat("Turbulence", emitter.turbulence, 0.01f, 0.0f, 20.0f);
        ui::tooltip("Swirling, as in smoke that curls");
        if (emitter.turbulence > 0) changed |= ui::dragFloat("Swirl Size", emitter.turbulenceSize, 0.01f, 0.05f, 100.0f, "%.2f m");
        changed |= ui::dragFloat("Streak", emitter.stretch, 0.001f, 0.0f, 1.0f, "%.3f s");
        ui::tooltip("Each particle drawn as a streak along the way it goes, this many seconds of it long: rain, sparks. 0 for round dots.");
        changed |= ui::checkbox("Bounces", emitter.bounces);
        ui::tooltip("Off a level floor");
        if (emitter.bounces) {
            changed |= ui::dragFloat("Floor Height", emitter.floorHeight, 0.01f);
            changed |= ui::sliderFloat("Bounciness", emitter.bounciness, 0.0f, 1.0f);
        }
    }
    if (ui::section("Look")) {
        changed |= ui::color("Color at Birth", emitter.colorStart);
        changed |= ui::dragFloat("Size at Birth", emitter.sizeStart, 0.005f, 0.0f, 100.0f);
        changed |= ui::checkbox("A Middle", emitter.hasMiddle);
        ui::tooltip("A third color and size on the way: fire that is yellow, then orange, then smoke");
        if (emitter.hasMiddle) {
            changed |= ui::sliderFloat("Middle At", emitter.middleAt, 0.02f, 0.98f);
            changed |= ui::color("Color at the Middle", emitter.colorMiddle);
            changed |= ui::dragFloat("Size at the Middle", emitter.sizeMiddle, 0.005f, 0.0f, 100.0f);
        }
        changed |= ui::color("Color at Death", emitter.colorEnd);
        changed |= ui::dragFloat("Size at Death", emitter.sizeEnd, 0.005f, 0.0f, 100.0f);
        changed |= ui::checkbox("Additive", emitter.isAdditive);
        ui::tooltip("Light added to what is behind (fire, sparks, magic) instead of covering it (smoke, dust)");
    }
    emitter.lifetimeMax = std::max(emitter.lifetimeMax, emitter.lifetimeMin);
    emitter.speedMax = std::max(emitter.speedMax, emitter.speedMin);
    return changed;
}

void registerBuiltinComponents(EditorContext& editor) {
    Registry& registry = editor.registry;

    registry.addComponent<MeshRenderer>(nullptr, "Mesh Renderer", [](EditorContext& context, Entity, MeshRenderer& renderer) {
        bool changed = false;
        std::vector<std::string> meshes = meshChoices(context);
        // One part of a model ("Models/car.glb#3") is not in the list of files: show it all the same.
        if (renderer.mesh && !renderer.mesh->name.empty() && indexOfName(meshes, renderer.mesh->name) < 0) meshes.push_back(renderer.mesh->name);
        int meshIndex = renderer.mesh ? indexOfName(meshes, renderer.mesh->name) : -1;
        if (ui::combo("Mesh", meshIndex, meshes)) {
            renderer.mesh = context.engine.assets().mesh(meshes[size_t(meshIndex)]);
            changed = true;
        }
        std::string dropped;
        if (ui::acceptAsset(context.assets, {AssetType::Mesh}, dropped)) {
            if (std::shared_ptr<Mesh> mesh = context.engine.assets().mesh(dropped)) {
                renderer.mesh = mesh;
                changed = true;
            }
        }
        changed |= drawMaterialSlot(context, renderer.material);
        changed |= ui::color("Tint", renderer.tint);
        changed |= ui::checkbox("Casts Shadows", renderer.castsShadows);
        changed |= ui::checkbox("Visible", renderer.isVisible);
        return changed;
    });

    registry.addComponent<Light>(nullptr, "Light", [](EditorContext&, Entity, Light& light) {
        bool changed = false;
        int type = int(light.type);
        if (ui::combo("Type", type, {"Directional", "Point", "Spot"})) {
            light.type = LightType(type);
            changed = true;
        }
        changed |= ui::color("Color", light.color);
        changed |= ui::dragFloat("Intensity", light.intensity, 0.05f, 0.0f, 1000.0f);
        if (light.type != LightType::Directional) changed |= ui::dragFloat("Range", light.range, 0.05f, 0.1f, 500.0f);
        if (light.type == LightType::Spot) {
            changed |= ui::dragDegrees("Inner Cone", light.innerConeAngle, 0.0f, 89.0f);
            changed |= ui::dragDegrees("Outer Cone", light.outerConeAngle, 0.0f, 89.0f);
            light.outerConeAngle = std::max(light.outerConeAngle, light.innerConeAngle);
        }
        if (light.type == LightType::Directional) changed |= ui::checkbox("Casts Shadows", light.castsShadows);
        return changed;
    });

    registry.addComponent<Camera>(nullptr, "Camera", [](EditorContext&, Entity, Camera& camera) {
        bool changed = ui::checkbox("Orthographic", camera.isOrthographic);
        if (camera.isOrthographic) changed |= ui::dragFloat("Height", camera.orthographicHeight, 0.05f, 0.01f, 10000.0f);
        else changed |= ui::dragDegrees("Field of View", camera.fovY, 5.0f, 150.0f);
        changed |= ui::dragFloat("Near", camera.nearPlane, 0.01f, 0.01f, 10.0f);
        changed |= ui::dragFloat("Far", camera.farPlane, 1.0f, 1.0f, 100000.0f);
        changed |= ui::checkbox("Used When Playing", camera.isActive);
        return changed;
    });

    // One editor for each shape of collider, as separate components to add: Box Collider,
    // Sphere Collider, Capsule Collider, Mesh Collider (convex or not). An object has one
    // collider: adding another shape changes it; more shapes are more objects under it, which
    // a vehicle collides with as its own body.
    auto drawCollider = [](EditorContext& context, Entity entity, Collider& collider) {
        bool changed = false;
        if (collider.shape == ColliderShape::Mesh) {
            // Chosen from the models there are (or dropped from the Explorer), never typed.
            std::vector<std::string> models = meshChoices(context);
            const MeshRenderer* shown = context.engine.scene.get<MeshRenderer>(entity);
            std::vector<std::string> labels = {"Its Mesh Renderer's model"};
            for (const std::string& model : models) labels.push_back(model);
            int index = collider.meshName.empty() ? 0 : indexOfName(models, collider.meshName) + 1;
            if (index == 0 && !collider.meshName.empty()) {
                labels.push_back(collider.meshName);                    // a part of a model, or a file that is gone: shown as it is
                index = int(labels.size()) - 1;
            }
            auto use = [&](const std::string& name) {
                collider.meshName = name;
                collider.mesh = nullptr;
                collider.meshScale = Vec3{0, 0, 0};
                changed = true;
            };
            if (ui::combo("Model", index, labels)) use(index == 0 ? (shown && shown->mesh ? shown->mesh->name : std::string()) : labels[size_t(index)]);
            ui::tooltip("The model whose triangles it is. The object's scale is its size");
            std::string dropped;
            if (ui::acceptAsset(context.assets, {AssetType::Mesh}, dropped)) use(dropped);
            if (ui::checkbox("Convex", collider.isConvex)) {
                collider.convex = nullptr;
                collider.meshScale = Vec3{0, 0, 0};
                changed = true;
            }
            ui::tooltip("Wrapped as a convex shape (no hollows, no holes): it can then move with a Rigidbody, fall, roll and be hit, "
                        "and be a vehicle's body. Off: the model's own triangles, for things that stand still");
            if (collider.isConvex) {
                ImGui::TextDisabled(collider.convex ? "%d corners, %d faces. It can move." : "Wrapped when the game runs.",
                                    collider.convex ? int(collider.convex->points.size()) : 0, collider.convex ? int(collider.convex->planes.size()) : 0);
            } else {
                ImGui::TextDisabled(collider.mesh ? "%d triangles. For things that stand still: other shapes collide with it." : "Read when the game runs.",
                                    collider.mesh ? int(collider.mesh->triangleCount()) : 0);
                if (context.engine.scene.get<RigidBody>(entity)) {
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1), "A Rigidbody moves it: turn Convex on, or it will not collide as it moves.");
                }
            }
            changed |= ui::checkbox("Trigger", collider.isTrigger);
            return changed;
        } else if (collider.shape == ColliderShape::Box) {
            changed |= ui::dragVec3("Half Size", collider.halfExtents, 0.01f);
        } else {
            changed |= ui::dragFloat("Radius", collider.radius, 0.01f, 0.01f, 1000.0f);
            if (collider.shape == ColliderShape::Capsule) changed |= ui::dragFloat("Half Height", collider.halfHeight, 0.01f, 0.0f, 1000.0f);
        }
        changed |= ui::dragVec3("Offset", collider.offset, 0.01f);
        changed |= ui::checkbox("Trigger", collider.isTrigger);
        Scene& scene = context.engine.scene;
        const MeshRenderer* renderer = scene.get<MeshRenderer>(entity);
        const Transform* transform = scene.get<Transform>(entity);
        if (renderer && renderer->mesh && transform && ImGui::Button("Fit to Mesh")) {
            // Colliders ignore Transform::scale, so the scale goes into their size.
            const AABB& box = renderer->mesh->bounds();
            Vec3 half = box.halfExtents() * transform->scale;
            collider.halfExtents = half;
            collider.radius = collider.shape == ColliderShape::Sphere ? std::max({half.x, half.y, half.z}) : std::max(half.x, half.z);
            collider.halfHeight = std::max(half.y - collider.radius, 0.0f);
            collider.offset = box.center() * transform->scale;
            changed = true;
        }
        return changed;
    };
    struct Shape {
        const char* name;
        ColliderShape shape;
    };
    for (Shape each : {Shape{"Box Collider", ColliderShape::Box}, Shape{"Sphere Collider", ColliderShape::Sphere}, Shape{"Capsule Collider", ColliderShape::Capsule},
                       Shape{"Mesh Collider", ColliderShape::Mesh}}) {
        ComponentEditor editor;
        editor.name = each.name;
        ColliderShape shape = each.shape;
        editor.has = [shape](Scene& scene, Entity entity) {
            const Collider* collider = scene.get<Collider>(entity);
            return collider && collider->shape == shape;
        };
        editor.add = [shape](Scene& scene, Entity entity) {
            Collider collider = scene.get<Collider>(entity) ? *scene.get<Collider>(entity) : Collider{};
            collider.shape = shape;
            if (shape == ColliderShape::Mesh) {
                // The model the object draws is the shape it is solid in.
                const MeshRenderer* shown = scene.get<MeshRenderer>(entity);
                if (collider.meshName.empty() && shown && shown->mesh) collider.meshName = shown->mesh->name;
                collider.mesh = nullptr;
                collider.meshScale = Vec3{0, 0, 0};
            }
            // The size of what it is on, if that draws a mesh.
            const MeshRenderer* renderer = scene.get<MeshRenderer>(entity);
            const Transform* transform = scene.get<Transform>(entity);
            if (renderer && renderer->mesh && transform && !scene.get<Collider>(entity)) {
                Vec3 half = renderer->mesh->bounds().halfExtents() * transform->scale;
                collider.halfExtents = simd::max(half, Vec3{0.01f, 0.01f, 0.01f});
                collider.radius = shape == ColliderShape::Sphere ? std::max({half.x, half.y, half.z}) : std::max(half.x, half.z);
                collider.halfHeight = std::max(half.y - collider.radius, 0.0f);
                collider.offset = renderer->mesh->bounds().center() * transform->scale;
            }
            if (scene.get<Collider>(entity)) *scene.get<Collider>(entity) = collider;
            else scene.add(entity, collider);
        };
        editor.remove = [](Scene& scene, Entity entity) { scene.remove<Collider>(entity); };
        editor.draw = [drawCollider](EditorContext& context, Entity entity) {
            Collider* collider = context.engine.scene.get<Collider>(entity);
            return collider ? drawCollider(context, entity, *collider) : false;
        };
        registry.addComponent(nullptr, std::move(editor));
    }

    registry.addComponent<RigidBody>(nullptr, "Rigidbody", [](EditorContext&, Entity, RigidBody& body) {
        bool changed = false;
        int type = int(body.type);
        if (ui::combo("Type", type, {"Static", "Kinematic", "Dynamic"})) {
            body.type = BodyType(type);
            changed = true;
        }
        changed |= ui::dragFloat("Mass", body.mass, 0.05f, 0.001f, 100000.0f);
        changed |= ui::sliderFloat("Bounce", body.restitution, 0.0f, 1.0f);
        changed |= ui::sliderFloat("Friction", body.friction, 0.0f, 2.0f);
        changed |= ui::dragFloat("Air Resistance", body.linearDamping, 0.005f, 0.0f, 10.0f);
        changed |= ui::dragFloat("Gravity Scale", body.gravityScale, 0.01f, -10.0f, 10.0f);
        changed |= ui::checkbox("Rotates", body.rotates);
        ui::tooltip("Turns when hit off-center, rolls, tumbles. Off, it keeps the way it faces: what a character that walks wants");
        if (body.rotates) changed |= ui::dragFloat("Turning Resistance", body.angularDamping, 0.005f, 0.0f, 10.0f);
        return changed;
    });

    registry.addComponent<ParticleEmitter>(nullptr, "Particle System", [](EditorContext& editor, Entity, ParticleEmitter& emitter) {
        return drawEmitterProperties(editor, emitter, true);
    });
}

}  // namespace molten::editor
