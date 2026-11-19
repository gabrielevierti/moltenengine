#include "World.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

using namespace molten;

namespace fpv {

namespace {

const Vec2 kFieldCenter{-25.0f, -18.0f};
const float kFieldHalf = 78.0f;             // the mowed square is 156 meters on a side
const float kTerrainSize = 620.0f;

/// How far outside the mowed square a point is: 0 on the field.
float beyondField(float x, float z) {
    return std::max(std::max(std::abs(x - kFieldCenter.x), std::abs(z - kFieldCenter.y)) - kFieldHalf, 0.0f);
}

/// Noise that repeats exactly every `period` units, so that a texture made from it tiles
/// without seams: four copies shifted by one period, cross-faded.
float tilingNoise(Vec2 p, float period, int octaves, uint32_t seed) {
    float tx = p.x / period, ty = p.y / period;
    float a = fractalNoise(p, octaves, seed), b = fractalNoise(p - Vec2{period, 0}, octaves, seed);
    float c = fractalNoise(p - Vec2{0, period}, octaves, seed), d = fractalNoise(p - Vec2{period, period}, octaves, seed);
    return molten::lerp(molten::lerp(a, b, tx), molten::lerp(c, d, tx), ty);
}

/// Grass, as a texture: greens that vary in patches and from blade to blade. A mowed
/// field also shows the two directions the mower went, as lighter and darker bands.
std::shared_ptr<Texture> grassTexture(Assets& assets, bool mowed) {
    const int size = 256;
    std::vector<uint8_t> pixels(size_t(size * size * 4));
    Random random(mowed ? 3 : 4);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            Vec2 p{float(x), float(y)};
            float patches = tilingNoise(p / 40.0f, float(size) / 40.0f, 3, 21);
            float blades = tilingNoise(p / 3.0f, float(size) / 3.0f, 2, 22);
            float shade = 0.72f + 0.38f * patches + 0.22f * (blades - 0.5f) + random.range(-0.05f, 0.05f);
            if (mowed) shade *= y < size / 2 ? 1.0f : 0.86f;
            // Dry patches lean toward yellow, more so away from the mowed field.
            float dry = saturate((patches - 0.55f) * (mowed ? 1.2f : 2.6f));
            Vec3 color = molten::lerp(Vec3{0.30f, 0.46f, 0.17f}, Vec3{0.52f, 0.52f, 0.24f}, dry) * shade;
            uint8_t* pixel = &pixels[size_t((y * size + x) * 4)];
            pixel[0] = uint8_t(saturate(color.x) * 255.0f);
            pixel[1] = uint8_t(saturate(color.y) * 255.0f);
            pixel[2] = uint8_t(saturate(color.z) * 255.0f);
            pixel[3] = 255;
        }
    }
    return assets.makeTexture(pixels, size, size);
}

/// Makes boxes of any size that share one mesh per size and repeat their texture per meter.
struct Builder {
    Engine& engine;
    std::map<std::tuple<float, float, float>, std::shared_ptr<Mesh>> meshes;

    Entity box(const char* name, Vec3 center, Vec3 size, const std::shared_ptr<Material>& material, Quat rotation = Quat{},
               bool solid = true) {
        float width = size.x, height = size.y, depth = size.z;       // a vector's elements cannot be passed by reference
        auto key = std::make_tuple(width, height, depth);
        std::shared_ptr<Mesh>& mesh = meshes[key];
        if (!mesh) mesh = engine.assets().makeMesh(MeshData::cube(size, 0.5f));
        Entity entity = engine.scene.create(name);
        Transform transform;
        transform.position = center;
        transform.rotation = rotation;
        engine.scene.add(entity, transform);
        engine.scene.add(entity, MeshRenderer{mesh, material});
        if (solid) engine.scene.add(entity, Collider::box(size * 0.5f));
        return entity;
    }

