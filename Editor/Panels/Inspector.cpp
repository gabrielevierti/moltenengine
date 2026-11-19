// The inspector: the components of the selected entity, or the facts of the selected asset.
#include "Editor.hpp"
#include "Panels/Widgets.hpp"
#include "Platform.hpp"

#include <algorithm>
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
            drawEntity(editor, entity);
        } else if (!editor.selectedAsset.empty() && editor.assets.find(editor.selectedAsset)) {
            drawAsset(editor, *editor.assets.find(editor.selectedAsset));
        } else {
            ImGui::TextDisabled("Nothing selected.");
            ImGui::TextDisabled("Click something in the view, the hierarchy or the assets.");
        }
    }

private:
    void drawEntity(EditorContext& editor, Entity entity) {
        SceneDocument& document = editor.scene;
        Scene& scene = editor.engine.scene;

        std::string name = document.nameOf(entity);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("##name", &name) && !name.empty()) {
            scene.add(entity, Name{name});
            document.markChanged();
        }
        if (document.selection().size() > 1) {
            ImGui::TextDisabled("%zu entities selected. Changes here apply to this one.", document.selection().size());
        }
        ImGui::Spacing();

        if (Transform* transform = scene.get<Transform>(entity)) {
            if (ui::section("Transform")) {
                bool changed = ui::dragVec3("Position", transform->position, 0.02f);
                // Angles are shown in degrees and remembered while the rotation stays the same:
                // converting the rotation back to angles every frame would make the numbers jump.
                if (shownFor_ != entity || simd::dot(shownRotation_.vector, transform->rotation.vector) < 0.999999f) {
                    shownAngles_ = toEulerAngles(transform->rotation) * (180.0f / kPi);
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
            // A small button at the right end of the header: what can be done to the component.
            ImGui::SameLine(rightEdge - 30);
            if (ImGui::SmallButton("...")) ImGui::OpenPopup("##component-menu");
            bool removed = false;
            if (ImGui::BeginPopup("##component-menu")) {
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
            bool isOpen = ui::section(type.name.c_str());
            ImGui::SameLine(rightEdge - 30);
            if (ImGui::SmallButton("...")) ImGui::OpenPopup("##component-menu");
            bool removed = false;
            if (ImGui::BeginPopup("##component-menu")) {
                if (ImGui::MenuItem("Remove Component")) removed = true;
                ImGui::EndPopup();
            }
            if (removed) {
                type.remove(scene, entity);
                document.markChanged();
            } else if (isOpen) {
                if (type.fields.empty()) ImGui::TextDisabled("No fields.");
                for (const Field& field : type.fields) {
                    if (drawField(field, component)) document.markChanged();
                }
            }
            ImGui::PopID();
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
        if (ImGui::BeginPopup("##add-component")) {
            bool any = false;
            for (auto& item : editor.registry.components) {
                if (item.value.has(scene, entity)) continue;
                any = true;
                if (ImGui::MenuItem(item.value.name.c_str())) {
                    item.value.add(scene, entity);
                    document.markChanged();
                }
            }
            bool hasGameTypes = false;
            for (size_t i = 0; i < ComponentRegistry::instance().types().size(); i++) {
                const ComponentType& type = ComponentRegistry::instance().types()[i];
                if (type.has(scene, entity)) continue;
                if (!hasGameTypes && any) ImGui::Separator();
                hasGameTypes = true;
                any = true;
                if (ImGui::MenuItem(type.name.c_str())) {
                    type.add(scene, entity);
                    document.markChanged();
                }
            }
            if (!any) ImGui::TextDisabled("It has them all.");
            ImGui::EndPopup();
        }
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
            case FieldType::String: changed = ui::text(label.c_str(), *static_cast<std::string*>(at)); break;
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
            bool canLoad = std::filesystem::path(asset.path).extension() == ".obj";
            if (!canLoad) ImGui::TextWrapped("This format cannot be imported yet: only Wavefront OBJ. glTF is next on the roadmap.");
            ImGui::BeginDisabled(!canLoad);
            if (ImGui::Button("Add to Scene")) {
                Entity created = editor.scene.createFromMesh(asset.path, Vec3{0, 0, 0});
                if (!created.isValid()) editor.notify("Cannot load " + asset.path, ConsoleMessage::Level::Error);
            }
            ImGui::EndDisabled();
        } else if (asset.type == AssetType::Scene) {
            if (ImGui::Button("Open Scene")) editor.requestOpenScene(editor.assets.absolutePath(asset.path));
        }

        ImGui::Spacing();
        if (ImGui::Button("Reveal in Finder")) platform::revealInFinder(editor.assets.absolutePath(asset.path));
        if (!asset.isFolder()) {
            ImGui::SameLine();
            if (ImGui::Button("Open")) platform::openWithDefaultApplication(editor.assets.absolutePath(asset.path));
        }
    }

    Entity shownFor_;
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
        if (asset.type == AssetType::Mesh && std::filesystem::path(asset.path).extension() == ".obj") names.push_back(asset.path);
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

void registerBuiltinComponents(EditorContext& editor) {
    Registry& registry = editor.registry;

    registry.addComponent<MeshRenderer>(nullptr, "Mesh Renderer", [](EditorContext& context, Entity, MeshRenderer& renderer) {
        bool changed = false;
        std::vector<std::string> meshes = meshChoices(context);
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
        std::vector<std::string> materialNames;
        for (const NamedMaterial& material : context.scene.materials) materialNames.push_back(material.name);
        int materialIndex = context.scene.materialIndex(renderer.material);
        if (ui::combo("Material", materialIndex, materialNames)) {
            renderer.material = context.scene.materials[size_t(materialIndex)].material;
            changed = true;
        }
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

    registry.addComponent<Collider>(nullptr, "Collider", [](EditorContext& context, Entity entity, Collider& collider) {
        bool changed = false;
        int shape = int(collider.shape);
        if (ui::combo("Shape", shape, {"Sphere", "Box", "Capsule"})) {
            collider.shape = ColliderShape(shape);
            changed = true;
        }
        if (collider.shape == ColliderShape::Box) {
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
    });

    registry.addComponent<RigidBody>(nullptr, "Rigid Body", [](EditorContext&, Entity, RigidBody& body) {
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
        return changed;
    });

    registry.addComponent<ParticleEmitter>(nullptr, "Particle Emitter", [](EditorContext&, Entity, ParticleEmitter& emitter) {
        bool changed = ui::checkbox("Emitting", emitter.isEmitting);
        changed |= ui::dragFloat("Rate", emitter.rate, 0.5f, 0.0f, 5000.0f, "%.0f per second");
        changed |= ui::dragInt("Most at Once", emitter.maxParticles, 4.0f, 1, 100000);
        changed |= ui::dragFloat("Life, Shortest", emitter.lifetimeMin, 0.01f, 0.01f, 60.0f);
        changed |= ui::dragFloat("Life, Longest", emitter.lifetimeMax, 0.01f, 0.01f, 60.0f);
        changed |= ui::dragFloat("Speed, Slowest", emitter.speedMin, 0.02f, 0.0f, 1000.0f);
        changed |= ui::dragFloat("Speed, Fastest", emitter.speedMax, 0.02f, 0.0f, 1000.0f);
        changed |= ui::dragVec3("Direction", emitter.direction, 0.01f);
        changed |= ui::dragDegrees("Spread", emitter.spread, 0.0f, 180.0f);
        changed |= ui::dragFloat("Spawn Radius", emitter.spawnRadius, 0.01f, 0.0f, 1000.0f);
        changed |= ui::dragVec3("Gravity", emitter.gravity, 0.02f);
        changed |= ui::dragFloat("Drag", emitter.drag, 0.01f, 0.0f, 20.0f);
        changed |= ui::dragFloat("Size at Birth", emitter.sizeStart, 0.005f, 0.0f, 100.0f);
        changed |= ui::dragFloat("Size at Death", emitter.sizeEnd, 0.005f, 0.0f, 100.0f);
        changed |= ui::color("Color at Birth", emitter.colorStart);
        changed |= ui::color("Color at Death", emitter.colorEnd);
        changed |= ui::checkbox("Additive", emitter.isAdditive);
        emitter.lifetimeMax = std::max(emitter.lifetimeMax, emitter.lifetimeMin);
        emitter.speedMax = std::max(emitter.speedMax, emitter.speedMin);
        return changed;
    });
}

}  // namespace molten::editor
