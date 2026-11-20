// LevelOfDetail.hpp
// Choosing, for one view, which version of each model under a LOD Group it draws.
#pragma once

#include "Math/Math.hpp"

namespace molten {

class Scene;

/// Marks every Mesh Renderer under a LOD Group as drawn or not from `eye`: the group's far
/// child beyond its distance, everything else within it. Renderers under no group are drawn.
void chooseLevelsOfDetail(Scene& scene, Vec3 eye);

}  // namespace molten