    Entity shape(const char* name, const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<Material>& material, Vec3 position,
                 Vec3 scale, Vec4 tint = Vec4{1, 1, 1, 1}) {
        Entity entity = engine.scene.create(name);
        Transform transform;
        transform.position = position;
        transform.scale = scale;
        engine.scene.add(entity, transform);
        MeshRenderer renderer{mesh, material};
        renderer.tint = tint;
        engine.scene.add(entity, renderer);
        return entity;
    }
};

/// A gate is a frame of bars around its opening: two posts and a bar on top, and a bar
/// below if the opening does not reach the ground.
std::vector<Entity> buildGate(Builder& builder, const Gate& gate, const std::shared_ptr<Material>& material) {
    const float thickness = 0.28f;
    float top = gate.center.y + gate.height * 0.5f, bottom = gate.center.y - gate.height * 0.5f;
    struct Bar {
        Vec3 center, size;
    };
    std::vector<Bar> bars;
    for (float side : {-1.0f, 1.0f}) {
        float x = side * (gate.width + thickness) * 0.5f;
        bars.push_back({Vec3{x, (top + thickness) * 0.5f - gate.center.y, 0}, Vec3{thickness, top + thickness, thickness}});
    }
    bars.push_back({Vec3{0, gate.height * 0.5f + thickness * 0.5f, 0}, Vec3{gate.width, thickness, thickness}});
    if (bottom > 0.6f) bars.push_back({Vec3{0, -gate.height * 0.5f - thickness * 0.5f, 0}, Vec3{gate.width, thickness, thickness}});

    std::vector<Entity> parts;
    // The bars are given in the gate's own space: turn them to face along the course.
    for (const Bar& bar : bars) {
        parts.push_back(builder.box("Gate", gate.center + rotate(gate.rotation(), bar.center), bar.size, material, gate.rotation()));
    }
    return parts;
}

}  // namespace

float groundHeight(float x, float z) {
    float outside = beyondField(x, z);
    if (outside <= 0) return 0;
    // Gentle at first, then real hills: the field sits in a bowl.
    float hills = fractalNoise(Vec2{x, z} / 80.0f, 4, 11);
    return smoothstep(0.0f, 110.0f, outside) * (3.0f + hills * 38.0f);
}

Vec3 groundNormal(float x, float z) {
    const float step = 1.0f;
    float slopeX = (groundHeight(x + step, z) - groundHeight(x - step, z)) / (2 * step);
    float slopeZ = (groundHeight(x, z + step) - groundHeight(x, z - step)) / (2 * step);
    return simd::normalize(Vec3{-slopeX, 1, -slopeZ});
}

