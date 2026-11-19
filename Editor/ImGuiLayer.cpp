#include <imgui.h>

#include "ImGuiLayer.hpp"

#include <ImGuizmo.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_metal.h>

#include <cmath>
#include <filesystem>

namespace molten::editor {

namespace {

/// The window's pixels are sRGB-encoded by the GPU on the way out, but ImGui's colors
/// are already sRGB numbers: left alone, the whole interface would come out pale.
/// Decoding every vertex color first makes the two cancel.
void convertColorsToLinear(ImDrawData* drawData) {
    static uint8_t table[256];
    static bool isReady = false;
    if (!isReady) {
        for (int i = 0; i < 256; i++) {
            float c = float(i) / 255.0f;
            float linear = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
            table[i] = uint8_t(linear * 255.0f + 0.5f);
        }
        isReady = true;
    }
    for (ImDrawList* list : drawData->CmdLists) {
        for (ImDrawVert& vertex : list->VtxBuffer) {
            ImU32 color = vertex.col;
            ImU32 r = table[(color >> IM_COL32_R_SHIFT) & 0xFF];
            ImU32 g = table[(color >> IM_COL32_G_SHIFT) & 0xFF];
            ImU32 b = table[(color >> IM_COL32_B_SHIFT) & 0xFF];
            ImU32 a = (color >> IM_COL32_A_SHIFT) & 0xFF;
            vertex.col = (r << IM_COL32_R_SHIFT) | (g << IM_COL32_G_SHIFT) | (b << IM_COL32_B_SHIFT) | (a << IM_COL32_A_SHIFT);
        }
    }
}

}  // namespace

bool ImGuiLayer::start(Engine& engine, const std::string& settingsFile) {
    Window* window = engine.window();
    if (!window) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.ConfigDragClickToInputText = true;
    settingsFile_ = settingsFile;
    io.IniFilename = settingsFile_.c_str();         // ImGui keeps the pointer: the string lives in this object

    // The system font where there is one; ImGui's built-in font otherwise.
    for (const char* path : {"/System/Library/Fonts/SFNS.ttf", "/System/Library/Fonts/Helvetica.ttc"}) {
        std::error_code ignored;
        if (std::filesystem::exists(path, ignored) && io.Fonts->AddFontFromFileTTF(path, 14.0f)) break;
    }
    applyTheme(1.0f);

    MTL::Device* device = engine.renderer().device().device();
    // true: ImGui adds its own listeners in front of the engine's and passes every event on.
    ImGui_ImplGlfw_InitForOther(window->nativeHandle(), true);
    ImGui_ImplMetal_Init(device);

    formatTexture_ = engine.renderer().device().makeRenderTarget(Window::kPixelFormat, 1, 1);
    formatPass_ = NS::TransferPtr(MTL::RenderPassDescriptor::alloc()->init());
    formatPass_->colorAttachments()->object(0)->setTexture(formatTexture_.get());

    engine.renderer().overlay = [this](MTL::CommandBuffer* commandBuffer, MTL::RenderCommandEncoder* encoder) {
        draw(commandBuffer, encoder);
    };
    isActive_ = true;
    return true;
}

void ImGuiLayer::shutdown(Engine& engine) {
    if (!isActive_) return;
    engine.renderer().waitUntilIdle();
    engine.renderer().overlay = nullptr;
    ImGui_ImplMetal_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    isActive_ = false;
}

void ImGuiLayer::beginFrame() {
    if (!isActive_) return;
    ImGui_ImplMetal_NewFrame(formatPass_.get());
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
    hasFrame_ = false;
}

void ImGuiLayer::endFrame() {
    if (!isActive_) return;
    ImGui::Render();
    hasFrame_ = true;
}

void ImGuiLayer::draw(MTL::CommandBuffer* commandBuffer, MTL::RenderCommandEncoder* encoder) {
    if (!isActive_ || !hasFrame_) return;
    hasFrame_ = false;                  // each frame's lists are converted and drawn once
    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData) return;
    convertColorsToLinear(drawData);
    encoder->pushDebugGroup(NS::String::string("Editor interface", NS::UTF8StringEncoding));
    ImGui_ImplMetal_RenderDrawData(drawData, commandBuffer, encoder);
    encoder->popDebugGroup();
}

ImTextureID ImGuiLayer::textureId(const Texture* texture) {
    return texture ? ImTextureID(reinterpret_cast<uintptr_t>(texture->handle())) : ImTextureID(0);
}

// ---------------------------------------------------------------- the theme

