// GameSettings.cpp
#include "Project/GameSettings.hpp"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace molten {

GameSettings::GameSettings() {
    tags = {"Player", "Enemy", "Pickup", "Obstacle", "Ground", "MainCamera", "Finish"};
    const char* named[] = {"Default", "Transparent", "Ignore Raycast", "Water", "UI", "Player", "Enemy", "Scenery"};
    for (int i = 0; i < 8; i++) layers[size_t(i)] = named[i];
    meets.fill(0xFFFFFFFFu);
}

std::string GameSettings::layerName(int layer) const {
    if (layer < 0 || layer >= kLayerCount) return "Layer " + std::to_string(layer);
    return layers[size_t(layer)].empty() ? "Layer " + std::to_string(layer) : layers[size_t(layer)];
}

int GameSettings::layerNamed(const std::string& name) const {
    for (int i = 0; i < kLayerCount; i++) {
        if (!layers[size_t(i)].empty() && layers[size_t(i)] == name) return i;
    }
    return -1;
}

std::vector<std::string> GameSettings::allTags() const {
    std::vector<std::string> all = {"Untagged"};
    for (const std::string& tag : tags) {
        if (!tag.empty() && std::find(all.begin(), all.end(), tag) == all.end()) all.push_back(tag);
    }
    return all;
}

bool GameSettings::layersMeet(int a, int b) const {
    if (a < 0 || b < 0 || a >= kLayerCount || b >= kLayerCount) return true;
    return (meets[size_t(a)] >> uint32_t(b)) & 1u;
}

void GameSettings::setLayersMeet(int a, int b, bool meet) {
    if (a < 0 || b < 0 || a >= kLayerCount || b >= kLayerCount) return;
    if (meet) {
        meets[size_t(a)] |= 1u << uint32_t(b);
        meets[size_t(b)] |= 1u << uint32_t(a);
    } else {
        meets[size_t(a)] &= ~(1u << uint32_t(b));
        meets[size_t(b)] &= ~(1u << uint32_t(a));
    }
}

std::string GameSettings::toText() const {
    std::string out = "molten-game-settings 1\n";
    char line[160];
    std::snprintf(line, sizeof(line), "gravity %g %g %g\n", double(gravity.x), double(gravity.y), double(gravity.z));
    out += line;
    out += "stepsPerSecond " + std::to_string(stepsPerSecond) + "\n";
    for (const std::string& tag : tags) {
        if (!tag.empty()) out += "tag " + tag + "\n";
    }
    for (int i = 0; i < kLayerCount; i++) {
        if (!layers[size_t(i)].empty()) out += "layer " + std::to_string(i) + " " + layers[size_t(i)] + "\n";
    }
    // Only the pairs that do not meet are written: a new layer meets everything, as a new project's do.
    for (int a = 0; a < kLayerCount; a++) {
        for (int b = a; b < kLayerCount; b++) {
            if (!layersMeet(a, b)) out += "apart " + std::to_string(a) + " " + std::to_string(b) + "\n";
        }
    }
    return out;
}

GameSettings GameSettings::parse(const std::string& text) {
    GameSettings settings;
    bool saysTags = false, saysLayers = false;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t space = line.find(' ');
        if (space == std::string::npos) continue;
        std::string word = line.substr(0, space), rest = line.substr(space + 1);
        if (word == "gravity") {
            float x = 0, y = 0, z = 0;
            if (std::sscanf(rest.c_str(), "%f %f %f", &x, &y, &z) == 3) settings.gravity = Vec3{x, y, z};
        } else if (word == "stepsPerSecond") {
            settings.stepsPerSecond = std::clamp(std::atoi(rest.c_str()), 20, 240);
        } else if (word == "tag") {
            // The file's tags are the project's: the ones a new project has are only where it starts.
            if (!saysTags) settings.tags.clear();
            saysTags = true;
            if (!rest.empty() && rest != "Untagged") settings.tags.push_back(rest);
        } else if (word == "layer") {
            if (!saysLayers) settings.layers.fill({});
            saysLayers = true;
            size_t next = rest.find(' ');
            int index = std::atoi(rest.c_str());
            if (next != std::string::npos && index >= 0 && index < kLayerCount) settings.layers[size_t(index)] = rest.substr(next + 1);
        } else if (word == "apart") {
            int a = -1, b = -1;
            if (std::sscanf(rest.c_str(), "%d %d", &a, &b) == 2) settings.setLayersMeet(a, b, false);
        }
    }
    return settings;
}

bool GameSettings::operator==(const GameSettings& other) const {
    return tags == other.tags && layers == other.layers && meets == other.meets && gravity.x == other.gravity.x && gravity.y == other.gravity.y &&
           gravity.z == other.gravity.z && stepsPerSecond == other.stepsPerSecond;
}

}  // namespace molten
