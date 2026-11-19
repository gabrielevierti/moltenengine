// Level.hpp
// The level: what is in it and where. Also the game's own components, which shows that
// a component can be any plain struct: the engine does not need to know about them.
#pragma once

#include "MoltenMetal.hpp"

/// Something to collect. The entity also carries a mesh, a light and a trigger collider.
struct Gem {
    molten::Vec3 home{0, 0, 0};         // the gem bobs up and down around this point
    float phase = 0;                // so that the gems do not all move in step
    molten::Vec3 color{1, 1, 1};
};

/// A platform that glides back and forth between two points.
struct MovingPlatform {
    molten::Vec3 from{0, 0, 0};
    molten::Vec3 to{0, 0, 0};
    float period = 6;               // seconds for a round trip
};

/// Destroys its entity when the time runs out: for one-shot effects.
struct Lifetime {
    float seconds = 1;
};

struct Level {
    molten::Entity marble;
    molten::Entity camera;
    molten::Entity goal;
    molten::Entity ferry;               // the moving platform
    molten::Vec3 start{0, 1, 0};        // where the marble begins
    int gemCount = 0;
    std::shared_ptr<molten::Material> goalMaterial;     // changes color when the goal opens

    static constexpr float kMarbleRadius = 0.5f;
};

/// Fills the scene (which must be empty) and says where the important things are.
Level buildLevel(molten::Engine& engine);
