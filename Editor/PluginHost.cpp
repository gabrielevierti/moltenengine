#include "Editor.hpp"

#include <dlfcn.h>

#include <algorithm>
#include <filesystem>

namespace molten::editor {

// ---------------------------------------------------------------- the registry

Panel* Registry::addPanel(const void* owner, std::unique_ptr<Panel> panel) {
    Panel* result = panel.get();
    panels.push_back({owner, std::move(panel)});
    return result;
}

void Registry::addMenuItem(const void* owner, std::string path, std::function<void(EditorContext&)> action, std::string shortcut,
                           std::function<bool(EditorContext&)> isEnabled) {
    menuItems.push_back({owner, MenuItem{std::move(path), std::move(shortcut), std::move(action), std::move(isEnabled)}});
}

void Registry::addComponent(const void* owner, ComponentEditor editor) {
    components.push_back({owner, std::move(editor)});
}

void Registry::addCreateEntry(const void* owner, std::string path, std::function<Entity(EditorContext&)> create) {
    createEntries.push_back({owner, CreateEntry{std::move(path), std::move(create)}});
}

void Registry::addViewportOverlay(const void* owner, std::function<void(EditorContext&, const ViewportInfo&)> draw) {
    viewportOverlays.push_back({owner, std::move(draw)});
}

void Registry::removeAll(const void* owner) {
    auto erase = [owner](auto& list) {
        list.erase(std::remove_if(list.begin(), list.end(), [owner](const auto& item) { return item.owner == owner; }), list.end());
    };
    erase(panels);
    erase(menuItems);
    erase(components);
    erase(createEntries);
    erase(viewportOverlays);
}

Panel* Registry::findPanel(const std::string& title) {
    for (auto& item : panels) {
        if (item.value->title == title) return item.value.get();
    }
    return nullptr;
}

// ---------------------------------------------------------------- the context

void EditorContext::notify(const std::string& message, ConsoleMessage::Level level) {
    statusText = message;
    statusUntil = engine.time.elapsed + 5.0;
    if (!console.empty() && console.back().text == message && console.back().level == level) {
        console.back().count++;
    } else {
        console.push_back(ConsoleMessage{level, message, engine.time.elapsed, 1});
    }
}

void EditorContext::openPanel(const std::string& title) {
    if (Panel* panel = registry.findPanel(title)) panel->isOpen = true;
    panelToFocus = title;
}

void EditorContext::editGraph(const std::string& path) {
    graphToEdit = path;
    if (Panel* panel = registry.findPanel("Visual Script")) panel->isOpen = true;
}

bool ViewportInfo::worldToScreen(Vec3 world, ImVec2& screen) const {
    Vec4 clip = projection * view * vec4(world, 1);
    if (clip.w <= 0.001f) return false;
    screen = ImVec2(position.x + (clip.x / clip.w * 0.5f + 0.5f) * size.x, position.y + (0.5f - clip.y / clip.w * 0.5f) * size.y);
    return true;
}

Ray ViewportInfo::screenRay(ImVec2 screen) const {
    if (size.x <= 0 || size.y <= 0) return Ray{};
    // From points to normalized device coordinates, then back through the camera.
    Vec2 ndc{(screen.x - position.x) / size.x * 2.0f - 1.0f, 1.0f - (screen.y - position.y) / size.y * 2.0f};
    Mat4 inverseViewProjection = simd::inverse(projection * view);
    Vec4 nearPoint = inverseViewProjection * Vec4{ndc.x, ndc.y, 0, 1};
    Vec4 farPoint = inverseViewProjection * Vec4{ndc.x, ndc.y, 1, 1};
    Vec3 origin = xyz(nearPoint) / nearPoint.w;
    return Ray{origin, safeNormalize(xyz(farPoint) / farPoint.w - origin, Vec3{0, 0, -1})};
}

// ---------------------------------------------------------------- plugins

bool PluginHost::load(EditorContext& editor, const std::string& path, bool isFromProject) {
    for (const Loaded& loaded : plugins_) {
        if (loaded.path == path) return false;      // already in
    }
    void* library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        editor.notify("Plugin " + path + " cannot be loaded: " + dlerror(), ConsoleMessage::Level::Error);
        return false;
    }
    using VersionFunction = int (*)();
    using CreateFunction = Plugin* (*)();
    auto version = reinterpret_cast<VersionFunction>(dlsym(library, "moltenEditorPluginAPIVersion"));
    auto create = reinterpret_cast<CreateFunction>(dlsym(library, "moltenCreateEditorPlugin"));
    std::string fileName = std::filesystem::path(path).filename().string();
    if (!version || !create) {
        editor.notify(fileName + " is not a Molten Metal Engine editor plugin (MOLTEN_EDITOR_PLUGIN is missing)", ConsoleMessage::Level::Error);
        dlclose(library);
        return false;
    }
    if (version() != MOLTEN_EDITOR_API_VERSION) {
        editor.notify(fileName + " was built for editor API " + std::to_string(version()) + ", this editor has " +
                          std::to_string(MOLTEN_EDITOR_API_VERSION) + ": rebuild the plugin",
                      ConsoleMessage::Level::Error);
        dlclose(library);
        return false;
    }
    Plugin* plugin = create();
    if (!plugin) {
        dlclose(library);
        return false;
    }
    Loaded loaded;
    loaded.path = path;
    loaded.name = plugin->name();
    loaded.library = library;
    loaded.plugin = plugin;
    loaded.isFromProject = isFromProject;
    plugin->onLoad(editor);
    plugins_.push_back(loaded);
    editor.notify("Loaded plugin " + loaded.name);
    return true;
}

void PluginHost::unload(EditorContext& editor, Loaded& loaded) {
    loaded.plugin->onUnload(editor);
    editor.registry.removeAll(loaded.plugin);       // before the code that draws them goes away
    delete loaded.plugin;
    dlclose(loaded.library);
}

int PluginHost::loadDirectory(EditorContext& editor, const std::string& directory, bool isFromProject) {
    std::vector<std::string> paths;
    std::error_code code;
    for (const auto& entry : std::filesystem::directory_iterator(directory, code)) {
        if (entry.path().extension() == ".dylib") paths.push_back(entry.path().string());
    }
    std::sort(paths.begin(), paths.end());
    int count = 0;
    for (const std::string& path : paths) {
        if (load(editor, path, isFromProject)) count++;
    }
    return count;
}

void PluginHost::unloadProjectPlugins(EditorContext& editor) {
    for (Loaded& loaded : plugins_) {
        if (loaded.isFromProject) unload(editor, loaded);
    }
    plugins_.erase(std::remove_if(plugins_.begin(), plugins_.end(), [](const Loaded& loaded) { return loaded.isFromProject; }),
                   plugins_.end());
}

void PluginHost::unloadAll(EditorContext& editor) {
    for (Loaded& loaded : plugins_) unload(editor, loaded);
    plugins_.clear();
}

void PluginHost::update(EditorContext& editor, float dt) {
    for (Loaded& loaded : plugins_) loaded.plugin->onUpdate(editor, dt);
}

}  // namespace molten::editor
