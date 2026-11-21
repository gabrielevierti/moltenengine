// MultiEdit.hpp
// Editing many entities at once: the Inspector shows the last one selected, and what is changed
// on it is changed on every other one selected too, value by value. Setting x of the position
// sets it on all, each keeping its own y and z; a light's intensity set on one is set on all,
// their colors left as they were; a component added or removed is added to or removed from each.
//
// It works by comparing the entity before and after it was edited: its components' values, and
// the lines the scene file writes for it. Nothing here draws.
#pragma once

#include "Scene/SceneFile.hpp"

#include <map>
#include <string>
#include <vector>

namespace molten {

/// What an entity is, as text: by "Component\nfield" for the transform and the declared
/// components, and by line for the built-in components (light, camera, collider...).
struct EntityPicture {
    std::map<std::string, std::string> values;
    std::map<std::string, std::vector<std::string>> lines;      // by the line's first word: its words after it
};

EntityPicture pictureOf(Scene& scene, Entity entity, const std::vector<NamedMaterial>& materials);

/// Does to each of `others` what was done to the entity that was `before` and is now `after`.
/// Returns how many values (or components) were changed on them.
int spreadChanges(Scene& scene, const EntityPicture& before, const EntityPicture& after, Entity edited, const std::vector<Entity>& others,
                  const std::vector<NamedMaterial>& materials, const SceneAssets& assets);

/// The words of a line, a "quoted string" as one.
std::vector<std::string> splitWords(const std::string& line);

}  // namespace molten
