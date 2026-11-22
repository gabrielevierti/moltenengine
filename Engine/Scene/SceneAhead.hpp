// SceneAhead.hpp
// What a scene needs from the disk, read before the scene is: its models made into
// triangles, its pictures into pixels.
//
// Reading a scene is quick; reading what it names is not. A car's model is a few megabytes
// of text or binary to parse, each of its pictures a JPEG to decompress. Done where the
// scene is opened, that is a second or three during which nothing moves. Here only files
// are read and only plain data is made, so it can all be done on other threads, several
// files at a time, while the game or the editor goes on drawing. The scene is then read in
// a moment, because everything slow has been done already.
#pragma once

#include "Assets/MeshData.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace molten {

struct SceneAhead {
    std::string name;                   // the scene, as it was asked for
    std::string text;                   // its file
    bool isRead = false;                // false: there is no such file

    struct Model {
        std::string name;               // as the scene names it: "Models/car.glb#3"
        MeshData data;
    };
    struct Picture {
        std::string name;               // "Textures/wall.jpg"
        std::vector<uint8_t> pixels;    // four bytes each (red, green, blue, alpha), rows from the top
        int width = 0, height = 0;
    };
    std::vector<Model> models;          // those that could be read
    std::vector<Picture> pictures;
};

/// How far the reading is, for a loading bar: files done of files to do.
struct SceneAheadProgress {
    std::atomic<int> done{0}, total{1};
    float fraction() const {
        int all = total.load();
        return all > 0 ? float(done.load()) / float(all) : 1.0f;
    }
};

/// The files a scene's text names: its models, the pictures of its materials, and the
/// material files it uses (whose pictures count too: `readText` reads one by name).
struct SceneNeeds {
    std::vector<std::string> models, pictures, materials;
};
SceneNeeds sceneNeeds(const std::string& sceneText, const std::function<bool(const std::string& name, std::string& text)>& readText);

/// Reads the scene file at `path` and everything it names. `pathOf` gives the full path of
/// a file of Assets by its name; it is called from several threads, so it must only read.
/// `threads` 0 uses what the machine has, less one for whoever is drawing.
SceneAhead readSceneAhead(const std::string& name, const std::string& path, const std::function<std::string(const std::string&)>& pathOf,
                          SceneAheadProgress* progress = nullptr, int threads = 0);

}  // namespace molten
