// AssetDependencies.hpp
// What uses what: a scene its models and materials, a material its pictures, a script the
// sounds it plays. Found by reading the project's text files for the names of its assets.
#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace molten {

class AssetDependencies {
public:
    /// Reads the files of a project's Assets folder (`root`); `files` are their paths in it.
    void build(const std::string& root, const std::vector<std::string>& files);
    /// The same, from texts already read (for tests, and for files not on disk).
    void build(const std::unordered_map<std::string, std::string>& texts, const std::vector<std::string>& files);

    /// The assets `text` (the file `from`) names: "Materials/Asphalt.material", the model of
    /// "Models/car.glb#3", a picture named beside a model ("wheel.png" in Models/).
    static std::vector<std::string> namedIn(const std::string& text, const std::string& from, const std::unordered_set<std::string>& known);
    /// Is the file one that can name other assets (a scene, a material, a script...)?
    static bool canName(const std::string& path);

    const std::vector<std::string>& uses(const std::string& path) const;
    const std::vector<std::string>& usedBy(const std::string& path) const;
    /// The assets nothing reaches from a scene, a script or the project: pictures, models,
    /// sounds, materials, prefabs and effects that could go. (Code that builds a name from
    /// pieces cannot be read: look before deleting.)
    std::vector<std::string> unused() const;

private:
    std::vector<std::string> files_;
    std::unordered_map<std::string, std::vector<std::string>> uses_, usedBy_;
};

}  // namespace molten
