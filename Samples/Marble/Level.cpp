#include "Level.hpp"

#include <array>
#include <map>

using namespace molten;

namespace {

/// Builds the level out of boxes. Boxes of the same size share one mesh, so the
/// renderer can draw them together as instances.
struct Builder {
    Engine& engine;
    std::map<std::array<float, 3>, std::shared_ptr<Mesh>> boxMeshes;

    std::shared_ptr<Mesh> boxMesh(Vec3 size) {
        std::array<float, 3> key{size.x, size.y, size.z};
        auto found = boxMeshes.find(key);
        if (found != boxMeshes.end()) return found->second;
        // Half a texture repeat per meter: the tile texture has 4 tiles across, so tiles are 50 cm.
        std::shared_ptr<Mesh> mesh = engine.assets().makeMesh(MeshData::cube(size, 0.5f));
        boxMeshes[key] = mesh;
        return mesh;
    }

    /// A solid box: something to roll on or bump into.
    Entity block(const char* name, Vec3 center, Vec3 size, const std::shared_ptr<Material>& material, Quat rotation = Quat{}) {
        Entity entity = engine.scene.create(name);
        Transform transform;
        transform.position = center;
        transform.rotation = rotation;
        engine.scene.add(entity, transform);
        engine.scene.add(entity, MeshRenderer{boxMesh(size), material});
        engine.scene.add(entity, Collider::box(size * 0.5f));
        return entity;
    }
};

std::shared_ptr<Material> texturedMaterial(Assets& assets, const char* name) {
    std::string prefix = std::string("Textures/") + name;
    auto material = std::make_shared<Material>();
    // Factors of 1 let the textures decide: roughness comes from green, metalness from blue.
    material->metallic = 1.0f;
    material->roughness = 1.0f;
    material->baseColorTexture = assets.texture(prefix + "_basecolor.png");
    material->normalTexture = assets.texture(prefix + "_normal.png", TextureFormat::RGBA8);
    material->metallicRoughnessTexture = assets.texture(prefix + "_orm.png", TextureFormat::RGBA8);
    material->occlusionTexture = material->metallicRoughnessTexture;       // same file: occlusion is its red channel
    return material;
}

}  // namespace

