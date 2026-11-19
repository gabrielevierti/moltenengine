// World.hpp
// The place to fly: a mowed field ringed by hills and trees, the gates of the course,
// an unfinished concrete building to thread, and the pilot's tent by the start.
// Everything is made in code from boxes, cylinders, cones and noise: no model files.
#pragma once

#include "FlightModel.hpp"
#include "MoltenMetal.hpp"
#include "Track.hpp"

#include <memory>
#include <vector>

namespace fpv {

/// The height of the ground under a point. The field is flat, at zero; hills rise around it.
float groundHeight(float x, float z);
/// Which way the ground faces there.
molten::Vec3 groundNormal(float x, float z);

struct World {
    std::vector<std::vector<molten::Entity>> gateParts;         // the bars that make up each gate
    std::shared_ptr<molten::Material> gateMaterial, nextGateMaterial;
    molten::Vec3 pilot{14.0f, 1.6f, 4.0f};                      // the radio link is measured from the pilot's hands
    molten::Vec3 start{0, 0, 0};                                // the launch mat
};

World buildWorld(molten::Engine& engine);

/// The drone as something to look at: frame, motors, battery, camera, spinning propellers.
/// It is rebuilt when the pilot chooses another airframe.
struct DroneModel {
    molten::Entity root;
    std::vector<molten::Entity> parts;
    molten::Entity blades[4], discs[4];
    float bladeAngle[4] = {0, 0, 0, 0};

    static DroneModel build(molten::Engine& engine, const Airframe& airframe);
    /// Moves the model to where the drone is and turns the propellers.
    void update(molten::Engine& engine, const Drone& drone, float dt);
    void destroy(molten::Engine& engine);
};

}  // namespace fpv