World buildWorld(Engine& engine) {
    Scene& scene = engine.scene;
    Assets& assets = engine.assets();
    World world;
    Builder builder{engine, {}};

    // ---- light: a sun in the afternoon
    Entity sun = scene.create("Sun");
    Transform sunTransform;
    sunTransform.rotation = lookRotation(Vec3{0.45f, -0.62f, -0.42f});
    scene.add(sun, sunTransform);
    Light light;
    light.type = LightType::Directional;
    light.color = Vec3{1.0f, 0.94f, 0.84f};
    light.intensity = 3.6f;
    light.castsShadows = true;
    scene.add(sun, light);

    // ---- the ground. The mowed field is one flat square; the land around it is a grid
    // whose height comes from groundHeight(). Under the field the grid dips a little,
    // so that the two never fight over the same pixels.
    auto fieldGrass = assets.makeMaterial(Vec4{1, 1, 1, 1}, 0.0f, 0.95f);
    fieldGrass->baseColorTexture = grassTexture(assets, true);
    fieldGrass->uvScale = Vec2{(kFieldHalf * 2 + 2) / 12.0f, (kFieldHalf * 2 + 2) / 12.0f};     // a 12 m tile: two 6 m mowing bands
    builder.shape("Field", assets.plane(), fieldGrass, Vec3{kFieldCenter.x, 0, kFieldCenter.y},
                  Vec3{kFieldHalf * 2 + 2, 1, kFieldHalf * 2 + 2});

    auto wildGrass = assets.makeMaterial(Vec4{1, 1, 1, 1}, 0.0f, 0.97f);
    wildGrass->baseColorTexture = grassTexture(assets, false);
    MeshData land = MeshData::heightfield(kTerrainSize, kTerrainSize, 124, 124, [](float x, float z) {
        float worldX = x + kFieldCenter.x, worldZ = z + kFieldCenter.y;
        return groundHeight(worldX, worldZ) - 0.35f * (1.0f - smoothstep(0.0f, 5.0f, beyondField(worldX, worldZ)));
    }, kTerrainSize / 9.0f);
    builder.shape("Land", assets.makeMesh(land), wildGrass, Vec3{kFieldCenter.x, 0, kFieldCenter.y}, Vec3{1, 1, 1});

    // ---- gates: bands of checkered fabric. The next one to fly through glows green.
    world.gateMaterial = assets.makeMaterial(Vec4{1.0f, 0.38f, 0.06f, 1}, 0.0f, 0.75f);
    world.gateMaterial->baseColorTexture = assets.texture("Textures/checker_basecolor.png");
    world.gateMaterial->emissive = Vec3{0.20f, 0.06f, 0.01f};
    world.nextGateMaterial = assets.makeMaterial(Vec4{0.05f, 0.5f, 0.15f, 1}, 0.0f, 0.6f);
    world.nextGateMaterial->emissive = Vec3{0.08f, 1.1f, 0.25f};
    for (const Gate& gate : courseGates()) world.gateParts.push_back(buildGate(builder, gate, world.gateMaterial));

    // ---- flags beside every other gate: a pole and a tall strip of cloth
    auto pole = assets.makeMaterial(Vec4{0.8f, 0.8f, 0.82f, 1}, 1.0f, 0.4f);
    const Vec4 cloths[4] = {Vec4{0.1f, 0.3f, 0.9f, 1}, Vec4{0.95f, 0.95f, 0.95f, 1}, Vec4{0.9f, 0.12f, 0.1f, 1}, Vec4{0.98f, 0.8f, 0.1f, 1}};
    auto cloth = assets.makeMaterial(Vec4{1, 1, 1, 1}, 0.0f, 0.8f);
    cloth->isDoubleSided = true;
    int flagCount = 0;
    for (size_t i = 0; i < courseGates().size(); i += 2) {
        const Gate& gate = courseGates()[i];
        for (float side : {-1.0f, 1.0f}) {
            Vec3 foot = gate.center + gate.right() * (side * (gate.width * 0.5f + 2.2f));
            foot.y = 0;
            builder.shape("Flag pole", assets.cylinder(), pole, foot + Vec3{0, 2.3f, 0}, Vec3{0.05f, 4.6f, 0.05f});
            Entity flag = builder.box("Flag", foot + Vec3{0, 3.1f, 0} + gate.right() * 0.36f, Vec3{0.66f, 2.6f, 0.02f}, cloth, gate.rotation(), false);
            scene.get<MeshRenderer>(flag)->tint = cloths[flagCount++ % 4];
        }
    }

    // ---- the start: a rubber mat, and the pilot's tent beside it
    auto rubber = assets.makeMaterial(Vec4{0.05f, 0.05f, 0.055f, 1}, 0.0f, 0.9f);
    builder.box("Launch mat", world.start + Vec3{0, 0.01f, 0}, Vec3{1.4f, 0.02f, 1.4f}, rubber, Quat{}, false);
    auto canvasWhite = assets.makeMaterial(Vec4{0.92f, 0.92f, 0.9f, 1}, 0.0f, 0.7f);
    Vec3 tent{world.pilot.x + 1.0f, 0, world.pilot.z};
    builder.box("Tent roof", tent + Vec3{0, 2.35f, 0}, Vec3{3.2f, 0.08f, 3.2f}, canvasWhite);
    for (float x : {-1.5f, 1.5f}) {
        for (float z : {-1.5f, 1.5f}) builder.shape("Tent leg", assets.cylinder(), pole, tent + Vec3{x, 1.15f, z}, Vec3{0.05f, 2.3f, 0.05f});
    }
    auto wood = assets.makeMaterial(Vec4{0.62f, 0.45f, 0.28f, 1}, 0.0f, 0.7f);
    builder.box("Table", tent + Vec3{0.4f, 0.74f, -0.6f}, Vec3{1.6f, 0.05f, 0.7f}, wood);

    // ---- the unfinished building: columns and slabs, open on every side
    auto concrete = std::make_shared<Material>();
    concrete->baseColor = Vec4{0.8f, 0.8f, 0.8f, 1};
    concrete->metallic = 1.0f;                  // factors of 1 let the texture maps decide
    concrete->roughness = 1.0f;
    concrete->baseColorTexture = assets.texture("Textures/tiles_basecolor.png");
    concrete->normalTexture = assets.texture("Textures/tiles_normal.png", TextureFormat::RGBA8);
    concrete->metallicRoughnessTexture = assets.texture("Textures/tiles_orm.png", TextureFormat::RGBA8);
    const Vec3 corner{22.0f, 0, -74.0f};
    const float bay = 5.0f, storey = 3.4f;
    for (int floor = 0; floor < 2; floor++) {
        float base = float(floor) * storey;
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 3; j++) {
                builder.box("Column", corner + Vec3{float(i) * bay, base + storey * 0.5f - 0.125f, float(j) * bay},
                            Vec3{0.4f, storey - 0.25f, 0.4f}, concrete);
            }
        }
        // The slab above this storey. The upper one has a bay missing: a way out through the roof.
        if (floor == 0) {
            builder.box("Slab", corner + Vec3{bay * 1.5f, base + storey - 0.125f, bay}, Vec3{bay * 3 + 0.6f, 0.25f, bay * 2 + 0.6f}, concrete);
        } else {
            builder.box("Slab", corner + Vec3{bay * 1.0f, base + storey - 0.125f, bay}, Vec3{bay * 2 + 0.6f, 0.25f, bay * 2 + 0.6f}, concrete);
            builder.box("Slab", corner + Vec3{bay * 2.5f, base + storey - 0.125f, bay * 0.5f}, Vec3{bay + 0.6f, 0.25f, bay + 0.6f}, concrete);
        }
    }
    // Two walls with a window each, to dive through.
    for (int k = 0; k < 2; k++) {
        float z = k == 0 ? 0.0f : bay * 2;
        builder.box("Wall", corner + Vec3{bay * 0.5f, 0.55f, z}, Vec3{bay - 0.4f, 1.1f, 0.2f}, concrete);
        builder.box("Wall", corner + Vec3{bay * 0.5f, storey - 0.65f, z}, Vec3{bay - 0.4f, 0.8f, 0.2f}, concrete);
    }

    // ---- trees: a ring of them around the field and on the hills, and a few on the
    // field itself to fly around. A fixed seed puts them in the same places every time.
    auto bark = assets.makeMaterial(Vec4{0.26f, 0.18f, 0.11f, 1}, 0.0f, 0.9f);
    auto needles = assets.makeMaterial(Vec4{0.10f, 0.26f, 0.12f, 1}, 0.0f, 0.85f);
    auto leaves = assets.makeMaterial(Vec4{0.20f, 0.36f, 0.12f, 1}, 0.0f, 0.85f);
    std::shared_ptr<Mesh> cone = assets.makeMesh(MeshData::cone(0.5f, 1.0f, 14));
    Random random(7);
    auto plant = [&](Vec3 foot) {
        float height = random.range(7.0f, 14.0f);
        Vec4 tint{random.range(0.8f, 1.15f), random.range(0.85f, 1.1f), random.range(0.8f, 1.05f), 1};
        if (random.next() < 0.6f) {
            // A conifer: three cones, each smaller and higher than the one below.
            Entity trunk = builder.shape("Tree", assets.cylinder(), bark, foot + Vec3{0, height * 0.12f, 0},
                                         Vec3{height * 0.045f, height * 0.24f, height * 0.045f});
            scene.add(trunk, Collider::capsule(height * 0.03f, height * 0.1f));
            for (int tier = 0; tier < 3; tier++) {
                float width = height * (0.42f - 0.09f * float(tier)), tall = height * 0.42f;
                Entity part = builder.shape("Tree", cone, needles, foot + Vec3{0, height * (0.18f + 0.24f * float(tier)) + tall * 0.5f, 0},
                                            Vec3{width, tall, width}, tint);
                Collider collider = Collider::sphere(width * 0.33f);
                collider.offset = Vec3{0, -tall * 0.2f, 0};         // a cone is widest near its base
                scene.add(part, collider);
            }
        } else {
            // A broadleaf: a trunk and a round crown.
            Entity trunk = builder.shape("Tree", assets.cylinder(), bark, foot + Vec3{0, height * 0.22f, 0},
                                         Vec3{height * 0.05f, height * 0.44f, height * 0.05f});
            scene.add(trunk, Collider::capsule(height * 0.03f, height * 0.2f));
            float crown = height * 0.62f;
            Entity top = builder.shape("Tree", assets.sphere(), leaves, foot + Vec3{0, height * 0.68f, 0}, Vec3{crown, crown * 0.85f, crown}, tint);
            scene.add(top, Collider::sphere(crown * 0.42f));
        }
    };
    int planted = 0;
    while (planted < 120) {
        Vec3 spot{kFieldCenter.x + random.range(-250.0f, 250.0f), 0, kFieldCenter.y + random.range(-250.0f, 250.0f)};
        float outside = beyondField(spot.x, spot.z);
        if (outside < 4 || outside > 170) continue;
        if (outside > 60 && random.next() < 0.5f) continue;         // thinner far away
        spot.y = groundHeight(spot.x, spot.z) - 0.2f;
        plant(spot);
        planted++;
    }
    planted = 0;
    while (planted < 7) {
        Vec3 spot{kFieldCenter.x + random.range(-70.0f, 70.0f), -0.1f, kFieldCenter.y + random.range(-70.0f, 70.0f)};
        // Keep the racing line, the start and the building clear.
        if (distanceToCourse(Vec2{spot.x, spot.z}) < 11 || simd::length(Vec2{spot.x, spot.z}) < 22) continue;
        if (spot.x > corner.x - 8 && spot.z < corner.z + bay * 2 + 8) continue;
        plant(spot);
        planted++;
    }
    return world;
}