Level buildLevel(Engine& engine) {
    Scene& scene = engine.scene;
    Assets& assets = engine.assets();
    Level level;
    Builder builder{engine, {}};

    // ---- materials
    auto stone = texturedMaterial(assets, "tiles");
    auto paint = assets.makeMaterial(Vec4{0.95f, 0.42f, 0.08f, 1}, 0.0f, 0.45f);          // the moving platform
    auto wood = assets.makeMaterial(Vec4{0.72f, 0.5f, 0.28f, 1}, 0.0f, 0.8f);
    wood->baseColorTexture = assets.texture("Textures/checker_basecolor.png");
    auto fence = texturedMaterial(assets, "grate");
    fence->alphaMode = AlphaMode::Mask;                 // the holes are cut out, also from its shadow
    fence->isDoubleSided = true;
    fence->uvScale = Vec2{3, 1};

    // ---- light: one sun. It is the first directional light, so it casts the shadows.
    Entity sun = scene.create("Sun");
    Transform sunTransform;
    sunTransform.rotation = lookRotation(simd::normalize(Vec3{0.55f, -0.75f, -0.4f}));     // the direction the light travels
    scene.add(sun, sunTransform);
    Light sunLight;
    sunLight.type = LightType::Directional;
    sunLight.color = Vec3{1.0f, 0.94f, 0.82f};
    sunLight.intensity = 3.5f;
    sunLight.castsShadows = true;
    scene.add(sun, sunLight);

    // ---- the course. Platforms are 1 m thick; y is the height of their top surface minus 0.5.
    builder.block("Start", Vec3{0, -0.5f, 0}, Vec3{8, 1, 8}, stone);
    builder.block("Bridge", Vec3{0, -0.5f, -9}, Vec3{2.5f, 1, 10}, stone);
    builder.block("Plaza", Vec3{0, -0.5f, -18}, Vec3{8, 1, 8}, stone);
    // A ramp is a box tilted around the z axis: it climbs 2.5 m toward +x.
    builder.block("Ramp", Vec3{8.75f, 0.72f, -18}, Vec3{10.2f, 1, 3}, stone, axisAngle(Vec3{0, 0, 1}, radians(14.5f)));
    builder.block("Terrace", Vec3{17, 2, -18}, Vec3{7, 1, 7}, stone);
    builder.block("Landing", Vec3{17, 2, -1}, Vec3{7, 1, 7}, stone);
    builder.block("Path 1", Vec3{17, 2, 4.5f}, Vec3{1.6f, 1, 4}, stone);
    builder.block("Path 2", Vec3{15, 2, 8}, Vec3{5.6f, 1, 1.6f}, stone);
    builder.block("Path 3", Vec3{13, 2, 11.5f}, Vec3{1.6f, 1, 5.4f}, stone);
    builder.block("Summit", Vec3{13, 2, 18}, Vec3{7, 1, 7}, stone);

    // Low walls so the plaza is not too easy to fall from.
    builder.block("Wall", Vec3{-3.75f, 0.3f, -18}, Vec3{0.5f, 0.6f, 8}, stone);
    builder.block("Wall", Vec3{0, 0.3f, -21.75f}, Vec3{8, 0.6f, 0.5f}, stone);

    // A fence with holes along the landing: look at its shadow. What you see is a flat
    // plane, stood up and turned to face -x; what stops the marble is an invisible box
    // on an entity of its own, because a collider follows its entity's rotation.
    Entity fenceEntity = scene.create("Fence");
    Transform fenceTransform;
    fenceTransform.position = Vec3{20.4f, 3.5f, -1};
    fenceTransform.rotation = axisAngle(Vec3{0, 1, 0}, radians(-90.0f)) * axisAngle(Vec3{1, 0, 0}, radians(90.0f));
    fenceTransform.scale = Vec3{7, 1, 2};
    scene.add(fenceEntity, fenceTransform);
    scene.add(fenceEntity, MeshRenderer{assets.plane(), fence});
    Entity fenceWall = scene.create("Fence wall");
    Transform wallTransform;
    wallTransform.position = fenceTransform.position;
    scene.add(fenceWall, wallTransform);
    scene.add(fenceWall, Collider::box(Vec3{0.1f, 1, 3.5f}));

    // The moving platform carries the marble across the gap between terrace and landing.
    Entity platform = builder.block("Ferry", Vec3{17, 2.25f, -12.4f}, Vec3{3, 0.5f, 3}, paint);
    RigidBody ferryBody;
    ferryBody.type = BodyType::Kinematic;       // it moves, but nothing can push it
    ferryBody.friction = 1.0f;
    scene.add(platform, ferryBody);
    scene.add(platform, MovingPlatform{Vec3{17, 2.25f, -12.4f}, Vec3{17, 2.25f, -6.6f}, 9.0f});
    level.ferry = platform;

    // Crates: dynamic bodies the marble can push around.
    for (int i = 0; i < 3; i++) {
        Entity crate = scene.create("Crate");
        Transform transform;
        transform.position = Vec3{-1.6f + 0.95f * float(i % 2), 0.45f + 0.9f * float(i / 2), -19.5f};
        if (i == 2) transform.position.x = -1.1f;
        scene.add(crate, transform);
        scene.add(crate, MeshRenderer{builder.boxMesh(Vec3{0.9f, 0.9f, 0.9f}), wood});
        scene.add(crate, Collider::box(Vec3{0.45f, 0.45f, 0.45f}));
        RigidBody body;
        body.mass = 0.4f;
        body.friction = 0.5f;
        scene.add(crate, body);
    }

    // ---- gems: a mesh from a file, a colored light, and a trigger to detect the marble.
    const Vec3 gemPositions[] = {
        {0, 1.0f, -7},    {0, 1.0f, -11},   {2.2f, 1.0f, -16}, {8.5f, 2.6f, -18},
        {17, 3.5f, -20},  {17, 4.6f, -9.5f}, {17, 3.5f, 4.5f},  {13, 3.5f, 11.5f},        // the sixth needs a jump from the ferry
    };
    const Vec3 gemColors[] = {{0.15f, 0.8f, 1.0f}, {1.0f, 0.3f, 0.6f}, {1.0f, 0.8f, 0.2f}, {0.4f, 1.0f, 0.5f}};
    std::shared_ptr<Mesh> gemMesh = assets.mesh("Models/gem.obj");
    std::shared_ptr<Material> gemMaterials[4];
    for (int i = 0; i < 4; i++) {
        gemMaterials[i] = assets.makeMaterial(vec4(gemColors[i], 1), 0.0f, 0.06f);
        gemMaterials[i]->emissive = gemColors[i] * 0.9f;                    // a soft glow, picked up by the bloom
    }
    for (const Vec3& position : gemPositions) {
        int index = level.gemCount++;
        Entity gem = scene.create("Gem");
        Transform transform;
        transform.position = position;
        transform.scale = Vec3{0.9f, 0.9f, 0.9f};
        scene.add(gem, transform);
        scene.add(gem, MeshRenderer{gemMesh, gemMaterials[index % 4]});
        scene.add(gem, Gem{position, 0.9f * float(index), gemColors[index % 4]});
        Light light;
        light.color = gemColors[index % 4];
        light.intensity = 5.0f;
        light.range = 5.0f;
        scene.add(gem, light);
        Collider trigger = Collider::sphere(0.75f);
        trigger.isTrigger = true;
        scene.add(gem, trigger);
    }

    // ---- the goal: a glowing disc on the summit. It opens when every gem has been collected.
    level.goalMaterial = assets.makeMaterial(Vec4{0.05f, 0.05f, 0.05f, 1}, 0.0f, 0.3f);
    level.goalMaterial->emissive = Vec3{1.2f, 0.25f, 0.1f};
    level.goal = scene.create("Goal");
    Transform goalTransform;
    goalTransform.position = Vec3{13, 2.56f, 18.5f};
    goalTransform.scale = Vec3{3, 0.12f, 3};
    scene.add(level.goal, goalTransform);
    scene.add(level.goal, MeshRenderer{assets.cylinder(), level.goalMaterial});
    Light goalLight;
    goalLight.color = Vec3{1.0f, 0.3f, 0.15f};
    goalLight.intensity = 6.0f;
    goalLight.range = 7.0f;
    scene.add(level.goal, goalLight);
    Collider goalTrigger = Collider::box(Vec3{1.2f, 1.2f, 1.2f});
    goalTrigger.offset = Vec3{0, 1.0f, 0};
    goalTrigger.isTrigger = true;
    scene.add(level.goal, goalTrigger);

    // ---- the marble
    auto marbleMaterial = assets.makeMaterial(Vec4{0.25f, 0.55f, 1.0f, 1}, 0.0f, 0.18f);
    marbleMaterial->baseColorTexture = assets.texture("Textures/checker_basecolor.png");    // so that rolling is visible
    level.start = Vec3{0, Level::kMarbleRadius + 0.05f, 2};
    level.marble = scene.create("Marble");
    Transform marbleTransform;
    marbleTransform.position = level.start;
    scene.add(level.marble, marbleTransform);
    scene.add(level.marble, MeshRenderer{assets.sphere(), marbleMaterial});
    scene.add(level.marble, Collider::sphere(Level::kMarbleRadius));
    RigidBody marbleBody;
    marbleBody.restitution = 0.35f;
    marbleBody.friction = 0.03f;                // a rolling ball barely slows down by itself
    marbleBody.linearDamping = 0.15f;
    scene.add(level.marble, marbleBody);

    // ---- the camera. The game moves it every frame.
    level.camera = scene.create("Camera");
    scene.add(level.camera, Transform{});
    Camera camera;
    camera.farPlane = 300.0f;
    scene.add(level.camera, camera);

    return level;
}
