#include <imgui.h>

#include "ImGuiLayer.hpp"

#include <ImGuizmo.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_metal.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
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
    hasWindow_ = window != nullptr;
    engine_ = &engine;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.ConfigDragClickToInputText = true;
    settingsFile_ = settingsFile;
    io.IniFilename = hasWindow_ ? settingsFile_.c_str() : nullptr;      // ImGui keeps the pointer: the string lives in this object

    // The system font where there is one; ImGui's built-in font otherwise.
    const char* chosen = std::getenv("MOLTEN_EDITOR_FONT");
    for (const char* path : {chosen ? chosen : "", "/System/Library/Fonts/SFNS.ttf", "/System/Library/Fonts/Helvetica.ttc"}) {
        std::error_code ignored;
        if (std::filesystem::exists(path, ignored) && io.Fonts->AddFontFromFileTTF(path, 14.0f)) break;
    }
    applyTheme(1.0f);

    MTL::Device* device = engine.renderer().device().device();
    // true: ImGui adds its own listeners in front of the engine's and passes every event on.
    if (hasWindow_) ImGui_ImplGlfw_InitForOther(window->nativeHandle(), true);
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
    if (hasWindow_) ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    isActive_ = false;
}

void ImGuiLayer::beginFrame() {
    if (!isActive_) return;
    ImGui_ImplMetal_NewFrame(formatPass_.get());
    if (hasWindow_) {
        ImGui_ImplGlfw_NewFrame();
    } else {
        // No window to ask: the picture's size, and the steady clock captures run on.
        ImGuiIO& io = ImGui::GetIO();
        float scale = float(std::max(engine_->config().captureScale, 1));
        io.DisplaySize = ImVec2(float(engine_->config().width), float(engine_->config().height));
        io.DisplayFramebufferScale = ImVec2(scale, scale);
        io.DeltaTime = 1.0f / 60.0f;
    }
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
    if (drawData) {
        convertColorsToLinear(drawData);
        encoder->pushDebugGroup(NS::String::string("Editor interface", NS::UTF8StringEncoding));
        ImGui_ImplMetal_RenderDrawData(drawData, commandBuffer, encoder);
        encoder->popDebugGroup();
    }
    // The encoder holds what it was given from here on: the pictures of this frame can go.
    for (MTL::Texture* held : heldForTheFrame()) held->release();
    heldForTheFrame().clear();
}

std::vector<MTL::Texture*>& ImGuiLayer::heldForTheFrame() {
    static std::vector<MTL::Texture*> held;
    return held;
}

ImTextureID ImGuiLayer::textureId(const Texture* texture) {
    if (!texture || !texture->handle()) return ImTextureID(0);
    // A picture handed to the interface is drawn at the end of the frame, by its address. Whoever
    // owns it may let it go before then (a preview made again, a cache emptied): keep it until it is drawn.
    std::vector<MTL::Texture*>& held = heldForTheFrame();
    if (held.size() < 4096) {
        texture->handle()->retain();
        held.push_back(texture->handle());
    }
    return ImTextureID(reinterpret_cast<uintptr_t>(texture->handle()));
}

}  // namespace molten::editor
