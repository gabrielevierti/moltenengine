// Window > Scene Weight: what the scene's objects cost to draw and to keep in memory, the
// heaviest first. A click on a row selects the object.
#include "Editor.hpp"
#include "SceneWeight.hpp"
#include "Panels/Widgets.hpp"
#include "Rendering/Material.hpp"
#include "Rendering/Mesh.hpp"
#include "Rendering/Texture.hpp"

#include <cstdio>

namespace molten::editor {

namespace {

size_t textureBytes(const Texture& texture) {
    MTL::Texture* handle = texture.handle();
    if (!handle) return 0;
    if (size_t allocated = handle->allocatedSize(); allocated > 0) return allocated;
    int bytesPerPixel = handle->pixelFormat() == MTL::PixelFormatR8Unorm ? 1 : 4;
    return pictureBytes(texture.width(), texture.height(), bytesPerPixel, handle->mipmapLevelCount() > 1);
}

class SceneWeightPanel final : public Panel {
public:
    SceneWeightPanel() {
        title = "Scene Weight";
        isOpen = false;                 // Window > Scene Weight
    }

    void draw(EditorContext& editor) override {
        Scene& scene = editor.engine.scene;
        SceneDocument& document = editor.scene;
        // Weighed again twice a second: enough to follow edits, light on a big scene.
        double now = ImGui::GetTime();
        if (now - weighedAt_ > 0.5 || orderChanged_) {
            weighedAt_ = now;
            orderChanged_ = false;
            std::vector<WeightPart> parts;
            for (Entity entity : document.entities()) {
                const Transform* transform = scene.get<Transform>(entity);
                const MeshRenderer* renderer = scene.get<MeshRenderer>(entity);
                if (!renderer || !renderer->mesh || (onlyShown_ && (!renderer->isVisible || (transform && !transform->isActiveInHierarchy)))) continue;
                WeightPart part;
                part.entity = entity;
                part.name = document.nameOf(entity);
                part.mesh = renderer->mesh.get();
                part.material = renderer->material.get();
                const Mesh& mesh = *renderer->mesh;
                part.triangles = mesh.indices().size() / 3;
                part.meshBytes = mesh.positions().size() * MeshData::kFloatsPerVertex * sizeof(float) + mesh.indices().size() * sizeof(uint32_t);
                if (const MeshInstances* copies = scene.get<MeshInstances>(entity)) part.copies = copies->places.size();
                if (const Material* material = renderer->material.get()) {
                    for (const std::shared_ptr<Texture>* texture : {&material->baseColorTexture, &material->normalTexture, &material->metallicRoughnessTexture,
                                                                    &material->occlusionTexture, &material->emissiveTexture}) {
                        if (*texture) part.textures.push_back({texture->get(), textureBytes(**texture)});
                    }
                }
                parts.push_back(std::move(part));
            }
            report_ = weighScene(parts, order_);
        }

        ImGui::Text("%zu triangles   %d draws   models %s (%d)   pictures %s (%d)", report_.triangles, report_.draws,
                    bytesText(report_.meshBytes).c_str(), report_.meshes, bytesText(report_.textureBytes).c_str(), report_.textures);
        ui::tooltip("Draws: one for each model and material, however many copies (they are drawn together). "
                    "Models and pictures that objects share are counted once.");
        ImGui::SameLine(0, 16);
        if (ImGui::Checkbox("Only what is shown", &onlyShown_)) orderChanged_ = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130);
        int order = int(order_);
        if (ImGui::Combo("##order", &order, "Most triangles\0Most memory\0By name\0")) {
            order_ = WeightOrder(order);
            orderChanged_ = true;
        }

        if (!ImGui::BeginTable("##weights", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) return;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Triangles", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("Share", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Model", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Pictures", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableHeadersRow();
        size_t total = std::max<size_t>(report_.triangles, 1);
        ImGuiListClipper clipper;
        clipper.Begin(int(report_.rows.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                const EntityWeight& row = report_.rows[size_t(i)];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(i);
                bool isSelected = false;
                for (Entity entity : document.selection()) isSelected = isSelected || entity == row.entity;
                if (ImGui::Selectable(row.name.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) document.select(row.entity);
                if (ImGui::IsItemHovered() && (row.sharedWith > 0 || row.drawnWith > 0)) {
                    ImGui::SetTooltip("Its model is drawn by %d other%s too; %d drawn in the same call", row.sharedWith, row.sharedWith == 1 ? "" : "s",
                                      row.drawnWith);
                }
                ImGui::PopID();
                ImGui::TableNextColumn();
                ImGui::Text("%zu", row.triangles);
                ImGui::TableNextColumn();
                ImGui::Text("%.1f%%", 100.0 * double(row.triangles) / double(total));
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(bytesText(row.meshBytes).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(row.textureBytes ? bytesText(row.textureBytes).c_str() : "-");
            }
        }
        ImGui::EndTable();
    }

private:
    WeightReport report_;
    WeightOrder order_ = WeightOrder::Triangles;
    double weighedAt_ = -1;
    bool orderChanged_ = false;
    bool onlyShown_ = true;
};

}  // namespace

void registerSceneWeightPanel(EditorContext&) {
    // A page of the Profiler now (drawSceneWeightPage), not a window of its own.
}

void drawSceneWeightPage(EditorContext& editor) {
    static SceneWeightPanel page;
    page.draw(editor);
}

}  // namespace molten::editor
