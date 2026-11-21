// HierarchySearch.hpp
// What the Hierarchy's search box finds. Words are looked for in the names; a word with a
// prefix looks elsewhere:
//
//   crate                     named with "crate" in it (any case)
//   c:light  t:light          with a component whose name has "light" in it (Light, Point Light...)
//   tag:enemy                 tagged Enemy
//   layer:water               on the layer Water (or its number: layer:4)
//   prefab:wheel              a copy of a prefab whose file has "wheel" in its name
//   is:off   is:on            switched off, or on
//
// Every word must hold: "c:collider tag:enemy" is enemies with colliders. A word starting
// with "-" must not: "c:light -c:camera".
#pragma once

#include "Scene/Scene.hpp"

#include <string>
#include <vector>

namespace molten {

struct SearchWord {
    enum class Kind { Name, Component, Tag, Layer, Prefab, IsOff, IsOn } kind = Kind::Name;
    std::string text;               // lower case
    bool isNot = false;
};

std::vector<SearchWord> parseSearch(const std::string& query);

/// True if the entity is one the words look for. `name` is its name as the Hierarchy shows it.
bool matchesSearch(Scene& scene, Entity entity, const std::string& name, const std::vector<SearchWord>& words);

/// The names of the components an entity has, as the Inspector shows them ("Mesh Renderer").
std::vector<std::string> componentNamesOf(Scene& scene, Entity entity);

}  // namespace molten
