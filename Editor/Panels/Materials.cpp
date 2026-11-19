// The materials of the open scene: a list on top, the chosen one's properties below.
#include "Editor.hpp"
#include "Panels/Widgets.hpp"

#include <algorithm>

namespace molten::editor {

namespace {

class MaterialsPanel final : public Panel {
public:
    MaterialsPanel() { title = "Materials"; }

    void draw(EditorContext& editor) override {
        SceneDocument& document = editor.scene;
        std::vector<NamedMaterial>& materials = document.materials;
        document.defaultMaterial();                 // there is always at least one
        selected_ = std::clamp(selected_, 0, int(materials.size()) - 1);

        // Selecting an entity shows its material: that is nearly always the one to edit.
        Entity primary = document.primary();
        if (primary != followed_) {
            followed_ = primary;
            if (const MeshRenderer* renderer = editor.engine.scene.get<MeshRenderer>(primary)) {
                int index = document.materialIndex(renderer->material);
                if (index >= 0) selected_ = index;
            }
        }

        if (ImGui::Button("+ New")) {
            materials.push_back(NamedMaterial{uniqueName(materials, "Material"), std::make_shared<Material>()});
            selected_ = int(materials.size()) - 1;
            document.markChanged();
        }
        ImGui::SameLine();
        if (ImGui::Button("Duplicate")) {
            NamedMaterial copy{uniqueName(materials, materials[size_t(selected_)].name), std::make_shared<Material>(*materials[size_t(selected_)].material)};
            materials.push_back(copy);
            selected_ = int(materials.size()) - 1;
            document.markChanged();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(materials.size() <= 1);
        if (ImGui::Button("Delete")) {
            // What used it falls back to the first of those that remain.
            std::shared_ptr<Material> gone = materials[size_t(selected_)].material;
            materials.erase(materials.begin() + selected_);
            std::shared_ptr<Material> fallback = materials[0].material;
            editor.engine.scene.each<MeshRenderer>([&](Entity, MeshRenderer& renderer) {
                if (renderer.material == gone) renderer.material = fallback;
            });
            selected_ = std::clamp(selected_, 0, int(materials.size()) - 1);
            document.markChanged();
        }
        ImGui::EndDisabled();

        float listHeight = std::min(float(materials.size()), 6.0f) * ImGui::GetTextLineHeightWithSpacing() + 10;
        ImGui::BeginChild("##list", ImVec2(0, listHeight), ImGuiChildFlags_Borders);
        for (int i = 0; i < int(materials.size()); i++) {
            ImGui::PushID(i);
            const Material& material = *materials[size_t(i)].material;
            ImVec4 swatch(saturate(material.baseColor.x), saturate(material.baseColor.y), saturate(material.baseColor.z), 1);
            ImGui::ColorButton("##swatch", swatch, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                               ImVec2(ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight()));
            ImGui::SameLine();
            if (ImGui::Selectable(materials[size_t(i)].name.c_str(), i == selected_)) selected_ = i;
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::Spacing();

        NamedMaterial& named = materials[size_t(selected_)];
        Material& material = *named.material;
        bool changed = false;
        std::string name = named.name;
        if (ui::text("Name", name) && !name.empty()) {
            named.name = name;
            changed = true;
        }
        bool isUsedBySelection = false;
        for (Entity entity : document.selection()) {
            const MeshRenderer* renderer = editor.engine.scene.get<MeshRenderer>(entity);
            isUsedBySelection = isUsedBySelection || (renderer && renderer->material == named.material);
        }
        ImGui::BeginDisabled(document.selection().empty() || isUsedBySelection);
        if (ImGui::Button("Assign to Selection")) {
            for (Entity entity : document.selection()) {
                if (MeshRenderer* renderer = editor.engine.scene.get<MeshRenderer>(entity)) renderer->material = named.material;
            }
            changed = true;
        }
        ImGui::EndDisabled();

        if (ui::section("Surface")) {
            changed |= ui::color("Base Color", material.baseColor);
            changed |= ui::sliderFloat("Metallic", material.metallic, 0.0f, 1.0f);
            changed |= ui::sliderFloat("Roughness", material.roughness, 0.0f, 1.0f);
            changed |= ui::color("Emissive", material.emissive, true);
            changed |= ui::dragVec2("Texture Repeat", material.uvScale, 0.01f);
        }
        if (ui::section("Textures")) {
            changed |= textureSlot(editor, "Base Color", material.baseColorTexture, TextureFormat::RGBA8_sRGB);
            changed |= textureSlot(editor, "Normal", material.normalTexture, TextureFormat::RGBA8);
            if (material.normalTexture) changed |= ui::sliderFloat("Normal Strength", material.normalScale, 0.0f, 2.0f);
            changed |= textureSlot(editor, "Metal & Rough", material.metallicRoughnessTexture, TextureFormat::RGBA8);
            changed |= textureSlot(editor, "Occlusion", material.occlusionTexture, TextureFormat::RGBA8);
            if (material.occlusionTexture) changed |= ui::sliderFloat("Occlusion Strength", material.occlusionStrength, 0.0f, 1.0f);
            changed |= textureSlot(editor, "Emissive", material.emissiveTexture, TextureFormat::RGBA8_sRGB);
        }
        if (ui::section("Drawing")) {
            int mode = int(material.alphaMode);
            if (ui::combo("Transparency", mode, {"Opaque", "Cut Out", "Blended"})) {
                material.alphaMode = AlphaMode(mode);
                changed = true;
            }
            if (material.alphaMode == AlphaMode::Mask) changed |= ui::sliderFloat("Cut Below", material.alphaCutoff, 0.0f, 1.0f);
            changed |= ui::checkbox("Two Sided", material.isDoubleSided);
            changed |= ui::checkbox("Unlit", material.isUnlit);
        }
        if (changed) document.markChanged();
    }

private:
    static std::string uniqueName(const std::vector<NamedMaterial>& materials, const std::string& base) {
        for (int number = 1;; number++) {
            std::string candidate = number == 1 ? base : base + " " + std::to_string(number);
            bool isTaken = false;
            for (const NamedMaterial& material : materials) isTaken = isTaken || material.name == candidate;
            if (!isTaken) return candidate;
        }
    }

    int selected_ = 0;
    Entity followed_;
};

}  // namespace

void registerMaterialsPanel(EditorContext& editor) {
    editor.registry.addPanel(nullptr, std::make_unique<MaterialsPanel>());
}

}  // namespace molten::editor
