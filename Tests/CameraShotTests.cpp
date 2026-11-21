// Camera shots: the camera goes to the shot that matters most, smoothly.
#include "Test.hpp"

#include "Core/World.hpp"
#include "Kit/CameraShots.hpp"
#include "Kit/Kit.hpp"
#include "Scene/Components.hpp"

#include <cmath>

using namespace molten;
using namespace molten::kit;

TEST(the_camera_blends_between_shots) {
    World world;
    kit::registerKit();
    Entity eye = world.scene.create("Camera");
    world.scene.add(eye, Transform{});
    world.scene.add(eye, Camera{});
    world.scene.add(eye, CameraDirector{});
    auto shot = [&](const char* name, Vec3 at, int priority, float blend, float fov) {
        Entity entity = world.scene.create(name);
        Transform transform;
        transform.position = at;
        world.scene.add(entity, transform);
        world.scene.add(entity, CameraShot{priority, blend, fov});
        return entity;
    };
    shot("Wide", Vec3{0, 10, 20}, 10, 1.0f, 60);
    Entity close = shot("Close", Vec3{0, 2, 4}, 20, 1.0f, 35);
    world.scene.get<Transform>(close)->isActive = false;
    auto play = [&](float seconds) {
        for (int i = 0; i < int(std::lround(seconds * 60)); i++) world.advance(1.0 / 60.0);
    };
    play(0.1f);
    // The first shot is taken at once.
    CHECK(simd::length(world.scene.get<Transform>(eye)->position - Vec3{0, 10, 20}) < 1e-3f);
    CHECK(std::abs(world.scene.get<Camera>(eye)->fovY * 57.29578f - 60) < 0.01f);
    // The close shot switched on: higher, so the camera goes there, over a second.
    world.scene.get<Transform>(close)->isActive = true;
    play(0.5f);
    Vec3 halfway = world.scene.get<Transform>(eye)->position;
    CHECK(halfway.z < 18 && halfway.z > 6);
    CHECK(world.scene.get<CameraDirector>(eye)->isBlending());
    play(0.6f);
    CHECK(simd::length(world.scene.get<Transform>(eye)->position - Vec3{0, 2, 4}) < 1e-3f);
    CHECK(std::abs(world.scene.get<Camera>(eye)->fovY * 57.29578f - 35) < 0.01f);
    // Off again: back to the wide one, cut when its blend is 0.
    world.scene.get<CameraShot>(world.find("Wide"))->blendSeconds = 0;
    world.scene.get<Transform>(close)->isActive = false;
    play(1.0f / 60.0f);
    CHECK(simd::length(world.scene.get<Transform>(eye)->position - Vec3{0, 10, 20}) < 1e-3f);
}
