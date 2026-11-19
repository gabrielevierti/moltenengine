#include "Test.hpp"

#include "Scene/Components.hpp"

using namespace molten;

namespace {
struct Health { int value = 100; };
struct Tag { int id = 0; };
}

TEST(entities_are_created_destroyed_and_reused) {
    Scene scene;
    Entity a = scene.create("A"), b = scene.create();
    CHECK(scene.isAlive(a) && scene.isAlive(b));
    CHECK(scene.entityCount() == 2);
    scene.destroy(a);
    CHECK(scene.isAlive(a));                    // destruction is deferred
    scene.flushDestroyed();
    CHECK(!scene.isAlive(a));
    CHECK(scene.entityCount() == 1);

    Entity c = scene.create();                  // reuses the index with a new generation
    CHECK(c.index == a.index);
    CHECK(c.generation != a.generation);
    CHECK(!scene.isAlive(a));
    CHECK(scene.get<Name>(a) == nullptr);
}

TEST(components_add_get_remove) {
    Scene scene;
    Entity e = scene.create();
    CHECK(scene.get<Health>(e) == nullptr);
    scene.add(e, Health{50});
    CHECK(scene.has<Health>(e));
    CHECK(scene.get<Health>(e)->value == 50);
    scene.get<Health>(e)->value -= 20;
    CHECK(scene.get<Health>(e)->value == 30);
    scene.add(e, Health{7});                    // adding again replaces
    CHECK(scene.get<Health>(e)->value == 7);
    CHECK(scene.count<Health>() == 1);
    scene.remove<Health>(e);
    CHECK(!scene.has<Health>(e));
}

TEST(removing_keeps_other_components_intact) {
    Scene scene;
    std::vector<Entity> entities;
    for (int i = 0; i < 10; i++) {
        entities.push_back(scene.create());
        scene.add(entities.back(), Tag{i});
    }
    scene.remove<Tag>(entities[3]);
    scene.destroy(entities[0]);
    scene.flushDestroyed();
    CHECK(scene.count<Tag>() == 8);
    for (int i = 1; i < 10; i++) {
        if (i == 3) continue;
        CHECK(scene.get<Tag>(entities[size_t(i)]) != nullptr);
        CHECK(scene.get<Tag>(entities[size_t(i)])->id == i);
    }
}

TEST(each_visits_entities_with_all_components) {
    Scene scene;
    Entity both = scene.create(), onlyHealth = scene.create(), onlyTag = scene.create();
    scene.add(both, Health{1});
    scene.add(both, Tag{1});
    scene.add(onlyHealth, Health{2});
    scene.add(onlyTag, Tag{3});

    int visited = 0;
    scene.each<Health, Tag>([&](Entity entity, Health& health, Tag& tag) {
        visited++;
        CHECK(entity == both);
        health.value += tag.id;
    });
    CHECK(visited == 1);
    CHECK(scene.get<Health>(both)->value == 2);

    int healthCount = 0;
    scene.each<Health>([&](Entity, Health&) { healthCount++; });
    CHECK(healthCount == 2);
}

TEST(find_by_name) {
    Scene scene;
    scene.create("Floor");
    Entity player = scene.create("Player");
    CHECK(scene.find("Player") == player);
    CHECK(!scene.find("Nobody").isValid());
}

TEST(world_matrices_follow_the_hierarchy) {
    Scene scene;
    // Created child first, on purpose: the update must not depend on storage order.
    Entity child = scene.create(), parent = scene.create(), grandchild = scene.create();
    scene.add(child, Transform{.position = {0, 0, -2}});
    scene.add(parent, Transform{.position = {10, 0, 0}, .rotation = axisAngle(Vec3{0, 1, 0}, radians(90))});
    scene.add(grandchild, Transform{.position = {0, 1, 0}});
    scene.setParent(child, parent);
    scene.setParent(grandchild, child);
    scene.updateTransforms();

    // Rotating 90 degrees around Y turns the child's -Z offset into -X.
    Vec3 childWorld = scene.get<Transform>(child)->worldPosition();
    CHECK_NEAR(childWorld.x, 8, 1e-4); CHECK_NEAR(childWorld.z, 0, 1e-4);
    Vec3 grandchildWorld = scene.get<Transform>(grandchild)->worldPosition();
    CHECK_NEAR(grandchildWorld.x, 8, 1e-4); CHECK_NEAR(grandchildWorld.y, 1, 1e-4);
}

TEST(destroying_a_parent_destroys_its_descendants) {
    Scene scene;
    Entity parent = scene.create(), child = scene.create(), grandchild = scene.create(), other = scene.create();
    for (Entity e : {parent, child, grandchild, other}) scene.add(e, Transform{});
    scene.setParent(grandchild, child);
    scene.setParent(child, parent);
    scene.destroy(parent);
    scene.flushDestroyed();
    CHECK(!scene.isAlive(parent) && !scene.isAlive(child) && !scene.isAlive(grandchild));
    CHECK(scene.isAlive(other));
    CHECK(scene.count<Transform>() == 1);
}

TEST(camera_projection_respects_aspect) {
    Camera camera;
    camera.fovY = radians(90);
    Mat4 p = camera.projection(2.0f);
    Vec4 edge = p * Vec4{2 * 10, 0, -10, 1};        // tan(45) * aspect * distance
    CHECK_NEAR(edge.x / edge.w, 1, 1e-4);
}
