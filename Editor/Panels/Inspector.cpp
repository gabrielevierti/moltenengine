// The inspector: the components of the selected entity, or the facts of the selected asset.
#include "MultiEdit.hpp"
#include "Scene/SceneFile.hpp"
#include "Editor.hpp"
#include "Assets/Gltf.hpp"
#include "Core/TextFile.hpp"
#include "Rendering/Particles.hpp"
#include "Panels/Widgets.hpp"
#include "Panels/Pickers.hpp"
#include "Kit/Foliage.hpp"
#include "Platform.hpp"

#include <algorithm>
#include <unordered_map>
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
        drawScenePart(editor);
    }

    /// What is chosen in the scene: the Inspector proper. Nothing of the Explorer is shown here.
    void drawScenePart(EditorContext& editor) {
        SceneDocument& scene = editor.scene;
        Entity entity = scene.primary();
        // Remembered, for when nothing is chosen.
        if (editor.engine.scene.isAlive(entity) && (recent_.empty() || recent_.front() != entity)) {
            recent_.erase(std::remove(recent_.begin(), recent_.end(), entity), recent_.end());
            recent_.insert(recent_.begin(), entity);
            if (recent_.size() > 5) recent_.pop_back();
        }
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
        }
        // With nothing chosen the Inspector is empty: it is for what is chosen.
    }

    std::vector<Entity> recent_;

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
            const GameSettings& settings = editor.engine.settings;
            std::vector<std::string> tags = settings.allTags();
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
                ImGui::SetNextItemWidth(170);
                if (ImGui::InputTextWithHint("##new-tag", "New tag, then Return", &newTag_, ImGuiInputTextFlags_EnterReturnsTrue) && !newTag_.empty()) {
                    current.value = newTag_;
                    // A tag made here is the project's from now on, like one added in Project Settings.
                    GameSettings added = settings;
                    if (std::find(added.tags.begin(), added.tags.end(), newTag_) == added.tags.end() && newTag_ != "Untagged") {
                        added.tags.push_back(newTag_);
                        saveGameSettings(editor, added);
                    }
                    newTag_.clear();
                    tagChanged = true;
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::Selectable("Edit Tags...")) editor.showProjectSettings("Tags and Layers");
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            std::string layerName = settings.layerName(current.layer);
            ImGui::SetNextItemWidth(half);
            if (ImGui::BeginCombo("##layer", ("Layer: " + layerName).c_str())) {
                for (int layer = 0; layer < GameSettings::kLayerCount; layer++) {
                    // The layers that have a name, and the one it is on even if it has none.
                    if (settings.layers[size_t(layer)].empty() && layer != current.layer) continue;
                    std::string label = settings.layerName(layer);
                    if (ImGui::Selectable((std::to_string(layer) + "  " + label).c_str(), layer == current.layer)) {
                        current.layer = layer;
                        tagChanged = true;
                        // A collider is on its entity's layer: what it meets is decided by layers.
                        if (Collider* collider = scene.get<Collider>(entity)) collider->layer = 1u << uint32_t(layer);
                    }
                }
                ImGui::Separator();
                if (ImGui::Selectable("Edit Layers...")) editor.showProjectSettings("Tags and Layers");
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

        // A piece of the interface lives on the screen, not in the world: no position, rotation
        // and scale in 3D for it, only its place on the screen (anchor, offset, size), below.
        bool isInterface = isInterfacePiece(scene, entity);
        if (isInterface) {
            ImGui::TextDisabled("On the screen: placed by its anchor and offset below, or dragged in the Game view.");
        }
        if (Transform* transform = isInterface ? nullptr : scene.get<Transform>(entity)) {
            if (ui::componentSection("Transform", "Transform", "axes")) {
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
            bool isOpen = ui::componentSection(component.name.c_str(), component.name.c_str(), ui::iconKindOf(component.name));
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
            if (ui::moreButton("##more")) ImGui::OpenPopup("##component-menu");
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
            bool isOpen = ui::componentSection(ComponentRegistry::displayName(type.name).c_str(), type.name.c_str(), ui::iconKindOf(type.name, type.category));
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
            if (ui::moreButton("##more")) ImGui::OpenPopup("##component-menu");
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
                    if (drawField(editor, entity, type, field, component)) document.markChanged();
                }
                if (type.name == "Foliage") {
                    // What foliage is: copies of this entity's mesh, painted on the ground.
                    const kit::Foliage* foliage = static_cast<const kit::Foliage*>(component);
                    ImGui::TextWrapped("%d copies of its mesh (grass, stones, trees), painted on whatever is under them. "
                                       "Paint them in the Scene view with the brush that shows there while this is chosen: Paint, Erase, Size, Density.",
                                       int(foliage->copies.size()));
                }
                if (type.name == "Spline") {
                    if (ImGui::Button("Add Point")) addSplinePoint(editor);
                    ui::tooltip("A point after the last (or after the one chosen), a little beyond it");
                    // What a spline can be made into: a road along it (its surface made from the curve).
                    if (const ComponentType* road = ComponentRegistry::instance().find("SplineRoad"); road && !road->get(scene, entity)) {
                        ImGui::SameLine();
                        if (ImGui::Button("Make It a Road")) {
                            road->add(scene, entity);
                            if (!scene.has<MeshRenderer>(entity)) scene.add(entity, MeshRenderer{});
                            document.markChanged();
                        }
                        ui::tooltip("A road along the curve: its width, banking and texture in the Road component it adds");
                    }
                }
                // Where an Animator's animations and states are made.
                if (type.name == "Animator") {
                    if (ImGui::Button("Open Timeline")) editor.openPanel("Timeline");
                    ui::tooltip("Its animations: create one, record and edit its keys");
                    ImGui::SameLine();
                    if (ImGui::Button("Open Animator")) editor.openPanel("Animator");
                    ui::tooltip("Its controller: states, transitions, parameters");
                }
            }
            ImGui::PopID();
        }
        // The material of its mesh, as in the mesh's own file: what is changed here changes
        // everything that uses the material.
        if (MeshRenderer* renderer = scene.get<MeshRenderer>(entity); renderer && renderer->material) {
            Material& material = *renderer->material;
            std::string title = "Material: " + materialDisplayName(editor, renderer->material);
            if (ui::componentSection(title.c_str(), "material", "material")) {
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
        if (ImGui::Button("Add Component", ImVec2(width, 0)) || editor.addComponentRequested) ImGui::OpenPopup("##add-component");
        editor.addComponentRequested = false;        // Component > Add... (Shift+Cmd+A)
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
                std::vector<std::string> categories = {"Mesh", "Rendering", "Physics", "Animation", "Audio", "Effects", "Navigation", "Paths", "Vehicles", "UI", "Network", "Scripts", "Miscellaneous"};
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
    static bool drawField(EditorContext& editor, Entity entity, const ComponentType& type, const Field& field, void* component) {
        if (field.isHidden) return false;
        std::string label = field.label();
        void* at = field.address(component);
        bool hasRange = field.minimum < field.maximum;
        bool changed = false;
        switch (field.type) {
            case FieldType::Float:
                changed = hasRange ? ui::sliderFloat(label.c_str(), *static_cast<float*>(at), field.minimum, field.maximum, ui::kNumberFormat)
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
                    // What it names, offered as a list: a file, an entity, a bone, a value... Words only for words.
                    std::string kind = field.pick;
                    if (!field.pickBy.empty()) {
                        if (const Field* by = type.findField(field.pickBy)) {
                            std::string decides = by->valueText(component);
                            for (const auto& [choice, picked] : field.pickFor) {
                                if (choice == decides) kind = picked;
                            }
                        }
                    }
                    if (kind == "none") break;              // nothing for it to name: not shown
                    changed = pickValue(editor, entity, kind, label.c_str(), value);
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

    Entity shownFor_;
    std::string newTag_;
    std::string addFilter_;                                             // what is typed in Add Component
    std::string copiedType_;                                            // Copy Values: of which type, and the values as text
    std::vector<std::pair<std::string, std::string>> copiedValues_;
    Vec3 shownAngles_{0, 0, 0};
    Quat shownRotation_{};
};

}  // namespace

/// A piece of the game's interface: something of the UI category, drawn on the screen.
bool isInterfacePiece(Scene& scene, Entity entity) {
    for (const ComponentType& type : ComponentRegistry::instance().types()) {
        // (Something of the world that only draws over the game with the interface, a weapon's sights, is not one: it has no place on the screen.)
        if ((type.category == "UI" || (type.isInterfacePiece && type.findField("anchor"))) && type.get && type.get(scene, entity)) return true;
    }
    return false;
}

std::vector<MenuEntry> addComponentEntries(EditorContext& editor) {
    Scene& scene = editor.engine.scene;
    std::vector<Entity> selected = editor.scene.selection();
    // Each by category, in the order people look for things in: what is seen, then how it behaves.
    struct Choice {
        std::string category, name;
        std::function<bool(Entity)> has;
        std::function<void(Entity)> add;
    };
    std::vector<Choice> choices;
    for (auto& item : editor.registry.components) {
        const std::string& name = item.value.name;
        const char* category = name == "Mesh Renderer" ? "Mesh" : name == "Camera" || name == "Light" ? "Rendering"
                             : name.find("Collider") != std::string::npos || name == "Rigidbody" ? "Physics" : name == "Particle System" ? "Effects" : "Miscellaneous";
        ComponentEditor* component = &item.value;
        choices.push_back({category, name, [component, &scene](Entity entity) { return component->has(scene, entity); },
                           [component, &scene](Entity entity) { component->add(scene, entity); }});
    }
    for (const ComponentType& type : ComponentRegistry::instance().types()) {
        if (type.category == "Hidden") continue;
        auto has = type.has;
        auto add = type.add;
        choices.push_back({type.category, ComponentRegistry::displayName(type.name), [has, &scene](Entity entity) { return has(scene, entity); },
                           [add, &scene](Entity entity) { add(scene, entity); }});
    }
    std::vector<std::string> categories = {"Mesh", "Rendering", "Physics", "Animation", "Audio", "Effects", "Navigation", "Paths", "Vehicles", "UI", "Network", "Scripts", "Miscellaneous"};
    for (const Choice& choice : choices) {
        if (std::find(categories.begin(), categories.end(), choice.category) == categories.end()) categories.push_back(choice.category);
    }
    std::vector<MenuEntry> entries;
    for (const std::string& category : categories) {
        MenuEntry submenu;
        submenu.label = category;
        for (const Choice& choice : choices) {
            if (choice.category != category) continue;
            // Offered while any of the selected lacks it.
            bool isMissing = false;
            for (Entity entity : selected) isMissing = isMissing || (scene.isAlive(entity) && !choice.has(entity));
            MenuEntry entry;
            entry.label = choice.name;
            entry.isEnabled = isMissing;
            auto has = choice.has;
            auto add = choice.add;
            entry.run = [&editor, has, add, selected] {
                for (Entity entity : selected) {
                    if (editor.engine.scene.isAlive(entity) && !has(entity)) add(entity);
                }
                editor.scene.markChanged();
            };
            submenu.children.push_back(std::move(entry));
        }
        if (!submenu.children.empty()) entries.push_back(std::move(submenu));
    }
    return entries;
}

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

namespace {

/// A module of a particle system, as in Unity: a row that folds, with a box that switches it on
/// when it is one that can be off. Returns whether it is open.
bool particleModule(const char* title, bool* isOn, bool& changed, bool startsOpen) {
    ImGui::PushID(title);
    ImGui::Spacing();
    if (isOn) {
        if (ImGui::Checkbox("##on", isOn)) changed = true;
        ImGui::SameLine(0, 6);
    }
    ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_FramePadding;
    if (startsOpen) flags |= ImGuiTreeNodeFlags_DefaultOpen;
    bool open = ImGui::TreeNodeEx("##module", flags, "%s", title);
    ImGui::PopStyleColor();
    ImGui::PopID();
    // Off: its settings are not shown, they do nothing.
    if (open && isOn && !*isOn) {
        ImGui::TreePop();
        return false;
    }
    return open;
}

/// What a module had when it was switched off, so that switching it on again brings it back.
ParticleEmitter& rememberedFor(const ParticleEmitter& emitter) {
    static std::unordered_map<const ParticleEmitter*, ParticleEmitter> kept;
    auto found = kept.find(&emitter);
    if (found == kept.end()) found = kept.emplace(&emitter, emitter).first;
    return found->second;
}

}  // namespace

bool drawEmitterProperties(EditorContext& editor, ParticleEmitter& emitter, bool) {
    bool changed = false;
    ParticleEmitter& remembered = rememberedFor(emitter);

    if (particleModule("Main", nullptr, changed, true)) {
        changed |= ui::checkbox("Playing", emitter.isEmitting);
        ui::tooltip("Emitting new particles. Code and visual scripts can switch it on and off");
        changed |= ui::dragFloat("Duration", emitter.duration, 0.01f, 0.0f, 600.0f, "%.2f s");
        ui::tooltip("It emits for this long and stops. 0: for as long as it is there");
        if (emitter.duration > 0) changed |= ui::checkbox("Looping", emitter.loops);
        changed |= ui::checkbox("Prewarm", emitter.prewarm);
        ui::tooltip("Starts as if it had been running already: a fire already burning when the scene begins");
        changed |= ui::dragFloat("Start Delay", emitter.startDelay, 0.01f, 0.0f, 600.0f, "%.2f s");
        changed |= ui::dragFloat("Lifetime, Shortest", emitter.lifetimeMin, 0.01f, 0.01f, 600.0f, "%.2f s");
        changed |= ui::dragFloat("Lifetime, Longest", emitter.lifetimeMax, 0.01f, 0.01f, 600.0f, "%.2f s");
        changed |= ui::dragFloat("Speed, Slowest", emitter.speedMin, 0.02f, 0.0f, 1000.0f, "%.2f m/s");
        changed |= ui::dragFloat("Speed, Fastest", emitter.speedMax, 0.02f, 0.0f, 1000.0f, "%.2f m/s");
        changed |= ui::sliderFloat("Size Variation", emitter.sizeVariation, 0.0f, 1.0f);
        changed |= ui::sliderFloat("Shade Variation", emitter.shadeVariation, 0.0f, 1.0f);
        ui::tooltip("Each particle a little darker or brighter than the next: smoke that is not one flat grey");
        changed |= ui::dragVec3("Gravity", emitter.gravity, 0.02f);
        int space = int(emitter.space);
        if (ui::combo("Simulation Space", space, {"World", "Local"})) {
            emitter.space = ParticleEmitter::Space(space);
            emitter.particles.clear();
            changed = true;
        }
        ui::tooltip("World: what it emitted stays where it was born as the entity moves on (smoke, a trail). Local: carried along with the entity (a halo)");
        changed |= ui::sliderFloat("Simulation Speed", emitter.simulationSpeed, 0.0f, 4.0f);
        changed |= ui::dragFloat("Inherit Velocity", emitter.inheritVelocity, 0.01f, -2.0f, 2.0f);
        ui::tooltip("The share of the entity's own movement each particle is born with: sparks off a moving grinder");
        changed |= ui::dragInt("Max Particles", emitter.maxParticles, 4.0f, 1, 100000);
        ImGui::TreePop();
    }
    if (particleModule("Emission", nullptr, changed, true)) {
        changed |= ui::dragFloat("Rate over Time", emitter.rate, 0.5f, 0.0f, 10000.0f, "%.0f a second");
        changed |= ui::dragFloat("Rate over Distance", emitter.ratePerMeter, 0.1f, 0.0f, 1000.0f, "%.1f a meter");
        ui::tooltip("Emitted for every meter the entity moves: a trail behind something that flies");
        changed |= ui::dragInt("Burst at Start", emitter.burstAtStart, 1.0f, 0, 100000);
        changed |= ui::dragFloat("Burst Every", emitter.burstEvery, 0.01f, 0.0f, 600.0f, "%.2f s");
        ui::tooltip("0: no bursts after the first");
        if (emitter.burstEvery > 0) changed |= ui::dragInt("Burst Count", emitter.burstCount, 1.0f, 0, 100000);
        ImGui::TreePop();
    }
    if (particleModule("Shape", nullptr, changed, true)) {
        int shape = int(emitter.shape);
        if (ui::combo("Shape", shape, {"Cone", "Box", "Ring", "Sphere", "Hemisphere", "Line"})) {
            emitter.shape = ParticleEmitter::Shape(shape);
            changed = true;
        }
        using Shape = ParticleEmitter::Shape;
        if (emitter.shape == Shape::Box) changed |= ui::dragVec3("Box Size", emitter.boxSize, 0.02f);
        else if (emitter.shape == Shape::Line) {
            float length = emitter.boxSize.x;
            if (ui::dragFloat("Length", length, 0.02f, 0.0f, 1000.0f, "%.2f m")) emitter.boxSize.x = length, changed = true;
        }
        else changed |= ui::dragFloat("Radius", emitter.spawnRadius, 0.01f, 0.0f, 1000.0f, "%.2f m");
        if (emitter.shape == Shape::Box || emitter.shape == Shape::Sphere || emitter.shape == Shape::Hemisphere || emitter.shape == Shape::Cone) {
            changed |= ui::checkbox("From the Surface", emitter.fromShell);
            ui::tooltip("Born on its surface instead of anywhere inside");
        }
        changed |= ui::dragVec3("Direction", emitter.direction, 0.01f);
        changed |= ui::dragDegrees("Spread", emitter.spread, 0.0f, 180.0f);
        ui::tooltip(emitter.shape == Shape::Sphere || emitter.shape == Shape::Hemisphere ? "How far from straight out they scatter"
                                                                                       : "How wide the cone they leave in is: 180 is every direction");
        ImGui::TreePop();
    }
    bool hasVelocity = simd::length_squared(emitter.drift) > 0 || emitter.radial != 0 || emitter.orbital != 0;
    bool velocityWas = hasVelocity;
    if (particleModule("Velocity over Lifetime", &hasVelocity, changed, false)) {
        changed |= ui::dragVec3("Linear", emitter.drift, 0.01f);
        ui::tooltip("Meters a second every particle moves along, besides its own speed, in the simulation's space");
        changed |= ui::dragFloat("Radial", emitter.radial, 0.01f, -100.0f, 100.0f, "%.2f m/s");
        ui::tooltip("Away from the emitter; below 0, toward it");
        changed |= ui::dragFloat("Orbital", emitter.orbital, 0.01f, -50.0f, 50.0f, "%.2f rad/s");
        ui::tooltip("Round the emitter's direction: a whirl, a vortex");
        ImGui::TreePop();
    }
    if (hasVelocity != velocityWas) {
        if (!hasVelocity) {
            remembered.drift = emitter.drift, remembered.radial = emitter.radial, remembered.orbital = emitter.orbital;
            emitter.drift = Vec3{0, 0, 0}, emitter.radial = 0, emitter.orbital = 0;
        } else {
            bool hadSome = simd::length_squared(remembered.drift) > 0 || remembered.radial != 0 || remembered.orbital != 0;
            emitter.drift = hadSome ? remembered.drift : Vec3{0, 0.5f, 0};
            emitter.radial = remembered.radial, emitter.orbital = remembered.orbital;
        }
    }
    if (particleModule("Forces", nullptr, changed, false)) {
        changed |= ui::dragFloat("Buoyancy", emitter.buoyancy, 0.02f, -50.0f, 50.0f, "%.2f m/s²");
        ui::tooltip("Hot gas rises: a push upward at birth that fades as the particle cools. Flames and smoke rise by this, not by gravity");
        changed |= ui::dragFloat("Drag", emitter.drag, 0.01f, 0.0f, 20.0f);
        ui::tooltip("How quickly the air slows them: light things (smoke) a lot, heavy ones (sparks) little");
        changed |= ui::sliderFloat("Wind", emitter.windInfluence, 0.0f, 1.0f);
        ui::tooltip("How much the scene's wind (Environment > Weather > Wind) carries them: 1 for smoke that goes wherever the air goes");
        ImGui::TreePop();
    }
    bool hasNoise = emitter.turbulence > 0;
    bool noiseWas = hasNoise;
    if (particleModule("Noise", &hasNoise, changed, false)) {
        changed |= ui::dragFloat("Strength", emitter.turbulence, 0.01f, 0.0f, 20.0f, "%.2f m/s");
        changed |= ui::dragFloat("Size", emitter.turbulenceSize, 0.01f, 0.05f, 100.0f, "%.2f m");
        ui::tooltip("How large the swirls are");
        changed |= ui::sliderFloat("Scroll Speed", emitter.turbulenceSpeed, 0.0f, 5.0f);
        ui::tooltip("How fast the swirls themselves change");
        changed |= ui::dragInt("Octaves", emitter.turbulenceOctaves, 0.05f, 1, 3);
        ui::tooltip("Swirls inside swirls: 2 or 3 for smoke that curls like real smoke");
        ImGui::TreePop();
    }
    if (hasNoise != noiseWas) {
        if (!hasNoise) remembered.turbulence = emitter.turbulence, emitter.turbulence = 0;
        else emitter.turbulence = remembered.turbulence > 0 ? remembered.turbulence : 0.5f;
    }
    bool hasCollision = emitter.collides || emitter.bounces;
    bool collisionWas = hasCollision;
    if (particleModule("Collision", &hasCollision, changed, false)) {
        changed |= ui::checkbox("With Colliders", emitter.collides);
        ui::tooltip("The boxes, spheres and capsules of the scene: sparks off a wall, smoke under a ceiling");
        changed |= ui::checkbox("With a Floor", emitter.bounces);
        if (emitter.bounces) changed |= ui::dragFloat("Floor Height", emitter.floorHeight, 0.01f, -10000.0f, 10000.0f, "%.2f m");
        changed |= ui::sliderFloat("Bounce", emitter.bounciness, 0.0f, 1.0f);
        changed |= ui::sliderFloat("Friction", emitter.friction, 0.0f, 1.0f);
        changed |= ui::sliderFloat("Lifetime Loss", emitter.lifetimeLoss, 0.0f, 1.0f);
        ui::tooltip("The share of its life a particle loses each time it hits");
        changed |= ui::checkbox("Dies on Hit", emitter.diesOnHit);
        if (emitter.space == ParticleEmitter::Space::Local) ImGui::TextDisabled("Collisions are for the World space.");
        ImGui::TreePop();
    }
    if (hasCollision != collisionWas) {
        if (!hasCollision) emitter.collides = emitter.bounces = false;
        else emitter.collides = true;
    }
    if (particleModule("Color over Lifetime", nullptr, changed, true)) {
        changed |= ui::color("At Birth", emitter.colorStart);
        changed |= ui::checkbox("A Middle Color", emitter.hasMiddle);
        ui::tooltip("A third color and size on the way: yellow, then orange, then smoke");
        if (emitter.hasMiddle) {
            changed |= ui::sliderFloat("Middle At", emitter.middleAt, 0.02f, 0.98f);
            changed |= ui::color("At the Middle", emitter.colorMiddle);
        }
        changed |= ui::color("At Death", emitter.colorEnd);
        ui::tooltip("Values above 1 glow: the bloom picks them up");
        ImGui::TreePop();
    }
    if (particleModule("Size over Lifetime", nullptr, changed, true)) {
        changed |= ui::dragFloat("At Birth", emitter.sizeStart, 0.005f, 0.0f, 1000.0f, "%.3f m");
        if (emitter.hasMiddle) changed |= ui::dragFloat("At the Middle", emitter.sizeMiddle, 0.005f, 0.0f, 1000.0f, "%.3f m");
        changed |= ui::dragFloat("At Death", emitter.sizeEnd, 0.005f, 0.0f, 1000.0f, "%.3f m");
        ImGui::TreePop();
    }
    if (particleModule("Rotation", nullptr, changed, false)) {
        changed |= ui::checkbox("Random Start", emitter.randomRotation);
        changed |= ui::dragFloat("Spin, Least", emitter.spinMin, 0.01f, 0.0f, 30.0f, "%.2f rad/s");
        changed |= ui::dragFloat("Spin, Most", emitter.spinMax, 0.01f, 0.0f, 30.0f, "%.2f rad/s");
        ui::tooltip("Each turns either way at a speed between these: smoke that rolls");
        ImGui::TreePop();
    }
    bool hasSheet = emitter.sheetColumns * emitter.sheetRows > 1;
    bool sheetWas = hasSheet;
    if (particleModule("Texture Sheet Animation", &hasSheet, changed, false)) {
        int sheet[2] = {emitter.sheetColumns, emitter.sheetRows};
        ui::label("Tiles");
        if (ImGui::DragInt2("##sheet", sheet, 0.05f, 1, 16)) {
            emitter.sheetColumns = std::max(sheet[0], 1);
            emitter.sheetRows = std::max(sheet[1], 1);
            changed = true;
        }
        ui::tooltip("The picture is a sheet of frames, columns by rows");
        changed |= ui::dragFloat("Frames a Second", emitter.sheetFps, 0.1f, 0.0f, 120.0f, "%.1f");
        ui::tooltip("0: the whole sheet once over each particle's life");
        changed |= ui::checkbox("Random Start Frame", emitter.randomFrame);
        if (emitter.texture.empty()) ImGui::TextDisabled("Choose a picture in the Renderer.");
        ImGui::TreePop();
    }
    if (hasSheet != sheetWas) {
        if (!hasSheet) emitter.sheetColumns = emitter.sheetRows = 1;
        else emitter.sheetColumns = 2, emitter.sheetRows = 2;
    }
    if (particleModule("Renderer", nullptr, changed, true)) {
        int mode = int(emitter.renderMode);
        if (ui::combo("Render Mode", mode, {"Billboard", "Stretched", "Horizontal", "Vertical"})) {
            emitter.renderMode = ParticleEmitter::RenderMode(mode);
            if (emitter.renderMode == ParticleEmitter::RenderMode::Stretched && emitter.stretch <= 0) emitter.stretch = 0.03f;
            changed = true;
        }
        if (emitter.renderMode == ParticleEmitter::RenderMode::Stretched) {
            changed |= ui::dragFloat("Length", emitter.stretch, 0.001f, 0.001f, 1.0f, "%.3f s of its way");
        }
        changed |= pickValue(editor, Entity{}, "picture", "Picture", emitter.texture);
        ui::tooltip("A picture with clear (transparent) around its shape, tinted by the particle's color. None: a soft round dot");
        int blend = emitter.isAdditive ? 0 : 1;
        if (ui::combo("Blending", blend, {"Additive (light)", "Alpha (matter)"})) {
            emitter.isAdditive = blend == 0;
            changed = true;
        }
        ui::tooltip("Additive adds its light to what is behind (fire, sparks, magic); Alpha covers it (smoke, dust, leaves)");
        changed |= ui::sliderFloat("Lit", emitter.lit, 0.0f, 1.0f);
        ui::tooltip("0: its own color, glowing. 1: lit by the sun and the sky, and brighter with the sun behind it, like smoke");
        changed |= ui::dragFloat("Soft Particles", emitter.softness, 0.01f, 0.0f, 10.0f, "%.2f m");
        ui::tooltip("Fades out where it meets a surface instead of cutting a hard line into it");
        ImGui::TreePop();
    }
    emitter.lifetimeMax = std::max(emitter.lifetimeMax, emitter.lifetimeMin);
    emitter.speedMax = std::max(emitter.speedMax, emitter.speedMin);
    emitter.spinMax = std::max(emitter.spinMax, emitter.spinMin);
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
        if (ui::combo("Type", type, {"Directional", "Point", "Spot", "Area"})) {
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
        if (light.type == LightType::Area) {
            changed |= ui::dragVec2("Size", light.areaSize, 0.02f);
            ui::tooltip("The glowing rectangle, in meters: a window, a panel of light, a screen. It shines from its front (the way the entity faces)");
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
            fitColliderToMesh(collider, renderer->mesh->bounds(), transform->scale);
            changed = true;
        }
        if (renderer && renderer->mesh && transform) {
            ui::tooltip("The size of the model it draws, at the entity's scale. A collider that fits goes on fitting while the entity is scaled; "
                        "one sized by hand is left alone.");
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
            if (renderer && renderer->mesh && transform && !scene.get<Collider>(entity)) fitColliderToMesh(collider, renderer->mesh->bounds(), transform->scale);
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
