// Foliage: copies painted on the ground, kept apart, taken away, kept in a file, drawn together.
#include "Test.hpp"

#include "Core/World.hpp"
#include "Kit/Foliage.hpp"
#include "Kit/Kit.hpp"
#include "Rendering/Material.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>

using namespace molten;
using namespace molten::kit;

TEST(foliage_is_painted_and_kept) {
    std::string root = "/tmp/molten-test-foliage";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    World world;
    world.assetRoot = root;
    kit::registerKit();
    Entity field = world.scene.create("Grass");
    world.scene.add(field, Transform{});
    Foliage grass;
    grass.density = 4;
    grass.spacing = 0.3f;
    world.scene.add(field, grass);
    Foliage* painted = world.scene.get<Foliage>(field);
    // A slope: the ground is higher toward +x. Nothing grows where x is beyond 8 (water).
    auto ground = [](Vec2 at) -> std::optional<Vec3> {
        if (at.x > 8) return std::nullopt;
        return Vec3{at.x, at.x * 0.5f, at.y};
    };
    int put = painted->paint(Vec2{0, 0}, 5, ground);
    CHECK(put > 150 && put <= int(3.14159f * 25 * 4) + 1);      // at most four a square meter, fewer where they would crowd
    for (const FoliageCopy& copy : painted->copies) {
        CHECK(std::abs(copy.position.y - copy.position.x * 0.5f) < 1e-4f);    // on the ground
        CHECK(copy.scale >= 0.8f && copy.scale <= 1.25f);
    }
    // Never two closer than the spacing, however often the same place is painted.
    for (int i = 0; i < 5; i++) painted->paint(Vec2{0, 0}, 5, ground);
    size_t crowded = 0;
    for (size_t a = 0; a < painted->copies.size(); a++) {
        for (size_t b = a + 1; b < painted->copies.size(); b++) {
            Vec3 d = painted->copies[a].position - painted->copies[b].position;
            if (d.x * d.x + d.z * d.z < 0.3f * 0.3f - 1e-5f) crowded++;
        }
    }
    CHECK(crowded == 0);
    // Not where there is no ground.
    CHECK(painted->paint(Vec2{12, 0}, 2, ground) == 0);
    // Taken away in a circle.
    size_t before = painted->copies.size();
    int taken = painted->erase(Vec2{0, 0}, 2);
    CHECK(taken > 0 && painted->copies.size() == before - size_t(taken));
    for (const FoliageCopy& copy : painted->copies) CHECK(copy.position.x * copy.position.x + copy.position.z * copy.position.z > 4 - 1e-3f);
    // Drawn as copies of one mesh.
    world.advance(1.0 / 60.0);
    const MeshInstances* shown = world.scene.get<MeshInstances>(field);
    CHECK(shown && shown->places.size() == painted->copies.size() && shown->drawDistance == 120);
    // Kept in its file and read back.
    painted->places = "Foliage/Grass.foliage";
    CHECK(painted->save(world));
    Foliage again;
    again.places = painted->places;
    CHECK(again.load(world) && again.copies.size() == painted->copies.size());
    CHECK(simd::length(again.copies[3].position - painted->copies[3].position) < 1e-6f && again.copies[3].turn == painted->copies[3].turn);
}
