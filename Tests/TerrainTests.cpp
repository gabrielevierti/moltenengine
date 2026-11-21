// Terrain: heights shaped with brushes, kept in a file, solid to stand on.
#include "Test.hpp"

#include "Core/World.hpp"
#include "Kit/Kit.hpp"
#include "Kit/Terrain.hpp"

#include <cmath>
#include <filesystem>

using namespace molten;
using namespace molten::kit;

TEST(terrain_is_shaped_kept_and_stood_on) {
    std::string root = "/tmp/molten-test-terrain";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    World world;
    world.assetRoot = root;
    kit::registerKit();
    Entity ground = world.scene.create("Ground");
    world.scene.add(ground, Transform{});
    Terrain terrain;
    terrain.size = Vec2{100, 100};
    terrain.resolution = 50;                                    // squares of 2 meters
    world.scene.add(ground, terrain);
    world.scene.add(ground, MeshRenderer{});
    world.scene.updateTransforms();
    Terrain* shaped = world.scene.get<Terrain>(ground);
    CHECK(!shaped->load(world));                                // no file: flat
    CHECK(shaped->samples.size() == 51 * 51 && shaped->heightAt(Vec2{10, -20}) == 0);

    // A hill: highest in the middle of the brush, nothing at its edge.
    for (int i = 0; i < 10; i++) shaped->sculpt(Vec2{0, 0}, 20, 1.0f, TerrainBrush::Raise);
    CHECK(std::abs(shaped->heightAt(Vec2{0, 0}) - 10) < 1e-3f);
    CHECK(shaped->heightAt(Vec2{10, 0}) > 3 && shaped->heightAt(Vec2{10, 0}) < 7);
    CHECK(shaped->heightAt(Vec2{30, 0}) == 0);
    Vec3 slope = shaped->normalAt(Vec2{10, 0});
    CHECK(slope.x > 0.2f && slope.y > 0.5f);                      // its side faces out
    // Smoothed, the top comes down; flattened, it goes to the level asked.
    float top = shaped->heightAt(Vec2{0, 0});
    for (int i = 0; i < 5; i++) shaped->sculpt(Vec2{0, 0}, 20, 1.0f, TerrainBrush::Smooth);
    CHECK(shaped->heightAt(Vec2{0, 0}) < top);
    for (int i = 0; i < 40; i++) shaped->sculpt(Vec2{0, 0}, 6, 0.5f, TerrainBrush::Flatten, 4.0f);
    CHECK(std::abs(shaped->heightAt(Vec2{0, 0}) - 4) < 0.05f);

    // The mesh: a square of two triangles for each square of it, facing up.
    MeshData mesh = Terrain::makeMesh(shaped->samples, 50, terrain.size, 8);
    CHECK(mesh.positions.size() == 51 * 51 && mesh.indices.size() == 50 * 50 * 6);
    Vec3 a = mesh.positions[mesh.indices[0]], b = mesh.positions[mesh.indices[1]], c = mesh.positions[mesh.indices[2]];
    CHECK(simd::cross(b - a, c - a).y > 0);

    // A ray from above finds it, and the physics stands on it.
    shaped->rebuild(world, ground);
    auto hit = shaped->raycast(world, ground, Vec3{0, 50, 0}, Vec3{0, -1, 0});
    CHECK(hit && std::abs(hit->y - shaped->heightAt(Vec2{0, 0})) < 0.01f);
    auto slanted = shaped->raycast(world, ground, Vec3{-60, 30, 0}, simd::normalize(Vec3{1, -0.5f, 0}));
    CHECK(slanted && std::abs(slanted->y - shaped->heightAt(Vec2{slanted->x, slanted->z})) < 0.02f);
    world.advance(1.0 / 60.0);
    auto ground_hit = world.physics.raycast(world.scene, Ray{Vec3{10, 50, 0}, Vec3{0, -1, 0}}, 100);
    CHECK(ground_hit && std::abs(ground_hit->point.y - shaped->heightAt(Vec2{10, 0})) < 0.05f);

    // Kept in its file, and read back at another resolution.
    shaped->heights = "Terrains/Hill.terrain";
    CHECK(shaped->save(world));
    Terrain again;
    again.heights = "Terrains/Hill.terrain";
    again.size = terrain.size;
    again.resolution = 100;
    CHECK(again.load(world));
    CHECK(std::abs(again.heightAt(Vec2{10, 0}) - shaped->heightAt(Vec2{10, 0})) < 0.2f);
    CHECK(std::abs(again.heightAt(Vec2{0, 0}) - shaped->heightAt(Vec2{0, 0})) < 0.05f);
}
