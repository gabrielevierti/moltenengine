// ImGuiLayer.hpp
// Dear ImGui on top of the engine: GLFW feeds it events, Metal draws it, and the
// engine's renderer calls it at the end of its last pass (Renderer::overlay).
#pragma once

#include "MoltenMetal.hpp"

#include <string>

namespace molten::editor {

class ImGuiLayer {
public:
    /// Needs a window: returns false when the engine runs without one (--capture).
    /// `settingsFile` is where the layout of the windows is remembered.
    bool start(Engine& engine, const std::string& settingsFile);
    void shutdown(Engine& engine);
    bool isActive() const { return isActive_; }

    void beginFrame();
    void endFrame();

    /// A texture as ImGui::Image wants it.
    static ImTextureID textureId(const Texture* texture);

private:
    void draw(MTL::CommandBuffer* commandBuffer, MTL::RenderCommandEncoder* encoder);

    bool isActive_ = false;
    bool hasFrame_ = false;
    std::string settingsFile_;
    NS::SharedPtr<MTL::Texture> formatTexture_;             // only tells ImGui which pixel format it draws into
    NS::SharedPtr<MTL::RenderPassDescriptor> formatPass_;
};

/// Molten's look: dark, quiet, one warm accent.
void applyTheme(float scale);

}  // namespace molten::editor