void applyTheme(float scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    style.WindowRounding = 6;
    style.ChildRounding = 5;
    style.FrameRounding = 4;
    style.PopupRounding = 6;
    style.GrabRounding = 4;
    style.TabRounding = 5;
    style.ScrollbarRounding = 8;
    style.WindowPadding = ImVec2(10, 10);
    style.FramePadding = ImVec2(8, 5);
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.IndentSpacing = 16;
    style.ScrollbarSize = 12;
    style.GrabMinSize = 10;
    style.WindowBorderSize = 1;
    style.FrameBorderSize = 0;
    style.PopupBorderSize = 1;
    style.TabBarBorderSize = 1;
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.SeparatorTextBorderSize = 1;
    style.DockingSeparatorSize = 2;

    auto rgb = [](unsigned hex, float alpha = 1.0f) {
        return ImVec4(float((hex >> 16) & 0xFF) / 255.0f, float((hex >> 8) & 0xFF) / 255.0f, float(hex & 0xFF) / 255.0f, alpha);
    };
    const ImVec4 background = rgb(0x17191d), panel = rgb(0x1e2126), raised = rgb(0x282c33), hover = rgb(0x333842);
    const ImVec4 active = rgb(0x3d434f), border = rgb(0x0e0f12), text = rgb(0xdfe3ea), dim = rgb(0x8b93a1);
    const ImVec4 accent = rgb(0xff7a2f);            // the color of hot iron: selection and focus only

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = text;
    colors[ImGuiCol_TextDisabled] = dim;
    colors[ImGuiCol_WindowBg] = panel;
    colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_PopupBg] = rgb(0x1a1c21, 0.98f);
    colors[ImGuiCol_Border] = border;
    colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_FrameBg] = background;
    colors[ImGuiCol_FrameBgHovered] = raised;
    colors[ImGuiCol_FrameBgActive] = hover;
    colors[ImGuiCol_TitleBg] = background;
    colors[ImGuiCol_TitleBgActive] = background;
    colors[ImGuiCol_TitleBgCollapsed] = background;
    colors[ImGuiCol_MenuBarBg] = background;
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ScrollbarGrab] = raised;
    colors[ImGuiCol_ScrollbarGrabHovered] = hover;
    colors[ImGuiCol_ScrollbarGrabActive] = active;
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = rgb(0xff9455);
    colors[ImGuiCol_Button] = raised;
    colors[ImGuiCol_ButtonHovered] = hover;
    colors[ImGuiCol_ButtonActive] = active;
    colors[ImGuiCol_Header] = raised;
    colors[ImGuiCol_HeaderHovered] = hover;
    colors[ImGuiCol_HeaderActive] = active;
    colors[ImGuiCol_Separator] = border;
    colors[ImGuiCol_SeparatorHovered] = accent;
    colors[ImGuiCol_SeparatorActive] = accent;
    colors[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ResizeGripHovered] = rgb(0xff7a2f, 0.6f);
    colors[ImGuiCol_ResizeGripActive] = accent;
    colors[ImGuiCol_Tab] = background;
    colors[ImGuiCol_TabHovered] = hover;
    colors[ImGuiCol_TabSelected] = panel;
    colors[ImGuiCol_TabSelectedOverline] = accent;
    colors[ImGuiCol_TabDimmed] = background;
    colors[ImGuiCol_TabDimmedSelected] = panel;
    colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_DockingPreview] = rgb(0xff7a2f, 0.45f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableHeaderBg] = background;
    colors[ImGuiCol_TableBorderStrong] = border;
    colors[ImGuiCol_TableBorderLight] = rgb(0x24272d);
    colors[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt] = rgb(0xffffff, 0.02f);
    colors[ImGuiCol_TextSelectedBg] = rgb(0xff7a2f, 0.35f);
    colors[ImGuiCol_DragDropTarget] = accent;
    colors[ImGuiCol_NavCursor] = accent;
    colors[ImGuiCol_ModalWindowDimBg] = rgb(0x000000, 0.55f);
    style.ScaleAllSizes(scale);

    ImGuizmo::Style& gizmo = ImGuizmo::GetStyle();
    gizmo.TranslationLineThickness = 4.0f;
    gizmo.TranslationLineArrowSize = 8.0f;
    gizmo.RotationLineThickness = 3.5f;
    gizmo.RotationOuterLineThickness = 2.5f;
    gizmo.ScaleLineThickness = 4.0f;
    gizmo.ScaleLineCircleSize = 7.0f;
    gizmo.CenterCircleSize = 6.0f;
    gizmo.Colors[ImGuizmo::DIRECTION_X] = rgb(0xe5484d);
    gizmo.Colors[ImGuizmo::DIRECTION_Y] = rgb(0x46a758);
    gizmo.Colors[ImGuizmo::DIRECTION_Z] = rgb(0x3e7bfa);
    gizmo.Colors[ImGuizmo::PLANE_X] = rgb(0xe5484d, 0.45f);
    gizmo.Colors[ImGuizmo::PLANE_Y] = rgb(0x46a758, 0.45f);
    gizmo.Colors[ImGuizmo::PLANE_Z] = rgb(0x3e7bfa, 0.45f);
    gizmo.Colors[ImGuizmo::SELECTION] = rgb(0xffc04d);
    gizmo.Colors[ImGuizmo::ROTATION_USING_BORDER] = accent;
    gizmo.Colors[ImGuizmo::ROTATION_USING_FILL] = rgb(0xff7a2f, 0.35f);
    gizmo.Colors[ImGuizmo::TEXT] = text;
}

}  // namespace molten::editor