// ---------------------------------------------------------------- the drone, to look at

DroneModel DroneModel::build(Engine& engine, const Airframe& a) {
    Scene& scene = engine.scene;
    Assets& assets = engine.assets();
    DroneModel model;
    model.root = scene.create("Drone");
    scene.add(model.root, Transform{});

    auto part = [&](const char* name, const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<Material>& material, Vec3 position,
                    Vec3 scale, Quat rotation = Quat{}) {
        Entity entity = scene.create(name);
        Transform transform;
        transform.position = position;
        transform.rotation = rotation;
        transform.scale = scale;
        transform.parent = model.root;
        scene.add(entity, transform);
        scene.add(entity, MeshRenderer{mesh, material});
        model.parts.push_back(entity);
        return entity;
    };

    float s = a.armX / 0.0795f;                 // everything is sized from the 5-inch frame
    auto carbon = assets.makeMaterial(Vec4{0.03f, 0.03f, 0.035f, 1}, 0.0f, 0.38f);
    auto metal = assets.makeMaterial(Vec4{0.75f, 0.76f, 0.8f, 1}, 1.0f, 0.35f);
    auto pack = assets.makeMaterial(Vec4{0.08f, 0.08f, 0.09f, 1}, 0.0f, 0.5f);
    auto bladeMaterial = assets.makeMaterial(Vec4{1.0f, 0.42f, 0.1f, 1}, 0.0f, 0.5f);
    auto discMaterial = assets.makeMaterial(Vec4{1.0f, 0.5f, 0.2f, 0.16f}, 0.0f, 0.6f);
    discMaterial->alphaMode = AlphaMode::Blend;

    // Frame: a body plate, a top plate, and two arms that cross.
    part("Frame", assets.cube(), carbon, Vec3{0, 0, 0}, Vec3{0.05f * s, 0.006f, 0.17f * s});
    part("Frame", assets.cube(), carbon, Vec3{0, 0.03f * s, 0}, Vec3{0.042f * s, 0.004f, 0.13f * s});
    float armLength = 2.0f * std::sqrt(a.armX * a.armX + a.armZ * a.armZ) + 0.02f * s;
    float armAngle = std::atan2(a.armX, a.armZ);
    for (float side : {-1.0f, 1.0f}) {
        part("Arm", assets.cube(), carbon, Vec3{0, -0.003f, 0}, Vec3{0.02f * s, 0.006f, armLength}, axisAngle(Vec3{0, 1, 0}, side * armAngle));
    }
    part("Battery", assets.cube(), pack, Vec3{0, 0.05f * s, 0.005f * s}, Vec3{0.038f * s, 0.034f * s, 0.078f * s});
    // The camera looks up by the same angle as the view from it.
    Quat tilt = axisAngle(Vec3{1, 0, 0}, radians(a.cameraTilt));
    part("Camera", assets.cube(), pack, Vec3{0, 0.017f * s, -0.082f * s}, Vec3{0.02f * s, 0.02f * s, 0.02f * s}, tilt);

    for (int i = 0; i < 4; i++) {
        Vec3 at{(i % 2 == 0 ? 1.0f : -1.0f) * a.armX, 0, (i < 2 ? -1.0f : 1.0f) * a.armZ};
        part("Motor", assets.cylinder(), metal, at + Vec3{0, 0.012f * s, 0}, Vec3{0.028f * s, 0.022f * s, 0.028f * s});
        float span = a.propRadius * 2;
        model.blades[i] = part("Propeller", assets.cube(), bladeMaterial, at + Vec3{0, 0.027f * s, 0}, Vec3{span, 0.002f, 0.014f * s});
        model.discs[i] = part("Propeller disc", assets.cylinder(), discMaterial, at + Vec3{0, 0.027f * s, 0}, Vec3{span, 0.001f, span});
        scene.get<MeshRenderer>(model.discs[i])->castsShadows = false;
        // A whoop's propellers turn inside rings that let it bounce off things.
        if (s < 0.7f) {
            auto duct = assets.makeMaterial(Vec4{0.9f, 0.9f, 0.92f, 0.35f}, 0.0f, 0.5f);
            duct->alphaMode = AlphaMode::Blend;
            Entity ring = part("Duct", assets.cylinder(), duct, at + Vec3{0, 0.02f * s, 0}, Vec3{span + 0.008f, 0.016f, span + 0.008f});
            scene.get<MeshRenderer>(ring)->castsShadows = false;
        }
    }
    return model;
}

