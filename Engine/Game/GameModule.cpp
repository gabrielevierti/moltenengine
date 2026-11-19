#include "Game/GameModule.hpp"

#include "Core/Log.hpp"
#include "Game/Component.hpp"

#include <dlfcn.h>

namespace molten {

bool GameModule::load(const std::string& libraryPath, std::string* error) {
    retire();
    ComponentRegistry& registry = ComponentRegistry::instance();
    registry.loadingModule = this;
    // RTLD_LOCAL: two builds of the same game define the same names, and each must keep its own.
    void* library = dlopen(libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    registry.loadingModule = nullptr;
    if (!library) {
        const char* reason = dlerror();
        if (error) *error = reason ? reason : "unknown error";
        registry.removeModule(this);
        return false;
    }
    isLoaded_ = true;
    path_ = libraryPath;
    generation_++;
    logInfo("game code loaded from %s (%zu component types)", libraryPath.c_str(), componentNames().size());
    return true;
}

void GameModule::retire() {
    ComponentRegistry::instance().removeModule(this);
    isLoaded_ = false;
    path_.clear();
}

std::vector<std::string> GameModule::componentNames() const {
    std::vector<std::string> names;
    for (const ComponentType& type : ComponentRegistry::instance().types()) {
        if (type.module == this) names.push_back(type.name);
    }
    return names;
}

}  // namespace molten
