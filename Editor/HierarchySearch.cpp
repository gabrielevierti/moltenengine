#include "HierarchySearch.hpp"

#include "Game/Component.hpp"
#include "Physics/Physics.hpp"
#include "Rendering/Particles.hpp"
#include "Rendering/Renderer.hpp"
#include "Scene/Components.hpp"
#include "Scene/SceneFile.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace molten {

namespace {

std::string lower(std::string text) {
    for (char& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool contains(const std::string& text, const std::string& lowerPart) {
    return lower(text).find(lowerPart) != std::string::npos;
}

const char* const kLayers[] = {"Default", "Transparent", "Ignore Raycast", "Water", "UI", "Player", "Enemy", "Scenery"};

}  // namespace

std::vector<SearchWord> parseSearch(const std::string& query) {
    std::vector<SearchWord> words;
    std::istringstream stream(query);
    std::string word;
    while (stream >> word) {
        SearchWord parsed;
        if (word.size() > 1 && word[0] == '-') parsed.isNot = true, word = word.substr(1);
        std::string low = lower(word);
        auto prefixed = [&](const char* prefix, SearchWord::Kind kind) {
            size_t length = std::char_traits<char>::length(prefix);
            if (low.compare(0, length, prefix) != 0 || low.size() <= length) return false;
            parsed.kind = kind;
            parsed.text = low.substr(length);
            return true;
        };
        if (low == "is:off") parsed.kind = SearchWord::Kind::IsOff;
        else if (low == "is:on") parsed.kind = SearchWord::Kind::IsOn;
        else if (!prefixed("c:", SearchWord::Kind::Component) && !prefixed("t:", SearchWord::Kind::Component) &&
                 !prefixed("tag:", SearchWord::Kind::Tag) && !prefixed("layer:", SearchWord::Kind::Layer) &&
                 !prefixed("prefab:", SearchWord::Kind::Prefab)) {
            parsed.text = low;
        }
        words.push_back(parsed);
    }
    return words;
}

std::vector<std::string> componentNamesOf(Scene& scene, Entity entity) {
    std::vector<std::string> names;
    if (scene.has<Transform>(entity)) names.push_back("Transform");
    if (scene.has<MeshRenderer>(entity)) names.push_back("Mesh Renderer");
    if (const Light* light = scene.get<Light>(entity)) {
        names.push_back("Light");
        names.push_back(light->type == LightType::Directional ? "Directional Light" : light->type == LightType::Spot ? "Spot Light" : "Point Light");
    }
    if (scene.has<Camera>(entity)) names.push_back("Camera");
    if (const Collider* collider = scene.get<Collider>(entity)) {
        names.push_back("Collider");
        names.push_back(collider->isTrigger ? "Trigger" : "Solid");
    }
    if (scene.has<RigidBody>(entity)) names.push_back("Rigidbody");
    if (scene.has<ParticleEmitter>(entity)) names.push_back("Particle System");
    if (scene.has<PrefabInstance>(entity)) names.push_back("Prefab");
    for (const ComponentType& type : ComponentRegistry::instance().types()) {
        if (type.has && type.has(scene, entity)) {
            names.push_back(ComponentRegistry::displayName(type.name));
            names.push_back(type.name);
        }
    }
    return names;
}

bool matchesSearch(Scene& scene, Entity entity, const std::string& name, const std::vector<SearchWord>& words) {
    std::vector<std::string> components;
    bool knowsComponents = false;
    for (const SearchWord& word : words) {
        bool holds = false;
        switch (word.kind) {
            case SearchWord::Kind::Name: holds = contains(name, word.text); break;
            case SearchWord::Kind::Component: {
                if (!knowsComponents) components = componentNamesOf(scene, entity), knowsComponents = true;
                // "meshrenderer" finds "Mesh Renderer" too.
                std::string squeezed = word.text;
                squeezed.erase(std::remove(squeezed.begin(), squeezed.end(), ' '), squeezed.end());
                for (const std::string& component : components) {
                    std::string low = lower(component);
                    std::string tight = low;
                    tight.erase(std::remove(tight.begin(), tight.end(), ' '), tight.end());
                    if (low.find(word.text) != std::string::npos || tight.find(squeezed) != std::string::npos) holds = true;
                }
                break;
            }
            case SearchWord::Kind::Tag: {
                const Tag* tag = scene.get<Tag>(entity);
                holds = contains(tag ? tag->value : "Untagged", word.text);
                break;
            }
            case SearchWord::Kind::Layer: {
                const Tag* tag = scene.get<Tag>(entity);
                int layer = tag ? tag->layer : 0;
                std::string layerName = layer >= 0 && layer < 8 ? kLayers[layer] : "Layer " + std::to_string(layer);
                holds = word.text == std::to_string(layer) || contains(layerName, word.text);
                break;
            }
            case SearchWord::Kind::Prefab: {
                const PrefabInstance* instance = scene.get<PrefabInstance>(entity);
                holds = instance && contains(instance->prefab, word.text);
                break;
            }
            case SearchWord::Kind::IsOff:
            case SearchWord::Kind::IsOn: {
                const Transform* transform = scene.get<Transform>(entity);
                bool isOn = !transform || transform->isActive;
                holds = (word.kind == SearchWord::Kind::IsOn) == isOn;
                break;
            }
        }
        if (holds == word.isNot) return false;
    }
    return true;
}

}  // namespace molten