void DroneModel::update(Engine& engine, const Drone& drone, float dt) {
    Scene& scene = engine.scene;
    Transform* transform = scene.get<Transform>(root);
    if (!transform) return;
    transform->position = drone.position;
    transform->rotation = drone.rotation;
    for (int i = 0; i < 4; i++) {
        // At speed a propeller is a blur: show the disc. Slow, show the blades turning.
        // (They turn far slower on screen than in reality, or they would only flicker.)
        bool blurred = drone.motors[i] > 0.14f;
        float direction = (i == 0 || i == 3) ? 1.0f : -1.0f;
        bladeAngle[i] += direction * (4.0f + drone.motors[i] * 60.0f) * dt * (drone.motors[i] > 0.005f ? 1.0f : 0.0f);
        if (Transform* blade = scene.get<Transform>(blades[i])) blade->rotation = axisAngle(Vec3{0, 1, 0}, bladeAngle[i]);
        if (MeshRenderer* renderer = scene.get<MeshRenderer>(blades[i])) renderer->isVisible = !blurred;
        if (MeshRenderer* renderer = scene.get<MeshRenderer>(discs[i])) renderer->isVisible = blurred;
    }
}

void DroneModel::destroy(Engine& engine) {
    for (Entity entity : parts) engine.scene.destroy(entity);
    engine.scene.destroy(root);
    parts.clear();
}

}  // namespace fpv
