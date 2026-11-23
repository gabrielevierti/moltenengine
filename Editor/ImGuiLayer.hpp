// ImGuiLayer.hpp
// Dear ImGui on top of the engine: GLFW feeds it events, Metal draws it, and the
// engine's renderer calls it at the end of its last pass (Renderer::overlay).
#pragma once

#include "Theme.hpp"

#include "MoltenMetal.hpp"

#include <string>

namespace molten::editor {

class ImGuiLayer {
public:
    /// `settingsFile` is where the layout of the windows is remembered. Without a window
    /// (--capture) the interface is still drawn, at the size of the picture, so that a
    /// capture of the editor shows the editor; nothing is remembered then.
    bool start(Engine& engine, const std::string& settingsFile);
    void shutdown(Engine& engine);
    bool isActive() const { return isActive_; }

    void beginFrame();
    void endFrame();

    /// A texture as ImGui::Image wants it.
    static ImTextureID textureId(const Texture* texture);

    /// The font of the editor's icons (Assets/Fonts/Icons.ttf), to draw one at any size; nullptr if
    /// it could not be read. They are also in the text font: an icon can stand in any label.
    static ImFont*& iconFont();

private:
    static std::vector<MTL::Texture*>& heldForTheFrame();

public:

private:
    void draw(MTL::CommandBuffer* commandBuffer, MTL::RenderCommandEncoder* encoder);

    bool isActive_ = false;
    bool hasFrame_ = false;
    bool hasWindow_ = false;
    Engine* engine_ = nullptr;
    std::string settingsFile_;
    NS::SharedPtr<MTL::Texture> formatTexture_;             // only tells ImGui which pixel format it draws into
    NS::SharedPtr<MTL::RenderPassDescriptor> formatPass_;
};


}  // namespace molten::editor
