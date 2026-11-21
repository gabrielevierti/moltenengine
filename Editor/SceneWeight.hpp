// SceneWeight.hpp
// What a scene weighs, object by object: the triangles each draws, the draw calls it costs, and
// the memory of its model and its pictures. Models and pictures several objects share are
// counted once in the totals, and each row says how many share them. Window > Scene Weight
// shows it; the editor fills in the parts from the scene, this sums and sorts them.
#pragma once

#include "Scene/Scene.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace molten {

/// One object as the editor found it.
struct WeightPart {
    Entity entity;
    std::string name;
    const void* mesh = nullptr;                 // what it draws: shared models are the same pointer
    size_t triangles = 0;                       // of one copy
    size_t copies = 1;                          // drawn this many times (foliage, instances)
    size_t meshBytes = 0;
    std::vector<std::pair<const void*, size_t>> textures;   // each picture, and its bytes
    const void* material = nullptr;             // with the model: what the renderer draws together
};

struct EntityWeight {
    Entity entity;
    std::string name;
    size_t triangles = 0;                       // all its copies
    size_t meshBytes = 0, textureBytes = 0;
    int sharedWith = 0;                         // how many others draw the same model
    int drawnWith = 0;                          // how many others are drawn in the same call (same model and material)
};

struct WeightReport {
    std::vector<EntityWeight> rows;
    size_t triangles = 0, meshBytes = 0, textureBytes = 0;
    int draws = 0, meshes = 0, textures = 0;    // draws: one for each model and material drawn (copies are drawn together)
};

enum class WeightOrder { Triangles, Memory, Name };

WeightReport weighScene(const std::vector<WeightPart>& parts, WeightOrder order = WeightOrder::Triangles);

/// The bytes of a picture: width x height x bytes a pixel, a third more with its mipmaps.
size_t pictureBytes(int width, int height, int bytesPerPixel, bool hasMipmaps);
/// "1.5 MB", "820 KB", "96 B".
std::string bytesText(size_t bytes);

}  // namespace molten
