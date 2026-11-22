// The three games a new project can start from: built, written, read back and played.
// See StarterGames.hpp.
#include "StarterGames.hpp"

#include "StarterFigure.hpp"

#include "Test.hpp"

#include "Assets/MeshData.hpp"
#include "Core/Paths.hpp"
#include "Core/TextFile.hpp"
#include "Input/Input.hpp"
#include "Animation/Animator.hpp"
#include "Kit/Character.hpp"
#include "Kit/Effects.hpp"
#include "Kit/Kit.hpp"
#include "Kit/Network.hpp"
#include "Kit/Shooting.hpp"
#include "Kit/Vehicles.hpp"
#include "Kit/WhenThen.hpp"
#include "Project/Project.hpp"
#include "Script/Runtime.hpp"
#include "Water/Water.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace starter {

namespace fs = std::filesystem;

// ---------------------------------------------------------------- shapes

void Model::box(Vec3 low, Vec3 high) {
    int first = int(points.size());
    for (int k = 0; k < 8; k++) {
        // The four corners at the low z, counter-clockwise from the low x and y; then the same at the high z.
        bool right = (k & 3) == 1 || (k & 3) == 2, top = (k & 3) >= 2;
        point(Vec3{right ? high.x : low.x, top ? high.y : low.y, k < 4 ? low.z : high.z});
    }
    auto side = [&](int a, int b, int c, int d) { face({first + a, first + b, first + c, first + d}); };
    side(4, 5, 6, 7);       // +z
    side(1, 0, 3, 2);       // -z
    side(5, 1, 2, 6);       // +x
    side(0, 4, 7, 3);       // -x
    side(3, 7, 6, 2);       // +y
    side(0, 1, 5, 4);       // -y
}

void Model::loft(const std::vector<std::vector<Vec3>>& sections) {
    if (sections.size() < 2) return;
    const int around = int(sections[0].size());
    int first = int(points.size());
    for (const std::vector<Vec3>& section : sections) {
        for (const Vec3& p : section) point(p);
    }
    const int count = int(sections.size());
    for (int s = 0; s + 1 < count; s++) {
        for (int i = 0; i < around; i++) {
            int a = first + s * around + i, b = first + s * around + (i + 1) % around;
            face({a, b, b + around, a + around});
        }
    }
    // The ends: the first looks back, the last looks on.
    std::vector<int> front, back;
    for (int i = around - 1; i >= 0; i--) front.push_back(first + i);
    for (int i = 0; i < around; i++) back.push_back(first + (count - 1) * around + i);
    face(front);
    face(back);
}

void Model::ring(float radius, float tube, int around, int across) {
    int first = int(points.size());
    for (int i = 0; i < around; i++) {
        float turn = float(i) / float(around) * 2.0f * kPi;
        for (int j = 0; j < across; j++) {
            float lift = float(j) / float(across) * 2.0f * kPi;
            float out = radius + tube * std::cos(lift);
            point(Vec3{out * std::cos(turn), out * std::sin(turn), tube * std::sin(lift)});
        }
    }
    for (int i = 0; i < around; i++) {
        for (int j = 0; j < across; j++) {
            int a = first + i * across + j, b = first + ((i + 1) % around) * across + j;
            int c = first + ((i + 1) % around) * across + (j + 1) % across, d = first + i * across + (j + 1) % across;
            face({a, b, c, d});
        }
    }
}

void Model::add(const Model& other, Vec3 offset) {
    int first = int(points.size());
    for (const Vec3& p : other.points) point(p + offset);
    for (const std::vector<int>& corners : other.faces) {
        std::vector<int> moved;
        for (int corner : corners) moved.push_back(first + corner);
        face(moved);
    }
}

float Model::volume() const {
    float sum = 0;
    for (const std::vector<int>& corners : faces) {
        for (size_t k = 1; k + 1 < corners.size(); k++) {
            sum += simd::dot(points[size_t(corners[0])], simd::cross(points[size_t(corners[k])], points[size_t(corners[k + 1])]));
        }
    }
    return sum / 6.0f;
}

std::string Model::obj() const {
    std::string text = "# Made by the engine's tests (Tests/StarterGames.cpp): flat faces, each with a normal of its own.\n";
    char line[160];
    for (const Vec3& p : points) {
        std::snprintf(line, sizeof(line), "v %.4f %.4f %.4f\n", double(p.x), double(p.y), double(p.z));
        text += line;
    }
    std::string facesText;
    int normals = 0;
    for (const std::vector<int>& corners : faces) {
        // The face's normal, from all its corners (a face may start with two that nearly coincide).
        Vec3 normal{0, 0, 0};
        for (size_t k = 0; k < corners.size(); k++) {
            Vec3 a = points[size_t(corners[k])], b = points[size_t(corners[(k + 1) % corners.size()])];
            normal += Vec3{(a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y)};
        }
        if (simd::length(normal) < 1e-9f) continue;            // no area: not a face
        normal = simd::normalize(normal);
        std::snprintf(line, sizeof(line), "vn %.4f %.4f %.4f\n", double(normal.x), double(normal.y), double(normal.z));
        text += line;
        normals++;
        facesText += "f";
        for (int corner : corners) facesText += " " + std::to_string(corner + 1) + "//" + std::to_string(normals);
        facesText += "\n";
    }
    return text + facesText;
}

// ---------------------------------------------------------------- putting a game together

std::shared_ptr<Material> Game::material(const std::string& name, Vec3 color, float roughness, float metallic, Vec3 emissive) {
    auto made = std::make_shared<Material>();
    made->baseColor = Vec4{color.x, color.y, color.z, 1};
    made->roughness = roughness;
    made->metallic = metallic;
    made->emissive = emissive;
    made->assetName = "Materials/" + name + ".material";
    materials.push_back(NamedMaterial{name, made});
    return made;
}

Entity Game::add(const std::string& name, Entity parent, Vec3 position, Quat rotation, Vec3 scale) {
    Entity entity = world.scene.create(name);
    Transform place;
    place.position = position;
    place.rotation = rotation;
    place.scale = scale;
    place.parent = parent;
    world.scene.add(entity, place);
    return entity;
}

Entity Game::shape(const std::string& name, Entity parent, const std::string& mesh, const std::shared_ptr<Material>& look, Vec3 position, Vec3 scale, Quat rotation,
                   bool castsShadows) {
    Entity entity = add(name, parent, position, rotation, scale);
    MeshRenderer renderer;
    renderer.meshName = mesh;
    renderer.material = look;
    renderer.castsShadows = castsShadows;
    world.scene.add(entity, renderer);
    return entity;
}

std::string Game::model(const std::string& name, const Model& made) {
    std::string file = "Models/" + name + ".obj";
    models[file] = made.obj();
    return file;
}

namespace {

/// The entity called `name` directly under `parent`; invalid if there is none.
Entity childCalled(Scene& scene, Entity parent, const char* name) {
    Entity found;
    scene.each<Transform, Name>([&](Entity entity, Transform& place, Name& named) {
        if (place.parent == parent && named.value == name) found = entity;
    });
    return found;
}

/// Makes a scaled cube solid: a box collider is measured in meters whatever its entity's
/// scale, so it is given half the scale. (Its parents must not be scaled.)
void makeSolid(Scene& scene, Entity cube) {
    const Transform* place = scene.get<Transform>(cube);
    Collider solid;
    if (place) solid.halfExtents = place->scale * 0.5f;
    scene.add(cube, solid);
}

/// A rotation about the vertical: 0 looks along -z, 90 along -x.
Quat heading(float degrees) { return Quat(radians(degrees), Vec3{0, 1, 0}); }

/// Something between 0 and 1 that is the same every time for the same number: where a tree stands.
float chance(int seed) {
    uint32_t x = uint32_t(seed) * 747796405u + 2891336453u;
    x = ((x >> ((x >> 28) + 4)) ^ x) * 277803737u;
    return float((x >> 22) ^ x) / 4294967295.0f;
}

/// A mound: round from above, domed, with `sides` flat sides and a little unevenness (none with `rough` 0).
Model mound(float radius, float height, int sides, int levels, float rough, int seed) {
    std::vector<std::vector<Vec3>> sections;
    // From the top down, so that each loop is counter-clockwise seen from below, looking up: the skin faces out.
    for (int level = levels; level >= 0; level--) {
        float t = float(level) / float(levels);                // 0 at the foot, 1 at the top
        float out = radius * std::cos(t * kPi * 0.5f) + (level == levels ? radius * 0.04f : 0.0f);
        std::vector<Vec3> loop;
        for (int i = 0; i < sides; i++) {
            float turn = -float(i) / float(sides) * 2.0f * kPi;
            float uneven = 1.0f + rough * (chance(seed + level * 31 + i * 7) - 0.5f);
            loop.push_back(Vec3{out * uneven * std::cos(turn), height * std::sin(t * kPi * 0.5f) * (level == 0 ? 0.0f : uneven), out * uneven * std::sin(turn)});
        }
        sections.push_back(loop);
    }
    Model made;
    made.loft(sections);
    return made;
}

/// A pine: a few cones one above the other, as flat sides.
Model pine(float height, float radius) {
    Model made;
    for (int tier = 0; tier < 3; tier++) {
        float low = height * (0.12f + 0.26f * float(tier)), high = height * (0.56f + 0.22f * float(tier));
        float wide = radius * (1.0f - 0.24f * float(tier));
        std::vector<std::vector<Vec3>> sections;
        for (int end = 1; end >= 0; end--) {
            std::vector<Vec3> loop;
            for (int i = 0; i < 7; i++) {
                float turn = -float(i) / 7.0f * 2.0f * kPi + float(tier) * 0.4f;
                float out = end == 1 ? wide * 0.03f : wide;
                loop.push_back(Vec3{out * std::cos(turn), end == 1 ? high : low, out * std::sin(turn)});
            }
            sections.push_back(loop);
        }
        made.loft(sections);
    }
    return made;
}

/// The light of a clear day, a little after noon, and a sky with a few clouds in it.
Entity addDaylight(Game& game, float sunPitch, float sunYaw, float clouds) {
    Entity sun = game.add("Sun", Entity{}, Vec3{0, 60, 0}, eulerAngles(radians(sunPitch), radians(sunYaw)));
    Light light;
    light.type = LightType::Directional;
    light.color = Vec3{1.0f, 0.95f, 0.86f};
    light.intensity = 4.2f;
    light.castsShadows = true;
    game.world.scene.add(sun, light);
    RenderSettings& settings = game.world.sceneSettings();
    settings.sky.zenithColor = Vec3{0.06f, 0.2f, 0.52f};
    settings.sky.horizonColor = Vec3{0.5f, 0.66f, 0.84f};
    settings.sky.groundColor = Vec3{0.16f, 0.17f, 0.16f};
    settings.sky.cloudCoverage = clouds;
    settings.sky.cloudsHaveDepth = true;
    settings.fogColor = Vec3{0.62f, 0.72f, 0.84f};
    settings.cascades.shadowDistance = 160;
    settings.ambientOcclusionEnabled = true;
    return sun;
}

/// The camera that stays behind what the player drives.
Entity addChaseCamera(Game& game, const std::string& target, Vec3 from, Vec3 offset, float smoothness, float fieldOfView, float farPlane) {
    Entity camera = game.add("Camera", Entity{}, from);
    Camera lens;
    lens.fovY = radians(fieldOfView);
    lens.nearPlane = 0.25f;
    lens.farPlane = farPlane;
    game.world.scene.add(camera, lens);
    kit::Follow follow;
    follow.target = target;
    follow.offset = offset;
    follow.smoothness = smoothness;
    follow.looksAtTarget = true;
    follow.turnsWithTarget = true;
    game.world.scene.add(camera, follow);
    return camera;
}

/// What is on the screen in all three: how many of the things to collect are collected, what
/// the keys do, a dial, a button that starts over, and the message at the end.
Entity addInterface(Game& game, const std::string& counter, const std::string& counterLabel, int target, const std::string& won,
                    const std::vector<std::string>& help, const std::string& dialValue, float dialMost, float dialEvery, const std::string& dialLabel) {
    Scene& scene = game.world.scene;
    Entity interface = game.add("Interface");
    auto piece = [&](const std::string& name) { return game.add(name, interface); };
    const Vec4 glass{0.04f, 0.045f, 0.055f, 0.62f}, rim{1, 1, 1, 0.16f};

    // Top middle: the count.
    kit::UIPanel panel;
    panel.anchor = Vec2{0.5f, 0};
    panel.offset = Vec2{0, 40};
    panel.size = Vec2{230, 52};
    panel.color = glass;
    panel.border = 1.5f;
    panel.borderColor = rim;
    scene.add(piece("Count Panel"), panel);
    kit::HudText label;
    label.text = counterLabel;
    label.anchor = Vec2{0.5f, 0};
    label.offset = Vec2{-52, 30};
    label.size = 13;
    label.color = Vec4{1, 1, 1, 0.6f};
    scene.add(piece("Count Label"), label);
    kit::HudText count;
    count.text = "{" + counter + "} / " + std::to_string(target);
    count.anchor = Vec2{0.5f, 0};
    count.offset = Vec2{46, 24};
    count.size = 26;
    count.color = Vec4{1.0f, 0.62f, 0.28f, 1};
    scene.add(piece("Count"), count);

    // Bottom middle: the dial, in a game that has something to show on one.
    if (!dialValue.empty()) {
        kit::UIGauge dial;
        dial.value = dialValue;
        dial.most = dialMost;
        dial.numbersEvery = dialEvery;
        dial.label = dialLabel;
        dial.anchor = Vec2{0.5f, 1};
        dial.offset = Vec2{0, -98};
        dial.radius = 78;
        dial.thickness = 7;
        scene.add(piece("Dial"), dial);
    }

    // Bottom left: what the keys do.
    for (size_t i = 0; i < help.size(); i++) {
        kit::HudText line;
        line.text = help[i];
        line.anchor = Vec2{0, 1};
        line.offset = Vec2{20, -24 - 17 * float(help.size() - 1 - i)};
        line.size = 12;
        line.color = Vec4{1, 1, 1, 0.62f};
        // Written from its left edge: the anchor is the text's middle, so it is found from its length.
        line.offset.x += float(help[i].size()) * 3.1f;
        scene.add(piece("Help " + std::to_string(i + 1)), line);
    }

    // Top right: over again.
    Entity again = piece("Restart");
    kit::UIButton button;
    button.label = "Restart";
    button.anchor = Vec2{1, 0};
    button.offset = Vec2{-70, 40};
    button.size = Vec2{104, 36};
    button.color = Vec4{1, 1, 1, 0.16f};
    scene.add(again, button);
    kit::WhenThen clicked;
    clicked.when = "It is clicked";
    clicked.once = false;
    clicked.action1 = "Restart scene";
    scene.add(again, clicked);

    // And the end: everything collected.
    kit::Goal goal;
    goal.counter = counter;
    goal.target = float(target);
    goal.message = won;
    goal.delay = 4.0f;
    scene.add(piece("Goal"), goal);
    return interface;
}

/// A thing to collect: it glows, turns, and is gone when `collector` touches it.
Entity addCollectible(Game& game, const std::string& name, Entity parent, const std::string& mesh, const std::shared_ptr<Material>& look, Vec3 position, Quat rotation, Vec3 scale,
                      float reach, const std::string& collector, const std::string& counter, bool turns) {
    Scene& scene = game.world.scene;
    Entity entity = game.shape(name, parent, mesh, look, position, scale, rotation, false);
    Collider trigger = Collider::sphere(reach);              // in meters, whatever its scale
    trigger.isTrigger = true;
    scene.add(entity, trigger);
    kit::Collectible collectible;
    collectible.collector = collector;
    collectible.counter = counter;
    scene.add(entity, collectible);
    if (turns) {
        kit::Spin spin;
        spin.degreesPerSecond = 70;
        scene.add(entity, spin);
    }
    return entity;
}

void addTrees(Game& game, Entity parent, const std::string& crown, const std::shared_ptr<Material>& leaves, const std::shared_ptr<Material>& bark, int count, int seed,
              const std::function<bool(float, float, Vec3&)>& placeAt) {
    int made = 0;
    for (int attempt = 0; attempt < count * 12 && made < count; attempt++) {
        Vec3 at{0, 0, 0};
        if (!placeAt(chance(seed + attempt * 2), chance(seed + attempt * 2 + 1), at)) continue;
        float size = 0.8f + 0.7f * chance(seed + 5000 + attempt);
        Entity tree = game.add("Tree " + std::to_string(++made), parent, at, heading(360.0f * chance(seed + 9000 + attempt)), Vec3{size, size, size});
        game.shape("Trunk", tree, "cylinder", bark, Vec3{0, 0.7f, 0}, Vec3{0.34f, 1.4f, 0.34f});
        game.shape("Crown", tree, crown, leaves);
    }
}

// ---------------------------------------------------------------- the car game

void buildCarGame(Game& game) {
    game.folder = "Car Game";
    game.description = "A car, a circuit through a park and eight stars to collect. Drive with W A S D, drift with Space. No code: every part is in the Inspector.";
    Scene& scene = game.world.scene;
    addDaylight(game, -46, 38, 0.38f);
    game.world.sceneSettings().fogDensity = 0.0011f;

    auto grass = game.material("Grass", Vec3{0.2f, 0.42f, 0.14f}, 0.95f);
    auto asphalt = game.material("Asphalt", Vec3{0.075f, 0.078f, 0.085f}, 0.82f);
    auto paintWhite = game.material("Road Paint", Vec3{0.86f, 0.86f, 0.82f}, 0.7f);
    auto curbRed = game.material("Curb", Vec3{0.72f, 0.08f, 0.06f}, 0.7f);
    auto carPaint = game.material("Car Paint", Vec3{0.78f, 0.07f, 0.05f}, 0.32f, 0.1f);
    auto glass = game.material("Glass", Vec3{0.03f, 0.05f, 0.07f}, 0.08f, 0.6f);
    auto rubber = game.material("Rubber", Vec3{0.02f, 0.02f, 0.022f}, 0.9f);
    auto steel = game.material("Steel", Vec3{0.62f, 0.64f, 0.68f}, 0.35f, 1.0f);
    auto lamps = game.material("Head Lamps", Vec3{1, 1, 0.92f}, 0.3f, 0.0f, Vec3{2.2f, 2.1f, 1.8f});
    auto tailLamps = game.material("Tail Lamps", Vec3{0.6f, 0.02f, 0.02f}, 0.3f, 0.0f, Vec3{1.6f, 0.06f, 0.04f});
    auto gold = game.material("Star", Vec3{1.0f, 0.72f, 0.12f}, 0.25f, 0.8f, Vec3{2.6f, 1.5f, 0.2f});
    auto wood = game.material("Crate", Vec3{0.55f, 0.36f, 0.17f}, 0.85f);
    auto leaves = game.material("Leaves", Vec3{0.08f, 0.3f, 0.1f}, 0.9f);
    auto bark = game.material("Bark", Vec3{0.24f, 0.15f, 0.09f}, 0.9f);
    auto concrete = game.material("Concrete", Vec3{0.56f, 0.56f, 0.54f}, 0.85f);
    auto cone = game.material("Cone", Vec3{0.95f, 0.36f, 0.05f}, 0.6f);
    auto hill = game.material("Hills", Vec3{0.17f, 0.34f, 0.15f}, 0.95f);

    // ---- the ground: one wide floor to drive on, and hills at its edge to stop the eye
    Entity ground = game.shape("Ground", Entity{}, "plane", grass, Vec3{0, 0, 0}, Vec3{900, 1, 900}, Quat{}, false);
    Collider floor;
    floor.halfExtents = Vec3{450, 0.5f, 450};
    floor.offset = Vec3{0, -0.5f, 0};
    scene.add(ground, floor);                                   // its top is the ground: a box a meter thick under it
    std::string hills = game.model("Hills", mound(1.0f, 1.0f, 11, 4, 0.5f, 7));
    Entity scenery = game.add("Scenery");
    for (int i = 0; i < 9; i++) {
        float turn = float(i) / 9.0f * 2.0f * kPi + 0.3f, far = 330.0f + 70.0f * chance(40 + i);
        float wide = 110.0f + 90.0f * chance(60 + i), tall = 26.0f + 44.0f * chance(80 + i);
        game.shape("Hill " + std::to_string(i + 1), scenery, hills, hill, Vec3{far * std::cos(turn), -1.0f, far * std::sin(turn)}, Vec3{wide, tall, wide}, heading(40.0f * float(i)), false);
    }

    // ---- the circuit: a ribbon of asphalt round a bent oval, a line down its middle, curbs on its bends
    const int steps = 96;
    const float roadHalf = 5.5f;
    auto center = [](float t) {
        float turn = t * 2.0f * kPi;
        float stretch = 1.0f + 0.14f * std::sin(3.0f * turn);
        return Vec3{88.0f * std::cos(turn) * stretch, 0, 56.0f * std::sin(turn) * stretch};
    };
    auto across = [&](float t) {
        Vec3 along = simd::normalize(center(t + 0.002f) - center(t - 0.002f));
        return Vec3{along.z, 0, -along.x};                      // to the right of the way round
    };
    Model road, line, curbs;
    for (int i = 0; i < steps; i++) {
        float t0 = float(i) / float(steps), t1 = float(i + 1) / float(steps);
        Vec3 a = center(t0), b = center(t1), sideA = across(t0), sideB = across(t1);
        // Laid a finger's width over the grass, its faces up: counter-clockwise seen from above.
        auto strip = [](Model& into, Vec3 a0, Vec3 a1, Vec3 b0, Vec3 b1, float height) {
            int first = into.point(Vec3{a0.x, height, a0.z});
            into.point(Vec3{a1.x, height, a1.z});
            into.point(Vec3{b1.x, height, b1.z});
            into.point(Vec3{b0.x, height, b0.z});
            Vec3 normal = simd::cross(into.points[size_t(first + 1)] - into.points[size_t(first)], into.points[size_t(first + 2)] - into.points[size_t(first)]);
            if (normal.y > 0) into.face({first, first + 1, first + 2, first + 3});
            else into.face({first + 3, first + 2, first + 1, first});
        };
        strip(road, a - sideA * roadHalf, a + sideA * roadHalf, b - sideB * roadHalf, b + sideB * roadHalf, 0.02f);
        if (i % 2 == 0) strip(line, a - sideA * 0.09f, a + sideA * 0.09f, b - sideB * 0.09f, b + sideB * 0.09f, 0.035f);
        // Where it bends: a curb along the inside and the outside, in red.
        Vec3 before = center(t0 - 0.01f), after = center(t0 + 0.01f);
        float bend = simd::length(simd::cross(simd::normalize(a - before), simd::normalize(after - a)));
        if (bend > 0.045f && i % 2 == 1) {
            strip(curbs, a - sideA * (roadHalf + 0.9f), a - sideA * roadHalf, b - sideB * (roadHalf + 0.9f), b - sideB * roadHalf, 0.04f);
            strip(curbs, a + sideA * roadHalf, a + sideA * (roadHalf + 0.9f), b + sideB * roadHalf, b + sideB * (roadHalf + 0.9f), 0.04f);
        }
    }
    Entity circuit = game.add("Circuit");
    game.shape("Road", circuit, game.model("Road", road), asphalt, Vec3{0, 0, 0}, Vec3{1, 1, 1}, Quat{}, false);
    game.shape("Center Line", circuit, game.model("Center Line", line), paintWhite, Vec3{0, 0, 0}, Vec3{1, 1, 1}, Quat{}, false);
    game.shape("Curbs", circuit, game.model("Curbs", curbs), curbRed, Vec3{0, 0, 0}, Vec3{1, 1, 1}, Quat{}, false);

    // ---- where it starts: on the straight, under an arch
    const float startAt = 0.25f;                                // a quarter of the way round: the far straight
    Vec3 start = center(startAt);
    Vec3 forward = simd::normalize(center(startAt + 0.003f) - center(startAt));
    Quat startFacing = lookRotation(forward);
    Vec3 startSide = across(startAt);
    Entity arch = game.add("Start Arch", circuit, start + forward * 9.0f, startFacing);
    for (int side = -1; side <= 1; side += 2) {
        Entity post = game.shape(side < 0 ? "Post L" : "Post R", arch, "cube", concrete, Vec3{float(side) * (roadHalf + 1.6f), 2.6f, 0}, Vec3{0.5f, 5.2f, 0.5f});
        makeSolid(scene, post);
    }
    game.shape("Beam", arch, "cube", carPaint, Vec3{0, 5.5f, 0}, Vec3{(roadHalf + 1.9f) * 2.0f, 0.7f, 0.6f});
    (void)startSide;

    // ---- the stars: eight, hovering over the road all the way round
    std::string star = game.model("Star", [] {
        // Two pyramids base to base, with eight sides: it catches the light as it turns.
        Model made;
        std::vector<std::vector<Vec3>> sections;
        for (int level = 0; level < 3; level++) {
            std::vector<Vec3> loop;
            float out = level == 1 ? 0.5f : 0.02f, height = float(level - 1) * 0.62f;
            for (int i = 0; i < 8; i++) {
                float turn = float(i) / 8.0f * 2.0f * kPi;
                loop.push_back(Vec3{out * std::cos(turn), height, -out * std::sin(turn)});
            }
            sections.push_back(loop);
        }
        made.loft(sections);
        return made;
    }());
    Entity stars = game.add("Stars");
    for (int i = 0; i < 8; i++) {
        float t = startAt + (float(i) + 0.6f) / 8.0f;
        Vec3 at = center(t) + across(t) * (i % 2 == 0 ? 2.2f : -2.2f);
        addCollectible(game, "Star " + std::to_string(i + 1), stars, star, gold, Vec3{at.x, 1.25f, at.z}, Quat{}, Vec3{1.5f, 1.5f, 1.5f}, 2.6f, "Car", "stars", true);
    }

    // ---- the infield: a yard to play in, with a ramp, a wall of crates and a row of cones
    Entity yard = game.add("Yard");
    Entity pad = game.shape("Pad", yard, "cube", asphalt, Vec3{0, -0.24f, 0}, Vec3{64, 0.56f, 44}, Quat{}, false);
    (void)pad;
    for (int side = -1; side <= 1; side += 2) {
        // Up one side and down the other: 11 degrees, which a car takes at speed and lands.
        Entity ramp = game.shape(side < 0 ? "Ramp Up" : "Ramp Down", yard, "cube", concrete, Vec3{-14.0f + float(side) * 3.9f, 0.42f, 6.0f},
                                 Vec3{8.0f, 0.3f, 6.0f}, Quat(radians(float(side) * 11.0f), Vec3{0, 0, 1}));
        makeSolid(scene, ramp);
    }
    // What physics moves is placed in the world's own measure: its group stays at the origin.
    Entity crates = game.add("Crates", yard);
    const Vec3 wallAt{16, 0, -6};
    for (int row = 0; row < 4; row++) {
        for (int column = 0; column < 5 - row; column++) {
            Entity crate = game.shape("Crate", crates, "cube", wood, wallAt + Vec3{(float(column) - float(4 - row) * 0.5f) * 1.05f, 0.5f + float(row) * 1.0f, 0}, Vec3{1, 1, 1});
            Collider solid;
            scene.add(crate, solid);
            RigidBody body;
            body.mass = 14;
            body.rotates = true;
            body.friction = 0.7f;
            scene.add(crate, body);
        }
    }
    Entity cones = game.add("Cones", yard);
    const Vec3 conesAt{-4, 0, -12};
    std::string coneShape = game.model("Cone", [] {
        Model made;
        made.box(Vec3{-0.26f, 0, -0.26f}, Vec3{0.26f, 0.05f, 0.26f});
        std::vector<std::vector<Vec3>> sections;
        for (int end = 1; end >= 0; end--) {
            std::vector<Vec3> loop;
            for (int i = 0; i < 8; i++) {
                float turn = -float(i) / 8.0f * 2.0f * kPi, out = end == 1 ? 0.035f : 0.19f;
                loop.push_back(Vec3{out * std::cos(turn), end == 1 ? 0.7f : 0.05f, out * std::sin(turn)});
            }
            sections.push_back(loop);
        }
        made.loft(sections);
        return made;
    }());
    for (int i = 0; i < 7; i++) {
        Entity one = game.shape("Cone", cones, coneShape, cone, conesAt + Vec3{float(i) * 5.0f, 0, i % 2 == 0 ? 0.0f : 2.4f});
        Collider solid;
        solid.halfExtents = Vec3{0.22f, 0.35f, 0.22f};
        solid.offset = Vec3{0, 0.35f, 0};
        scene.add(one, solid);
        RigidBody body;
        body.mass = 3;
        body.rotates = true;
        scene.add(one, body);
    }

    // ---- trees: clear of the road and of the yard
    std::string crown = game.model("Pine", pine(6.5f, 2.0f));
    Entity trees = game.add("Trees");
    addTrees(game, trees, crown, leaves, bark, 70, 100, [&](float u, float v, Vec3& at) {
        at = Vec3{(u - 0.5f) * 420.0f, 0, (v - 0.5f) * 320.0f};
        if (std::abs(at.x) < 40 && std::abs(at.z) < 30) return false;
        for (int i = 0; i < steps; i += 2) {
            if (simd::distance(center(float(i) / float(steps)), at) < roadHalf + 7.0f) return false;
        }
        return true;
    });

    // ---- the car: the kit's own parts, and a body to look at
    Entity car = kit::makeCar(game.world, start + Vec3{0, 0.62f, 0}, "i4", "rear");
    if (Transform* place = scene.get<Transform>(car)) place->rotation = startFacing;
    if (kit::VehicleBody* body = scene.get<kit::VehicleBody>(car)) {
        body->mass = 1150;
        body->size = Vec3{1.75f, 1.2f, 4.2f};
        // Forgiving: it may be thrown into a slide, and is caught before it spins.
        body->stability = 0.8f;
        body->driftAngle = 18;
        body->slideGrip = 0.8f;
    }
    scene.add(car, Tag{"Player", 0});
    // Solid to what it runs into (the crates fly), and what the stars' triggers see.
    Collider shell;
    shell.halfExtents = Vec3{0.84f, 0.42f, 2.05f};
    shell.offset = Vec3{0, 0.12f, 0};
    scene.add(car, shell);
    game.sounds = {"engine_i4_1000.wav", "engine_i4_2600.wav", "engine_i4_4400.wav", "engine_i4_6200.wav", "skid.wav", "wind.wav"};

    Model body;
    {
        // Cross-sections from the nose (-z) to the tail: the floor, the shoulders, all at car scale.
        struct Cut { float z, halfFloor, floorY, halfShoulder, shoulderY; };
        const Cut cuts[] = {{-2.12f, 0.6f, -0.2f, 0.62f, 0.02f}, {-1.95f, 0.78f, -0.3f, 0.8f, 0.14f}, {-1.3f, 0.85f, -0.32f, 0.86f, 0.24f}, {-0.75f, 0.86f, -0.32f, 0.87f, 0.3f},
                            {0.4f, 0.86f, -0.32f, 0.87f, 0.32f},   {1.45f, 0.85f, -0.32f, 0.86f, 0.34f},  {2.0f, 0.8f, -0.28f, 0.82f, 0.32f},   {2.12f, 0.7f, -0.2f, 0.72f, 0.24f}};
        std::vector<std::vector<Vec3>> sections;
        for (const Cut& cut : cuts) {
            sections.push_back({Vec3{-cut.halfFloor, cut.floorY, cut.z}, Vec3{cut.halfFloor, cut.floorY, cut.z}, Vec3{cut.halfShoulder, cut.shoulderY, cut.z},
                                Vec3{-cut.halfShoulder, cut.shoulderY, cut.z}});
        }
        body.loft(sections);
        body.box(Vec3{-0.62f, 0.8f, -0.24f}, Vec3{0.62f, 0.84f, 0.8f});       // the roof
    }
    Model cabin;
    {
        // The glass between the shoulders and the roof: wide below, narrow above.
        std::vector<std::vector<Vec3>> sections;
        sections.push_back({Vec3{-0.8f, 0.3f, -0.78f}, Vec3{0.8f, 0.3f, -0.78f}, Vec3{0.8f, 0.31f, -0.78f}, Vec3{-0.8f, 0.31f, -0.78f}});
        sections.push_back({Vec3{-0.8f, 0.3f, -0.2f}, Vec3{0.8f, 0.3f, -0.2f}, Vec3{0.61f, 0.8f, -0.22f}, Vec3{-0.61f, 0.8f, -0.22f}});
        sections.push_back({Vec3{-0.8f, 0.3f, 0.78f}, Vec3{0.8f, 0.3f, 0.78f}, Vec3{0.61f, 0.8f, 0.78f}, Vec3{-0.61f, 0.8f, 0.78f}});
        sections.push_back({Vec3{-0.8f, 0.33f, 1.48f}, Vec3{0.8f, 0.33f, 1.48f}, Vec3{0.8f, 0.34f, 1.48f}, Vec3{-0.8f, 0.34f, 1.48f}});
        cabin.loft(sections);
    }
    Entity look = game.add("Body", car);
    game.shape("Paint", look, game.model("Car Body", body), carPaint);
    game.shape("Windows", look, game.model("Car Cabin", cabin), glass);
    for (int side = -1; side <= 1; side += 2) {
        game.shape(side < 0 ? "Head Lamp L" : "Head Lamp R", look, "cube", lamps, Vec3{float(side) * 0.56f, 0.05f, -2.09f}, Vec3{0.3f, 0.12f, 0.08f}, Quat{}, false);
        game.shape(side < 0 ? "Tail Lamp L" : "Tail Lamp R", look, "cube", tailLamps, Vec3{float(side) * 0.56f, 0.2f, 2.11f}, Vec3{0.34f, 0.1f, 0.06f}, Quat{}, false);
    }
    // Each wheel's model under its "Spin", which the wheel turns: a cylinder lies along y, an axle along x.
    scene.each<kit::Wheel, Transform>([&](Entity wheel, kit::Wheel& part, Transform&) {
        Entity spin = childCalled(scene, wheel, "Spin");
        if (!spin.isValid()) return;
        float across = 0.24f, round = part.radius * 2.0f;
        game.shape("Tire", spin, "cylinder", rubber, Vec3{0, 0, 0}, Vec3{round, across, round}, Quat(radians(90.0f), Vec3{0, 0, 1}));
        game.shape("Rim", spin, "cylinder", steel, Vec3{0, 0, 0}, Vec3{round * 0.58f, across * 1.04f, round * 0.58f}, Quat(radians(90.0f), Vec3{0, 0, 1}));
    });

    addChaseCamera(game, "Car", start - forward * 7.0f + Vec3{0, 2.6f, 0}, Vec3{0, 1.9f, 5.8f}, 6.0f, 62.0f, 1500.0f);
    addInterface(game, "stars", "STARS", 8, "Every star! Now try the ramp.",
                 {"W  S    gas and brake, reverse", "A  D    steer", "Space    handbrake: drift"}, "speed", 200, 40, "km/h");
}

// ---------------------------------------------------------------- the plane game

void buildPlaneGame(Game& game) {
    game.folder = "Plane Game";
    game.description = "A light aeroplane, a runway and eight rings in the sky. Hold Shift for power, pull back with S at 110 km/h, and fly through them all. No code: every part is in the Inspector.";
    Scene& scene = game.world.scene;
    addDaylight(game, -52, -30, 0.45f);
    RenderSettings& settings = game.world.sceneSettings();
    settings.fogDensity = 0.00028f;
    settings.sky.cloudHeight = 900;
    settings.sky.cloudThickness = 900;
    settings.cascades.shadowDistance = 220;

    auto grass = game.material("Grass", Vec3{0.22f, 0.4f, 0.15f}, 0.95f);
    auto fields = game.material("Fields", Vec3{0.42f, 0.44f, 0.16f}, 0.95f);
    auto asphalt = game.material("Asphalt", Vec3{0.08f, 0.083f, 0.09f}, 0.85f);
    auto paintWhite = game.material("Runway Paint", Vec3{0.88f, 0.88f, 0.84f}, 0.7f);
    auto skin = game.material("Aeroplane", Vec3{0.92f, 0.92f, 0.9f}, 0.35f, 0.1f);
    auto trim = game.material("Aeroplane Trim", Vec3{0.82f, 0.1f, 0.06f}, 0.35f, 0.1f);
    auto glass = game.material("Glass", Vec3{0.03f, 0.05f, 0.07f}, 0.08f, 0.6f);
    auto rubber = game.material("Rubber", Vec3{0.02f, 0.02f, 0.022f}, 0.9f);
    auto steel = game.material("Steel", Vec3{0.62f, 0.64f, 0.68f}, 0.35f, 1.0f);
    auto ringLook = game.material("Ring", Vec3{1.0f, 0.45f, 0.08f}, 0.3f, 0.2f, Vec3{3.0f, 1.1f, 0.15f});
    auto leaves = game.material("Leaves", Vec3{0.08f, 0.3f, 0.1f}, 0.9f);
    auto bark = game.material("Bark", Vec3{0.24f, 0.15f, 0.09f}, 0.9f);
    auto concrete = game.material("Concrete", Vec3{0.6f, 0.6f, 0.58f}, 0.85f);
    auto roof = game.material("Hangar Roof", Vec3{0.2f, 0.32f, 0.5f}, 0.5f, 0.6f);
    auto rock = game.material("Mountains", Vec3{0.34f, 0.36f, 0.36f}, 0.95f);

    // ---- the land: flat for six kilometers, with mountains round it
    Entity ground = game.shape("Ground", Entity{}, "plane", grass, Vec3{0, 0, 0}, Vec3{9000, 1, 9000}, Quat{}, false);
    Collider floor;
    floor.halfExtents = Vec3{4500, 0.5f, 4500};
    floor.offset = Vec3{0, -0.5f, 0};
    scene.add(ground, floor);
    Entity scenery = game.add("Scenery");
    std::string mountain = game.model("Mountain", mound(1.0f, 1.0f, 9, 5, 0.6f, 3));
    for (int i = 0; i < 12; i++) {
        float turn = float(i) / 12.0f * 2.0f * kPi + 0.2f, far = 2600.0f + 700.0f * chance(140 + i);
        float wide = 700.0f + 500.0f * chance(160 + i), tall = 260.0f + 420.0f * chance(180 + i);
        game.shape("Mountain " + std::to_string(i + 1), scenery, mountain, rock, Vec3{far * std::cos(turn) + 400.0f, -4.0f, far * std::sin(turn) - 500.0f}, Vec3{wide, tall, wide},
                   heading(50.0f * float(i)), false);
    }
    // Fields, to see the ground go by from above: squares of another green laid on it.
    for (int i = 0; i < 26; i++) {
        float x = (chance(300 + i) - 0.5f) * 3200.0f + 400.0f, z = (chance(330 + i) - 0.5f) * 3200.0f - 500.0f;
        if (std::abs(x) < 60 && z < 520 && z > -520) continue;
        game.shape("Field " + std::to_string(i + 1), scenery, "plane", i % 2 == 0 ? fields : leaves, Vec3{x, 0.03f + 0.01f * float(i % 3), z},
                   Vec3{120.0f + 160.0f * chance(360 + i), 1, 90.0f + 140.0f * chance(390 + i)}, heading(90.0f * chance(420 + i)), false);
    }

    // ---- the airfield: a runway that runs north (toward -z), a hangar, a tower
    Entity airfield = game.add("Airfield");
    const float runwayLength = 900, runwayHalf = 10;
    game.shape("Runway", airfield, "cube", asphalt, Vec3{0, -0.2f, 0}, Vec3{runwayHalf * 2.0f, 0.5f, runwayLength}, Quat{}, false);
    Model marks;
    for (int i = 0; i < 26; i++) {
        float z = -runwayLength * 0.5f + 40.0f + float(i) * 32.0f;
        marks.box(Vec3{-0.3f, 0.05f, z}, Vec3{0.3f, 0.065f, z + 16.0f});
    }
    for (int end = -1; end <= 1; end += 2) {
        for (int bar = -3; bar <= 3; bar++) {
            if (bar == 0) continue;
            float z = float(end) * (runwayLength * 0.5f - 22.0f);
            marks.box(Vec3{float(bar) * 2.4f - 0.7f, 0.05f, z - 10.0f}, Vec3{float(bar) * 2.4f + 0.7f, 0.065f, z + 10.0f});
        }
    }
    game.shape("Runway Marks", airfield, game.model("Runway Marks", marks), paintWhite, Vec3{0, 0, 0}, Vec3{1, 1, 1}, Quat{}, false);
    Entity hangar = game.add("Hangar", airfield, Vec3{46, 0, 330});
    Entity walls = game.shape("Walls", hangar, "cube", concrete, Vec3{0, 4, 0}, Vec3{26, 8, 30});
    makeSolid(scene, walls);
    game.shape("Roof", hangar, "cube", roof, Vec3{0, 8.6f, 0}, Vec3{28, 1.2f, 32});
    Entity tower = game.add("Tower", airfield, Vec3{-40, 0, 300});
    Entity shaft = game.shape("Shaft", tower, "cube", concrete, Vec3{0, 9, 0}, Vec3{5, 18, 5});
    makeSolid(scene, shaft);
    game.shape("Cab", tower, "cube", glass, Vec3{0, 19.5f, 0}, Vec3{8, 3, 8});
    game.shape("Cap", tower, "cube", roof, Vec3{0, 21.4f, 0}, Vec3{9, 0.8f, 9});
    std::string crown = game.model("Pine", pine(7.5f, 2.3f));
    addTrees(game, game.add("Trees"), crown, leaves, bark, 80, 500, [&](float u, float v, Vec3& at) {
        at = Vec3{(u - 0.5f) * 900.0f, 0, (v - 0.5f) * 1500.0f};
        return std::abs(at.x) > 34.0f && !(at.x > 20 && at.x < 70 && at.z > 300 && at.z < 360);
    });

    // ---- the rings: a climb straight out, then one wide turn to the right, 700 meters across its
    // half: what an aeroplane at 200 km/h goes round with its wings 30 degrees over.
    Vec3 course[8];
    const float heights[8] = {50, 75, 95, 110, 115, 110, 100, 90};
    for (int i = 0; i < 8; i++) {
        float turn = radians(180.0f - 30.0f * float(i));
        course[i] = Vec3{700.0f + 700.0f * std::cos(turn), heights[i], -700.0f - 700.0f * std::sin(turn)};
    }
    Model ring;
    ring.ring(16.0f, 0.8f, 32, 8);
    std::string ringShape = game.model("Ring", ring);
    Entity rings = game.add("Rings");
    for (int i = 0; i < 8; i++) {
        // Each faces the way it is flown through: from the one before to the one after.
        Vec3 before = i > 0 ? course[i - 1] : Vec3{0, 2, 0}, after = i < 7 ? course[i + 1] : course[i] + (course[i] - course[i - 1]);
        Quat facing = lookRotation(simd::normalize(after - before));
        addCollectible(game, "Ring " + std::to_string(i + 1), rings, ringShape, ringLook, course[i], facing, Vec3{1, 1, 1}, 15.0f, "Plane", "rings", false);
    }

    // ---- the aeroplane: the kit's parts, with something to look at on each
    const Vec3 start{0, 1.3f, runwayLength * 0.5f - 60.0f};
    Entity plane = kit::makeAirplane(game.world, start);
    scene.add(plane, Tag{"Player", 0});
    if (kit::FlightControls* controls = scene.get<kit::FlightControls>(plane)) controls->assist = true;
    // What the rings' triggers see: a box about the size of it, that stops nothing itself.
    Collider seen;
    seen.halfExtents = Vec3{5.2f, 1.0f, 3.6f};
    seen.offset = Vec3{0, 0.2f, 0.6f};
    seen.isTrigger = true;
    scene.add(plane, seen);
    game.sounds = {"propeller_2400.wav", "wind.wav", "stall.wav"};
    // What Flight Controls reads besides the usual keys: the rudder, the throttle, the flaps, the brakes.
    game.inputActions = InputMap::flying().toText();

    Model fuselage;
    {
        // Eight-sided, from the nose (-z) to the tail, the tail lifting as it thins.
        struct Cut { float z, half, tall, lift; };
        const Cut cuts[] = {{-2.75f, 0.18f, 0.18f, 0.0f}, {-2.5f, 0.46f, 0.46f, 0.0f},  {-1.5f, 0.58f, 0.62f, 0.02f}, {-0.2f, 0.6f, 0.68f, 0.06f},
                            {1.2f, 0.52f, 0.58f, 0.1f},   {3.0f, 0.3f, 0.36f, 0.24f},  {4.7f, 0.1f, 0.2f, 0.38f},    {5.0f, 0.03f, 0.08f, 0.42f}};
        std::vector<std::vector<Vec3>> sections;
        for (const Cut& cut : cuts) {
            std::vector<Vec3> loop;
            for (int i = 0; i < 8; i++) {
                float turn = (float(i) + 0.5f) / 8.0f * 2.0f * kPi - kPi * 0.5f;
                loop.push_back(Vec3{cut.half * std::cos(turn), cut.lift + cut.tall * std::sin(turn), cut.z});
            }
            sections.push_back(loop);
        }
        fuselage.loft(sections);
    }
    Model canopy;
    {
        std::vector<std::vector<Vec3>> sections;
        sections.push_back({Vec3{-0.44f, 0.5f, -1.5f}, Vec3{0.44f, 0.5f, -1.5f}, Vec3{0.44f, 0.52f, -1.5f}, Vec3{-0.44f, 0.52f, -1.5f}});
        sections.push_back({Vec3{-0.5f, 0.5f, -0.9f}, Vec3{0.5f, 0.5f, -0.9f}, Vec3{0.4f, 1.02f, -0.8f}, Vec3{-0.4f, 1.02f, -0.8f}});
        sections.push_back({Vec3{-0.5f, 0.5f, 0.3f}, Vec3{0.5f, 0.5f, 0.3f}, Vec3{0.4f, 1.02f, 0.2f}, Vec3{-0.4f, 1.02f, 0.2f}});
        sections.push_back({Vec3{-0.42f, 0.5f, 1.3f}, Vec3{0.42f, 0.5f, 1.3f}, Vec3{0.42f, 0.52f, 1.3f}, Vec3{-0.42f, 0.52f, 1.3f}});
        canopy.loft(sections);
    }
    Entity look = game.add("Body", plane);
    game.shape("Fuselage", look, game.model("Fuselage", fuselage), skin);
    game.shape("Canopy", look, game.model("Canopy", canopy), glass);
    game.shape("Stripe", look, "cube", trim, Vec3{0, 0.02f, 0.2f}, Vec3{1.23f, 0.12f, 4.6f}, Quat{}, false);
    // On each part that makes lift, a slab its size: the part's own tilt and place are the slab's.
    scene.each<kit::Wing, Name>([&](Entity wing, kit::Wing& part, Name& name) {
        float span = std::sqrt(part.area * part.aspect), chord = part.area / std::max(span, 0.1f);
        // The main wing is two halves, one on each side; the tailplane and the fin are whole.
        bool isHalf = name.value == "Wing L" || name.value == "Wing R";
        game.shape("Skin", wing, "cube", name.value == "Fin" ? trim : skin, Vec3{0, 0, 0.1f}, Vec3{isHalf ? span * 0.5f : span, 0.13f, chord});
    });
    scene.each<kit::Propeller, Transform>([&](Entity propeller, kit::Propeller&, Transform&) {
        Entity blades = childCalled(scene, propeller, "Blades");
        if (!blades.isValid()) blades = game.add("Blades", propeller);
        game.shape("Blade", blades, "cube", rubber, Vec3{0, 0, -0.06f}, Vec3{0.16f, 1.95f, 0.04f});
        game.shape("Spinner", blades, "sphere", trim, Vec3{0, 0, -0.1f}, Vec3{0.36f, 0.36f, 0.5f});
    });
    scene.each<kit::Wheel, Transform>([&](Entity wheel, kit::Wheel& part, Transform&) {
        Entity spin = childCalled(scene, wheel, "Spin");
        if (!spin.isValid()) spin = game.add("Spin", wheel);
        float round = part.radius * 2.0f;
        game.shape("Tire", spin, "cylinder", rubber, Vec3{0, 0, 0}, Vec3{round, 0.14f, round}, Quat(radians(90.0f), Vec3{0, 0, 1}));
        game.shape("Leg", wheel, "cube", steel, Vec3{0, 0.32f, 0}, Vec3{0.06f, 0.6f, 0.06f});
    });

    addChaseCamera(game, "Plane", start + Vec3{0, 3.5f, 14.0f}, Vec3{0, 3.2f, 13.5f}, 4.0f, 62.0f, 9000.0f);
    addInterface(game, "rings", "RINGS", 8, "All eight rings! Now land it.",
                 {"Shift  Ctrl    throttle up and down", "S  W    pull up, push down      A  D    roll", "Q  E    rudder      F    flaps      B    brakes"},
                 "airspeed", 300, 50, "km/h");
    // The height, beside the dial.
    Entity interface = scene.find("Interface");
    kit::HudText height;
    height.text = "{altitude} m";
    height.anchor = Vec2{0.5f, 1};
    height.offset = Vec2{132, -70};
    height.size = 20;
    scene.add(game.add("Altitude", interface), height);
    kit::HudText power;
    power.text = "POWER {throttle:percent}";
    power.anchor = Vec2{0.5f, 1};
    power.offset = Vec2{-146, -70};
    power.size = 14;
    power.color = Vec4{1, 1, 1, 0.75f};
    scene.add(game.add("Power", interface), power);
}

// ---------------------------------------------------------------- the boat game

void buildBoatGame(Game& game) {
    game.folder = "Boat Game";
    game.description = "A speedboat on open water, islands, and ten buoys to round. Drive with W A S D. The sea, the boat and the buoys that bob on it are all parts in the Inspector: no code.";
    Scene& scene = game.world.scene;
    addDaylight(game, -42, 60, 0.3f);
    RenderSettings& settings = game.world.sceneSettings();
    settings.fogDensity = 0.0009f;
    settings.cascades.shadowDistance = 120;

    auto sand = game.material("Sand", Vec3{0.76f, 0.68f, 0.46f}, 0.95f);
    auto seaBed = game.material("Sea Bed", Vec3{0.5f, 0.52f, 0.4f}, 0.95f);
    auto hullWhite = game.material("Hull", Vec3{0.93f, 0.93f, 0.9f}, 0.3f, 0.05f);
    auto hullBlue = game.material("Hull Stripe", Vec3{0.05f, 0.2f, 0.5f}, 0.3f, 0.1f);
    auto glass = game.material("Glass", Vec3{0.03f, 0.05f, 0.07f}, 0.08f, 0.6f);
    auto seat = game.material("Seats", Vec3{0.7f, 0.25f, 0.1f}, 0.8f);
    auto engine = game.material("Outboard", Vec3{0.06f, 0.06f, 0.07f}, 0.4f, 0.5f);
    auto buoyRed = game.material("Buoy", Vec3{0.85f, 0.1f, 0.06f}, 0.5f);
    auto buoyTop = game.material("Buoy Light", Vec3{1.0f, 0.8f, 0.2f}, 0.3f, 0.0f, Vec3{3.0f, 2.0f, 0.3f});
    auto leaves = game.material("Leaves", Vec3{0.1f, 0.34f, 0.12f}, 0.9f);
    auto bark = game.material("Bark", Vec3{0.3f, 0.2f, 0.12f}, 0.9f);
    auto white = game.material("Lighthouse", Vec3{0.9f, 0.9f, 0.88f}, 0.7f);
    auto red = game.material("Lighthouse Red", Vec3{0.75f, 0.1f, 0.08f}, 0.6f);
    auto lamp = game.material("Lighthouse Lamp", Vec3{1, 0.95f, 0.7f}, 0.3f, 0.0f, Vec3{4.0f, 3.6f, 2.2f});
    auto rock = game.material("Rocks", Vec3{0.3f, 0.3f, 0.3f}, 0.9f);
    // The water: its color where it is deep, and how murky it is. Clear enough to see the bottom near the islands.
    auto sea = game.material("Sea", Vec3{0.02f, 0.13f, 0.2f}, 0.05f);
    sea->baseColor.w = 0.3f;
    sea->isWater = true;
    sea->alphaMode = AlphaMode::Blend;          // for a GPU that cannot show what is behind the water the better way: seen through all the same

    // ---- the sea, and its bed under it
    Entity water = game.shape("Sea", Entity{}, "plane", sea, Vec3{0, 0, 0}, Vec3{1, 1, 1}, Quat{}, false);
    Water waves;
    waves.size = 1600;
    waves.windSpeed = 6.5f;
    waves.waveHeight = 0.7f;
    scene.add(water, waves);
    game.shape("Sea Bed", Entity{}, "plane", seaBed, Vec3{0, -14, 0}, Vec3{2600, 1, 2600}, Quat{}, false);

    // ---- islands: mounds of sand whose tops stand out of the water, with trees on them. Each is
    // a round wall to a boat (a capsule as wide as its shore): run into, it is slid along, not climbed.
    std::string islandShape = game.model("Island", mound(1.0f, 1.0f, 14, 5, 0.3f, 11));
    std::string crown = game.model("Pine", pine(6.0f, 1.9f));
    struct Island { Vec3 at; float foot, height; };
    const float under = 6.0f;                                   // how far under the water their foot is
    const Island islands[] = {{Vec3{-120, 0, -160}, 45, 7.5f}, {Vec3{130, 0, -300}, 60, 9.0f}, {Vec3{-200, 0, -470}, 40, 6.0f}, {Vec3{60, 0, -520}, 42, 8.0f}};
    Entity land = game.add("Islands");
    int islandIndex = 0;
    for (const Island& island : islands) {
        islandIndex++;
        const float tall = island.height + under;
        // How high the island stands at a share of the way out from its middle (its shape is a quarter circle).
        auto heightAt = [&](float share) { return tall * std::sqrt(std::max(1.0f - share * share, 0.0f)) - under; };
        Entity made = game.shape("Island " + std::to_string(islandIndex), land, islandShape, sand, island.at + Vec3{0, -under, 0}, Vec3{island.foot, tall, island.foot});
        float shore = island.foot * std::sqrt(std::max(1.0f - (under / tall) * (under / tall), 0.0f));
        Collider wall;
        wall.shape = ColliderShape::Capsule;
        wall.radius = shore * 0.96f;
        wall.halfHeight = 6.0f;
        wall.offset = Vec3{0, under, 0};
        scene.add(made, wall);
        addTrees(game, land, crown, leaves, bark, 6, 700 + islandIndex * 50, [&](float u, float v, Vec3& at) {
            float turn = u * 2.0f * kPi, share = 0.06f + 0.3f * v;
            at = island.at + Vec3{island.foot * share * std::cos(turn), heightAt(share) - 0.5f, island.foot * share * std::sin(turn)};
            return true;
        });
    }
    // A lighthouse on the far island, to steer for.
    const Island& far = islands[3];
    Entity lighthouse = game.add("Lighthouse", Entity{}, far.at + Vec3{0, far.height - 0.6f, 0});
    game.shape("Tower", lighthouse, "cylinder", white, Vec3{0, 9, 0}, Vec3{4.4f, 18, 4.4f});
    game.shape("Band", lighthouse, "cylinder", red, Vec3{0, 9, 0}, Vec3{4.5f, 4, 4.5f});
    game.shape("Lamp", lighthouse, "cylinder", lamp, Vec3{0, 19.3f, 0}, Vec3{2.6f, 2.4f, 2.6f}, Quat{}, false);
    game.shape("Cap", lighthouse, "sphere", red, Vec3{0, 21.0f, 0}, Vec3{3.6f, 2.2f, 3.6f});
    Entity beam = game.add("Beam", lighthouse, Vec3{0, 19.3f, 0});
    Light glow;
    glow.type = LightType::Point;
    glow.color = Vec3{1.0f, 0.9f, 0.6f};
    glow.intensity = 30;
    glow.range = 60;
    scene.add(beam, glow);
    // Rocks off the islands' shores: out of the way of the course, there to be looked at.
    std::string rockShape = game.model("Rock", mound(1.0f, 1.0f, 7, 3, 0.7f, 23));
    Entity rocks = game.add("Rocks");
    for (int i = 0; i < 10; i++) {
        const Island& near = islands[i % 4];
        float turn = chance(900 + i) * 2.0f * kPi, out = near.foot * 0.95f + 3.0f + 9.0f * chance(930 + i);
        float size = 1.6f + 2.6f * chance(960 + i);
        Vec3 at = near.at + Vec3{out * std::cos(turn), -0.9f, out * std::sin(turn)};
        Entity one = game.shape("Rock " + std::to_string(i + 1), rocks, rockShape, rock, at, Vec3{size * 1.5f, size * 1.3f, size * 1.5f}, heading(57.0f * float(i)));
        Collider solid = Collider::sphere(size * 1.1f);
        solid.offset = Vec3{0, 0.2f, 0};
        scene.add(one, solid);
    }

    // ---- the course: ten buoys that float, one after the other between the islands
    Model buoy;
    {
        std::vector<std::vector<Vec3>> sections;
        const float outs[] = {0.05f, 0.42f, 0.7f, 0.7f, 0.28f, 0.2f}, heights[] = {2.0f, 1.2f, 0.5f, -0.5f, -0.9f, -1.1f};
        for (int level = 0; level < 6; level++) {
            std::vector<Vec3> loop;
            for (int i = 0; i < 10; i++) {
                float turn = -float(i) / 10.0f * 2.0f * kPi;
                loop.push_back(Vec3{outs[level] * std::cos(turn), heights[level], outs[level] * std::sin(turn)});
            }
            sections.push_back(loop);
        }
        buoy.loft(sections);
    }
    std::string buoyShape = game.model("Buoy", buoy);
    Model gate;
    gate.ring(3.0f, 0.22f, 20, 6);
    std::string gateShape = game.model("Gate", gate);
    const Vec3 course[10] = {Vec3{0, 0, -70},    Vec3{30, 0, -150},   Vec3{0, 0, -240},    Vec3{-60, 0, -300},  Vec3{-80, 0, -390},
                             Vec3{-30, 0, -470}, Vec3{-60, 0, -570},  Vec3{30, 0, -640},   Vec3{140, 0, -600},  Vec3{190, 0, -480}};
    Entity buoys = game.add("Buoys");
    for (int i = 0; i < 10; i++) {
        Entity floats = game.shape("Buoy " + std::to_string(i + 1), buoys, buoyShape, buoyRed, course[i] + Vec3{0, 0.3f, 0});
        Collider solid = Collider::sphere(0.7f);
        scene.add(floats, solid);
        RigidBody body;
        body.mass = 60;
        body.linearDamping = 0.4f;
        scene.add(floats, body);
        kit::Buoyancy lift;
        lift.lift = 1.8f;
        lift.depth = 0.9f;
        lift.drag = 3.0f;
        scene.add(floats, lift);
        game.shape("Light", floats, "sphere", buoyTop, Vec3{0, 2.15f, 0}, Vec3{0.34f, 0.34f, 0.34f}, Quat{}, false);
        // What is collected is the ring that stands over it: through it, or close by.
        Vec3 before = i > 0 ? course[i - 1] : Vec3{0, 0, 0}, after = i < 9 ? course[i + 1] : course[i] + (course[i] - course[i - 1]);
        Vec3 along = after - before;
        along.y = 0;
        addCollectible(game, "Gate " + std::to_string(i + 1), buoys, gateShape, buoyTop, course[i] + Vec3{0, 4.6f, 0}, lookRotation(simd::normalize(along)), Vec3{1, 1, 1}, 7.5f, "Boat",
                       "buoys", false);
    }

    // ---- the boat: a body that floats, a Boat to drive it, and a hull to look at
    Entity boat = game.add("Boat", Entity{}, Vec3{0, 0.4f, 0});
    scene.add(boat, Tag{"Player", 0});
    RigidBody body;
    body.mass = 700;
    body.rotates = true;
    body.linearDamping = 0.02f;
    scene.add(boat, body);
    const Vec3 hullHalf{1.1f, 0.42f, 3.0f};
    Collider solid;
    solid.halfExtents = hullHalf;
    scene.add(boat, solid);
    kit::Buoyancy floats;
    floats.hull = hullHalf;
    floats.lift = 2.3f;
    floats.depth = 0.42f;
    floats.drag = 2.2f;
    floats.turnDrag = 2.2f;
    scene.add(boat, floats);
    kit::Boat driven;
    driven.acceleration = 7.5f;
    driven.topSpeed = 17.0f;
    driven.turnSpeed = 52.0f;
    driven.sound = "Audio/boat.wav";
    scene.add(boat, driven);
    game.sounds = {"boat.wav"};

    Model hull;
    {
        // From the bow (-z) to the square stern: the keel, the chines, the gunwales. The deck is the top of each cut.
        struct Cut { float z, keel, halfChine, chineY, halfDeck, deckY; };
        const Cut cuts[] = {{-3.35f, 0.42f, 0.02f, 0.46f, 0.03f, 0.56f}, {-2.6f, -0.02f, 0.3f, 0.12f, 0.62f, 0.6f}, {-1.4f, -0.3f, 0.68f, -0.04f, 1.0f, 0.58f},
                            {0.4f, -0.4f, 0.9f, -0.1f, 1.12f, 0.54f},     {2.2f, -0.38f, 0.92f, -0.1f, 1.1f, 0.5f},  {2.95f, -0.34f, 0.9f, -0.08f, 1.06f, 0.48f}};
        std::vector<std::vector<Vec3>> sections;
        for (const Cut& cut : cuts) {
            sections.push_back({Vec3{0, cut.keel, cut.z}, Vec3{cut.halfChine, cut.chineY, cut.z}, Vec3{cut.halfDeck, cut.deckY, cut.z}, Vec3{-cut.halfDeck, cut.deckY, cut.z},
                                Vec3{-cut.halfChine, cut.chineY, cut.z}});
        }
        hull.loft(sections);
    }
    Model screen;
    {
        std::vector<std::vector<Vec3>> sections;
        sections.push_back({Vec3{-0.8f, 0.56f, -0.9f}, Vec3{0.8f, 0.56f, -0.9f}, Vec3{0.8f, 0.58f, -0.9f}, Vec3{-0.8f, 0.58f, -0.9f}});
        sections.push_back({Vec3{-0.86f, 0.56f, -0.45f}, Vec3{0.86f, 0.56f, -0.45f}, Vec3{0.74f, 1.12f, -0.3f}, Vec3{-0.74f, 1.12f, -0.3f}});
        sections.push_back({Vec3{-0.86f, 0.56f, -0.3f}, Vec3{0.86f, 0.56f, -0.3f}, Vec3{0.74f, 1.12f, -0.24f}, Vec3{-0.74f, 1.12f, -0.24f}});
        screen.loft(sections);
    }
    Entity look = game.add("Hull", boat);
    game.shape("Skin", look, game.model("Hull", hull), hullWhite);
    game.shape("Stripe", look, "cube", hullBlue, Vec3{0, 0.5f, 0.3f}, Vec3{2.26f, 0.1f, 5.3f}, Quat{}, false);
    game.shape("Windscreen", look, game.model("Windscreen", screen), glass);
    game.shape("Seat L", look, "cube", seat, Vec3{-0.45f, 0.72f, 0.5f}, Vec3{0.6f, 0.34f, 0.6f});
    game.shape("Seat R", look, "cube", seat, Vec3{0.45f, 0.72f, 0.5f}, Vec3{0.6f, 0.34f, 0.6f});
    game.shape("Bench", look, "cube", seat, Vec3{0, 0.7f, 2.2f}, Vec3{1.7f, 0.3f, 0.55f});
    game.shape("Outboard", look, "cube", engine, Vec3{0, 0.62f, 3.12f}, Vec3{0.5f, 0.9f, 0.5f});
    game.shape("Outboard Leg", look, "cube", engine, Vec3{0, -0.1f, 3.2f}, Vec3{0.16f, 0.9f, 0.3f});

    addChaseCamera(game, "Boat", Vec3{0, 4.0f, 10.0f}, Vec3{0, 3.4f, 9.5f}, 3.5f, 62.0f, 3000.0f);
    addInterface(game, "buoys", "BUOYS", 10, "All ten buoys rounded!", {"W  S    ahead and astern", "A  D    the rudder: it bites once the boat is moving"}, "knots", 40, 10, "knots");
}

// ---------------------------------------------------------------- the third person game

/// A cube that is in the way: of stone, of wood, whatever `look` is.
Entity addBlock(Game& game, const std::string& name, Entity parent, const std::shared_ptr<Material>& look, Vec3 center, Vec3 size, Quat rotation = Quat{}) {
    Entity block = game.shape(name, parent, "cube", look, center, size, rotation);
    makeSolid(game.world.scene, block);
    return block;
}

/// A cut stone: two points and a waist of six sides.
Model gem() {
    std::vector<std::vector<Vec3>> sections;
    const float heights[3] = {0.42f, 0.08f, -0.42f}, radii[3] = {0.012f, 0.27f, 0.012f};
    for (int level = 0; level < 3; level++) {
        std::vector<Vec3> loop;
        for (int i = 0; i < 6; i++) {
            float turn = -float(i) / 6.0f * 2.0f * kPi;
            loop.push_back(Vec3{radii[level] * std::cos(turn), heights[level], radii[level] * std::sin(turn)});
        }
        sections.push_back(loop);
    }
    Model made;
    made.loft(sections);
    return made;
}

/// The figure the player walks with, and the camera behind it: Models/Figure.glb with its
/// bones and clips (see StarterFigure.hpp), an Animator that follows Animations/Player.animator,
/// and the kit's character and orbit camera.
Entity addWalker(Game& game, Vec3 at, bool asPrefab = false) {
    Scene& scene = game.world.scene;
    Figure figure = makeFigure();
    game.files["Models/Figure.glb"] = figure.glb;
    game.files["Models/Figure.png"] = figure.palette;
    auto look = game.material("Figure", Vec3{1, 1, 1}, 0.6f);
    game.pictures["Figure"] = "Models/Figure.png";

    Entity player = game.add("Player", Entity{}, at);
    scene.add(player, Tag{"Player", 0});
    kit::CharacterController body;
    body.radius = 0.32f;
    scene.add(player, body);
    kit::CharacterMover mover;
    mover.walkSpeed = Figure::kWalkSpeed;       // the speeds its Walk and Run clips cover the ground at: its feet do not slide
    mover.runSpeed = Figure::kRunSpeed;
    mover.alwaysRuns = true;
    mover.jumpHeight = 1.15f;
    scene.add(player, mover);
    // What triggers see of it: a capsule its size that blocks nothing.
    Collider seen = Collider::capsule(0.35f, 0.55f);
    seen.offset = Vec3{0, 0.9f, 0};
    seen.isTrigger = true;
    scene.add(player, seen);
    // The model looks toward +z, as models do; the engine's forward is -z: it is turned half way round.
    Entity model = game.shape("Figure", player, "Models/Figure.glb", look, Vec3{0, 0, 0}, Vec3{1, 1, 1}, heading(180));
    Animator animator;
    animator.model = "Models/Figure.glb";
    animator.controller = "Animations/Player.animator";
    scene.add(model, animator);
    // What it does and when: standing, going (a walk and a run blended by its speed), leaving the ground, falling.
    game.texts["Animations/Player.animator"] =
        "molten-animator 1\n"
        "parameter float speed 0\n"
        "parameter bool grounded 1\n"
        "parameter float verticalSpeed 0\n"
        "parameter trigger jump\n"
        "\n"
        "state Idle\n"
        "  motion Idle\n"
        "  place 60 60\n"
        "state Move blend speed\n"
        "  motion Walk " + std::to_string(Figure::kWalkSpeed) + "\n"
        "  motion Run " + std::to_string(Figure::kRunSpeed) + "\n"
        "  event Audio/step.wav 0.03\n"
        "  event Audio/step.wav 0.53\n"
        "  place 300 60\n"
        "state Jump loops 0\n"
        "  motion Jump\n"
        "  event Audio/jump.wav 0\n"
        "  place 300 220\n"
        "state Fall\n"
        "  motion Fall\n"
        "  place 60 220\n"
        "\n"
        "start Idle\n"
        "transition Idle Move when speed > 0.3 blend 0.15\n"
        "transition Move Idle when speed < 0.2 blend 0.2\n"
        "transition Idle Jump when jump blend 0.08\n"
        "transition Move Jump when jump blend 0.08\n"
        "transition Idle Fall when grounded false blend 0.2\n"
        "transition Move Fall when grounded false blend 0.2\n"
        "transition Jump Fall exit 0.85 blend 0.2\n"
        "transition Jump Move when grounded true and speed > 0.3 blend 0.12\n"
        "transition Jump Idle when grounded true blend 0.12\n"
        "transition Fall Move when grounded true and speed > 0.3 blend 0.12\n"
        "transition Fall Idle when grounded true blend 0.15\n";

    Entity camera = game.add("Camera", Entity{}, at + Vec3{0, 2.6f, 4.2f});
    Camera lens;
    lens.fovY = radians(60.0f);
    lens.nearPlane = 0.1f;
    lens.farPlane = 500;
    scene.add(camera, lens);
    kit::OrbitCamera orbit;
    orbit.target = "Player";
    orbit.distance = 4.4f;
    orbit.height = 1.45f;
    orbit.pitch = 14;
    orbit.capturesMouse = true;
    scene.add(camera, orbit);
    if (!asPrefab) return player;

    // For a game played together, the player is not in the scene: it is a prefab, and everyone
    // who is in the game gets one. Only its owner's machine keeps what moves it; the others are told where it is.
    kit::Networked shared;
    shared.ownerOnly = "CharacterController, CharacterMover";
    scene.add(player, shared);
    if (Transform* place = scene.get<Transform>(player)) place->position = Vec3{0, 0, 0};
    scene.updateTransforms();
    std::vector<Entity> parts = {player, model};
    RenderSettings none;
    game.texts["Prefabs/Player.prefab"] = writeScene(scene, {}, none, &parts);
    scene.destroy(model);
    scene.destroy(player);
    scene.flushDestroyed();
    return Entity{};
}

/// A garden of stone to walk about: a terrace up some stairs, a bridge, blocks to jump across
/// to a tower, and a ramp back down; ten gems along the way.
void buildGarden(Game& game, bool together) {
    Scene& scene = game.world.scene;
    game.folder = together ? "Online Game" : "Third Person Game";
    game.description = together ? "The garden for several players: one hosts, the others join at its address, and everyone has a figure of their own. No code."
                                : "A figure that walks, runs and jumps, with its animations, and a camera the mouse turns. Ten gems to find. No code.";
    game.sounds = {"Audio/step.wav", "Audio/jump.wav", "Audio/pickup.wav"};
    addDaylight(game, -50, 32, 0.3f);
    game.world.sceneSettings().cascades.shadowDistance = 90;

    auto grass = game.material("Grass", Vec3{0.24f, 0.46f, 0.19f}, 0.95f);
    auto stone = game.material("Stone", Vec3{0.63f, 0.61f, 0.57f}, 0.85f);
    auto darkStone = game.material("Stone Dark", Vec3{0.40f, 0.40f, 0.41f}, 0.9f);
    auto paving = game.material("Paving", Vec3{0.72f, 0.67f, 0.55f}, 0.9f);
    auto wood = game.material("Wood", Vec3{0.52f, 0.34f, 0.17f}, 0.85f);
    auto plank = game.material("Planks", Vec3{0.43f, 0.28f, 0.15f}, 0.8f);
    auto jewel = game.material("Gem", Vec3{0.10f, 0.78f, 0.72f}, 0.2f, 0.0f, Vec3{0.1f, 1.5f, 1.3f});
    auto leaves = game.material("Leaves", Vec3{0.13f, 0.36f, 0.16f}, 0.9f);
    auto bark = game.material("Bark", Vec3{0.30f, 0.21f, 0.14f}, 0.9f);
    auto hill = game.material("Hills", Vec3{0.27f, 0.45f, 0.24f}, 1.0f);
    auto rock = game.material("Rocks", Vec3{0.46f, 0.45f, 0.44f}, 0.95f);

    // ---- the ground, and the hills that close the view
    Entity land = game.add("Land");
    addBlock(game, "Ground", land, grass, Vec3{0, -0.5f, 0}, Vec3{180, 1, 180});
    Model hills;
    for (int i = 0; i < 9; i++) {
        float turn = float(i) / 9.0f * 2 * kPi + 0.3f, far = 62 + 14 * chance(700 + i);
        hills.add(mound(15 + 9 * chance(710 + i), 6 + 6 * chance(720 + i), 9, 3, 0.3f, 730 + i), Vec3{far * std::cos(turn), -0.2f, far * std::sin(turn)});
    }
    game.shape("Hills", land, game.model("Hills", hills), hill);

    // ---- what is walked on: a square to start from, the way to the stairs
    Entity garden = game.add("Garden");
    addBlock(game, "Square", garden, paving, Vec3{0, 0.06f, 16}, Vec3{7, 0.12f, 7});
    game.shape("Way 1", garden, "cube", paving, Vec3{0, 0.02f, 10.2f}, Vec3{1.6f, 0.04f, 4.6f}, Quat{}, false);
    game.shape("Way 2", garden, "cube", paving, Vec3{-4.5f, 0.02f, 8.2f}, Vec3{10.6f, 0.04f, 1.6f}, Quat{}, false);

    // The terrace in the west, 1.6 meters up, and the eight steps to it.
    addBlock(game, "Terrace", garden, stone, Vec3{-9, 0.8f, -1}, Vec3{10, 1.6f, 10});
    Entity stairs = game.add("Stairs", garden);
    for (int step = 1; step <= 8; step++) {
        float top = 0.2f * float(step);
        addBlock(game, "Step " + std::to_string(step), stairs, step % 2 ? darkStone : stone, Vec3{-9, top * 0.5f, 4.0f + (8.0f - float(step)) * 0.4f + 0.2f}, Vec3{2.4f, top, 0.4f});
    }
    for (float x : {-13.5f, -4.5f}) {
        for (float z : {-5.5f, 3.5f}) {
            Entity column = game.shape("Column", garden, "cylinder", darkStone, Vec3{x, 2.8f, z}, Vec3{0.6f, 2.4f, 0.6f});
            Collider solid;
            solid.halfExtents = Vec3{0.3f, 1.2f, 0.3f};
            scene.add(column, solid);
            game.shape("Cap", column, "cube", stone, Vec3{0, 0.56f, 0}, Vec3{1.5f, 0.12f, 1.5f});
        }
    }

    // The bridge east from the terrace, of planks, with a rail to each side that is only looked at.
    addBlock(game, "Bridge", garden, plank, Vec3{0, 1.5f, -1}, Vec3{8, 0.2f, 1.7f});
    for (float z : {-1.8f, -0.2f}) {
        game.shape("Rail", garden, "cube", wood, Vec3{0, 2.25f, z}, Vec3{8, 0.07f, 0.07f}, Quat{}, false);
        for (int post = 0; post < 5; post++) game.shape("Post", garden, "cube", wood, Vec3{-4 + 2.0f * float(post), 1.92f, z}, Vec3{0.09f, 0.66f, 0.09f}, Quat{}, false);
    }
    addBlock(game, "East Terrace", garden, stone, Vec3{7, 0.8f, -1}, Vec3{6, 1.6f, 6});

    // Blocks to jump across, each higher than the last, round to the tower.
    const Vec3 blocks[4] = {{12.2f, 2.0f, -1.0f}, {14.9f, 2.4f, -2.9f}, {15.4f, 2.8f, -6.1f}, {13.4f, 3.2f, -8.9f}};
    for (int i = 0; i < 4; i++) {
        addBlock(game, "Block " + std::to_string(i + 1), garden, i % 2 ? stone : darkStone, Vec3{blocks[i].x, blocks[i].y * 0.5f, blocks[i].z}, Vec3{1.8f, blocks[i].y, 1.8f});
    }
    addBlock(game, "Tower", garden, stone, Vec3{9, 1.6f, -10.5f}, Vec3{5, 3.2f, 5});
    // And the way down: a ramp to the west, 9 meters long, that anyone can walk.
    {
        const float run = 9.0f, drop = 3.2f, thick = 0.3f;
        float slope = std::atan2(drop, run), length = std::sqrt(run * run + drop * drop);
        Vec3 middle{6.5f - run * 0.5f + std::sin(slope) * thick * 0.5f, drop * 0.5f - std::cos(slope) * thick * 0.5f, -10.5f};
        addBlock(game, "Ramp", garden, plank, middle, Vec3{length, thick, 2.6f}, Quat(slope, Vec3{0, 0, 1}));
        for (float x : {4.4f, 1.9f, -0.5f}) {
            float top = (x + 2.5f) / run * drop - 0.25f;
            game.shape("Trestle", garden, "cube", wood, Vec3{x, top * 0.5f, -10.5f}, Vec3{0.18f, top, 2.2f});
        }
    }

    // ---- crates to shove about, by the way to the stairs
    Entity crates = game.add("Crates");
    const Vec3 crateAt[5] = {{-3.6f, 0.4f, 10.4f}, {-4.5f, 0.4f, 10.6f}, {-4.05f, 1.2f, 10.5f}, {-6.2f, 0.4f, 6.4f}, {2.6f, 0.4f, 12.6f}};
    for (int i = 0; i < 5; i++) {
        Entity crate = game.shape("Crate", crates, "cube", wood, crateAt[i], Vec3{0.8f, 0.8f, 0.8f}, heading(18.0f * float(i)));
        Collider solid;
        solid.halfExtents = Vec3{0.4f, 0.4f, 0.4f};
        scene.add(crate, solid);
        RigidBody body;
        body.mass = 9;
        body.rotates = true;
        body.friction = 0.6f;
        scene.add(crate, body);
        if (together) {
            // Only the server pushes them about; the other machines show where it has them.
            kit::Networked shared;
            shared.ownerOnly = "RigidBody";
            scene.add(crate, shared);
        }
    }

    // ---- the gems: each a meter above what is stood on
    Entity gems = game.add("Gems");
    std::string cut = game.model("Gem", gem());
    const Vec3 gemAt[10] = {{0, 1.05f, 11.0f},     {-4.5f, 1.05f, 8.2f},   {-9, 1.95f, 5.4f},      {-11.5f, 2.65f, -3.5f}, {0, 2.65f, -1},
                            {12.2f, 3.05f, -1.0f}, {15.4f, 3.85f, -6.1f}, {9, 4.25f, -10.5f},     {2.0f, 2.7f, -10.5f},   {-6.0f, 1.05f, -11.5f}};
    for (int i = 0; i < 10; i++) {
        Entity one = addCollectible(game, "Gem " + std::to_string(i + 1), gems, cut, jewel, gemAt[i], Quat{}, Vec3{1, 1, 1}, 0.75f, "Player", "gems", true);
        if (kit::Collectible* collectible = scene.get<kit::Collectible>(one)) collectible->sound = "Audio/pickup.wav";
        if (together) {
            // It turns by itself on every machine; only the server says who collected it, and then it is gone from all of them.
            kit::Networked shared;
            shared.syncsPlace = false;
            shared.ownerOnly = "Collectible";
            scene.add(one, shared);
        }
    }

    // ---- trees and rocks, away from where the walking is
    std::string crown = game.model("Pine", pine(4.6f, 1.5f));
    addTrees(game, game.add("Trees"), crown, leaves, bark, 46, 4100, [](float a, float b, Vec3& at) {
        at = Vec3{-70 + 140 * a, 0, -70 + 140 * b};
        bool nearTheGarden = at.x > -18 && at.x < 20 && at.z > -20 && at.z < 22;
        return !nearTheGarden && simd::length(Vec2{at.x, at.z}) < 68;
    });
    std::string boulder = game.model("Rock", mound(1.0f, 0.9f, 7, 2, 0.5f, 910));
    Entity rocks = game.add("Rocks");
    const Vec3 rockAt[8] = {{-2.2f, 0, -3.4f}, {1.6f, 0, 0.9f}, {3.1f, 0, -2.6f}, {-7.5f, 0, -12.6f}, {-5.2f, 0, -13.1f}, {5.5f, 0, 9.5f}, {-15.5f, 0, 8.0f}, {19.0f, 0, -4.5f}};
    for (int i = 0; i < 8; i++) {
        float size = 0.7f + 1.1f * chance(950 + i);
        game.shape("Rock", rocks, boulder, rock, rockAt[i], Vec3{size, size * (0.7f + 0.5f * chance(960 + i)), size}, heading(360 * chance(970 + i)));
    }

    addWalker(game, Vec3{0, 0.14f, 16}, together);
    Entity interface = addInterface(game, "gems", "GEMS", 10, "All ten gems!", {"W A S D   move", "Shift   walk", "Space   jump", "Mouse   look about", "Esc   free the pointer"}, "", 0, 0, "");
    if (!together) return;

    // ---- together: who is in the game, and where each appears
    Entity session = game.add("Session");
    kit::NetworkSession play;
    play.playerPrefab = "Prefabs/Player.prefab";
    play.sharedValues = "gems";
    scene.add(session, play);
    for (int i = 0; i < 6; i++) {
        float turn = float(i) / 6.0f * 2 * kPi;
        game.add("Spawn " + std::to_string(i + 1), session, Vec3{2.2f * std::sin(turn), 0.14f, 16 + 2.2f * std::cos(turn)});
    }
    // Starting over would end the game for everyone: the button leaves it instead.
    if (kit::UIButton* button = scene.get<kit::UIButton>(childCalled(scene, interface, "Restart"))) button->label = "Leave";
}

void buildThirdPersonGame(Game& game) { buildGarden(game, false); }
void buildOnlineGame(Game& game) { buildGarden(game, true); }

// ---------------------------------------------------------------- the shooter game

/// A scene of its own as the text of a prefab: something to spawn.
std::string prefabOf(const std::function<void(Scene&)>& make) {
    Scene scene;
    make(scene);
    scene.updateTransforms();
    RenderSettings none;
    return writeScene(scene, {}, none);
}

/// A steel plate on a post, its rings toward its -z, that falls when it is shot.
Entity addTarget(Game& game, const std::string& name, Entity parent, Vec3 foot, float facing, const std::shared_ptr<Material>& white, const std::shared_ptr<Material>& red,
                 const std::shared_ptr<Material>& steel, float standsAfter = 0) {
    Scene& scene = game.world.scene;
    Entity target = game.add(name, parent, foot, heading(facing));
    kit::ShootingTarget plate;
    plate.sound = "Audio/hit.wav";
    plate.standsAfter = standsAfter;
    scene.add(target, plate);
    game.shape("Post", target, "cube", steel, Vec3{0, 0.5f, 0.02f}, Vec3{0.07f, 1.0f, 0.05f});
    // The plate stands on its edge: a cylinder turned to face along z. What a shot meets is a box its size.
    Quat onEdge(radians(90.0f), Vec3{1, 0, 0});
    Entity face = game.shape("Plate", target, "cylinder", white, Vec3{0, 1.3f, 0}, Vec3{0.72f, 0.04f, 0.72f}, onEdge);
    Collider solid;
    solid.halfExtents = Vec3{0.36f, 0.03f, 0.36f};
    scene.add(face, solid);
    game.shape("Ring", target, "cylinder", red, Vec3{0, 1.3f, -0.012f}, Vec3{0.50f, 0.03f, 0.50f}, onEdge, false);
    game.shape("Inner", target, "cylinder", white, Vec3{0, 1.3f, -0.020f}, Vec3{0.28f, 0.03f, 0.28f}, onEdge, false);
    game.shape("Middle", target, "cylinder", red, Vec3{0, 1.3f, -0.028f}, Vec3{0.10f, 0.03f, 0.10f}, onEdge, false);
    return target;
}

/// A machine that hunts: a dark ball with one red eye, under a rotor that turns.
Entity addHunter(Game& game, const std::string& name, Vec3 at, float facing, const std::shared_ptr<Material>& shell, const std::shared_ptr<Material>& eye,
                 const std::shared_ptr<Material>& steel) {
    Scene& scene = game.world.scene;
    Entity hunter = game.shape(name, Entity{}, "sphere", shell, at, Vec3{0.8f, 0.7f, 0.8f}, heading(facing));
    Collider solid = Collider::sphere(0.42f);
    scene.add(hunter, solid);
    kit::Hunter hunts;
    hunts.attackSound = "Audio/hurt.wav";
    scene.add(hunter, hunts);
    kit::Health health;
    health.amount = 3;
    health.scoreCounter = "score";
    health.points = 1;
    health.effect = "Prefabs/Burst.prefab";
    scene.add(hunter, health);
    // Its parts are a ball's own size apart: the ball is scaled, so they are too.
    game.shape("Eye", hunter, "sphere", eye, Vec3{0, 0.02f, -0.44f}, Vec3{0.3f, 0.34f, 0.2f}, Quat{}, false);
    game.shape("Mast", hunter, "cube", steel, Vec3{0, 0.58f, 0}, Vec3{0.08f, 0.3f, 0.08f}, Quat{}, false);
    Entity rotor = game.shape("Rotor", hunter, "cube", steel, Vec3{0, 0.74f, 0}, Vec3{2.1f, 0.03f, 0.16f}, Quat{}, false);
    kit::Spin spin;
    spin.degreesPerSecond = 1400;
    scene.add(rotor, spin);
    game.shape("Rotor Across", rotor, "cube", steel, Vec3{0, 0, 0}, Vec3{0.076f, 1, 13.1f}, Quat{}, false);
    return hunter;
}

/// The player seen from their own eyes, and the rifle they hold.
Entity addShooter(Game& game, Vec3 at, const std::shared_ptr<Material>& gunmetal, const std::shared_ptr<Material>& accent) {
    Scene& scene = game.world.scene;
    Entity player = game.add("Player", Entity{}, at);
    scene.add(player, Tag{"Player", 0});
    kit::CharacterController body;
    body.radius = 0.32f;
    scene.add(player, body);
    kit::CharacterMover mover;
    mover.walkSpeed = 3.6f;
    mover.runSpeed = 6.4f;
    mover.acceleration = 34;
    mover.jumpHeight = 0.9f;
    mover.turnsToFace = false;                  // its view turns it
    scene.add(player, mover);
    Collider seen = Collider::capsule(0.35f, 0.55f);
    seen.offset = Vec3{0, 0.9f, 0};
    seen.isTrigger = true;
    scene.add(player, seen);
    kit::Health health;
    health.amount = 100;
    health.counter = "health";
    health.scoreCounter.clear();
    health.restartsSceneOnDeath = true;
    scene.add(player, health);

    Entity camera = game.add("Camera", player, Vec3{0, 1.62f, 0});
    Camera lens;
    lens.fovY = radians(72.0f);
    lens.nearPlane = 0.04f;
    lens.farPlane = 400;
    scene.add(camera, lens);
    scene.add(camera, kit::FirstPersonLook{});

    // The rifle, held low and to the right, of a few boxes: its barrel along -z.
    Entity rifle = game.add("Rifle", camera, Vec3{0.165f, -0.165f, -0.34f});
    kit::Weapon weapon;
    weapon.hitEffect = "Prefabs/Impact.prefab";
    weapon.shotSound = "Audio/shot.wav";
    weapon.emptySound = "Audio/empty.wav";
    weapon.reloadSound = "Audio/reload.wav";
    scene.add(rifle, weapon);
    auto part = [&](const char* name, const std::shared_ptr<Material>& look, Vec3 center, Vec3 size, float tipped = 0) {
        return game.shape(name, rifle, "cube", look, center, size, Quat(radians(tipped), Vec3{1, 0, 0}), false);
    };
    part("Receiver", gunmetal, Vec3{0, 0, 0}, Vec3{0.05f, 0.078f, 0.30f});
    part("Guard", accent, Vec3{0, -0.004f, -0.265f}, Vec3{0.046f, 0.062f, 0.235f});
    part("Barrel", gunmetal, Vec3{0, 0.008f, -0.47f}, Vec3{0.02f, 0.02f, 0.2f});
    part("Magazine", gunmetal, Vec3{0, -0.09f, -0.055f}, Vec3{0.036f, 0.135f, 0.066f}, -12);
    part("Grip", gunmetal, Vec3{0, -0.072f, 0.088f}, Vec3{0.034f, 0.095f, 0.046f}, 18);
    part("Stock", accent, Vec3{0, -0.012f, 0.245f}, Vec3{0.04f, 0.064f, 0.2f});
    part("Rear Sight", gunmetal, Vec3{0, 0.05f, 0.07f}, Vec3{0.014f, 0.024f, 0.02f});
    part("Front Sight", gunmetal, Vec3{0, 0.046f, -0.37f}, Vec3{0.008f, 0.032f, 0.012f});
    // Where it flashes: a light that is on for a moment at each shot, and two bright blades across each other.
    Entity muzzle = game.add("Muzzle", rifle, Vec3{0, 0.008f, -0.61f});
    Light flash;
    flash.type = LightType::Point;
    flash.color = Vec3{1.0f, 0.74f, 0.38f};
    flash.intensity = 16;
    flash.range = 8;
    flash.isEnabled = false;
    scene.add(muzzle, flash);
    auto fire = game.material("Muzzle Flash", Vec3{1.0f, 0.85f, 0.5f}, 1.0f, 0.0f, Vec3{6.0f, 3.6f, 1.2f});
    fire->isUnlit = true;
    for (int blade = 0; blade < 2; blade++) {
        Entity shown = game.shape(blade ? "Flash Across" : "Flash", muzzle, "cube", fire, Vec3{0, 0, 0}, Vec3{0.17f, 0.035f, 0.01f}, Quat(radians(blade ? 90.0f : 0.0f), Vec3{0, 0, 1}), false);
        if (MeshRenderer* renderer = scene.get<MeshRenderer>(shown)) renderer->isVisible = false;
    }
    return player;
}

/// A training yard inside four walls: plates to shoot down, near and far, some behind cover,
/// two that move; machines that hunt whoever they see; rounds and bandages to pick up.
void buildShooterGame(Game& game) {
    Scene& scene = game.world.scene;
    game.folder = "Shooter Game";
    game.description = "Through your own eyes, a rifle in your hands: a magazine to load, plates to shoot down, machines that hunt you. No code.";
    game.sounds = {"Audio/shot.wav", "Audio/empty.wav", "Audio/reload.wav", "Audio/hit.wav", "Audio/hurt.wav", "Audio/down.wav", "Audio/pickup.wav"};
    addDaylight(game, -42, -38, 0.25f);
    game.world.sceneSettings().cascades.shadowDistance = 80;

    auto ground = game.material("Ground", Vec3{0.52f, 0.49f, 0.42f}, 0.95f);
    auto grass = game.material("Grass", Vec3{0.25f, 0.44f, 0.2f}, 0.95f);
    auto wall = game.material("Wall", Vec3{0.34f, 0.36f, 0.40f}, 0.9f);
    auto cover = game.material("Cover", Vec3{0.50f, 0.53f, 0.57f}, 0.85f);
    auto stripe = game.material("Stripe", Vec3{0.94f, 0.66f, 0.10f}, 0.7f);
    auto wood = game.material("Crate", Vec3{0.52f, 0.35f, 0.18f}, 0.85f);
    auto steel = game.material("Steel", Vec3{0.22f, 0.23f, 0.26f}, 0.45f, 0.8f);
    auto white = game.material("Target White", Vec3{0.92f, 0.92f, 0.88f}, 0.6f);
    auto red = game.material("Target Red", Vec3{0.82f, 0.10f, 0.07f}, 0.6f);
    auto gunmetal = game.material("Gunmetal", Vec3{0.13f, 0.14f, 0.16f}, 0.42f, 0.7f);
    auto accent = game.material("Rifle Accent", Vec3{0.95f, 0.44f, 0.10f}, 0.55f);
    auto shell = game.material("Hunter", Vec3{0.16f, 0.17f, 0.20f}, 0.35f, 0.6f);
    auto eye = game.material("Hunter Eye", Vec3{1.0f, 0.12f, 0.06f}, 0.3f, 0.0f, Vec3{5.0f, 0.4f, 0.2f});
    auto rounds = game.material("Rounds", Vec3{0.24f, 0.36f, 0.20f}, 0.8f);
    auto bandage = game.material("Bandage", Vec3{0.94f, 0.94f, 0.92f}, 0.7f);
    auto cross = game.material("Bandage Cross", Vec3{0.85f, 0.12f, 0.10f}, 0.6f);
    auto barrel = game.material("Barrel", Vec3{0.16f, 0.34f, 0.52f}, 0.5f, 0.3f);
    auto leaves = game.material("Leaves", Vec3{0.13f, 0.36f, 0.16f}, 0.9f);
    auto bark = game.material("Bark", Vec3{0.30f, 0.21f, 0.14f}, 0.9f);

    // ---- the yard: 50 meters square, a wall 3.5 high about it, grass and trees outside
    Entity yard = game.add("Yard");
    addBlock(game, "Land", yard, grass, Vec3{0, -0.52f, 0}, Vec3{240, 1, 240});
    addBlock(game, "Floor", yard, ground, Vec3{0, -0.5f, 0}, Vec3{51, 1.02f, 51});
    addBlock(game, "North Wall", yard, wall, Vec3{0, 1.75f, -25.5f}, Vec3{52, 3.5f, 1});
    addBlock(game, "South Wall", yard, wall, Vec3{0, 1.75f, 25.5f}, Vec3{52, 3.5f, 1});
    addBlock(game, "East Wall", yard, wall, Vec3{25.5f, 1.75f, 0}, Vec3{1, 3.5f, 50});
    addBlock(game, "West Wall", yard, wall, Vec3{-25.5f, 1.75f, 0}, Vec3{1, 3.5f, 50});
    std::string crown = game.model("Pine", pine(5.2f, 1.7f));
    addTrees(game, game.add("Trees"), crown, leaves, bark, 40, 6200, [](float a, float b, Vec3& at) {
        at = Vec3{-90 + 180 * a, 0, -90 + 180 * b};
        return std::abs(at.x) > 30 || std::abs(at.z) > 30;
    });

    // ---- the line to shoot from, and things to stand behind
    addBlock(game, "Firing Line", yard, cover, Vec3{0, 0.5f, 14.5f}, Vec3{11, 1.0f, 0.5f});
    game.shape("Line Stripe", yard, "cube", stripe, Vec3{0, 1.01f, 14.5f}, Vec3{11, 0.03f, 0.52f}, Quat{}, false);
    const struct {
        Vec3 center, size;
    } covers[] = {{{-9.0f, 0.6f, 6.0f}, {4.0f, 1.2f, 0.5f}},  {{8.0f, 0.6f, 4.5f}, {0.5f, 1.2f, 4.0f}},   {{-2.5f, 1.5f, -3.0f}, {0.6f, 3.0f, 5.0f}}, {{4.5f, 1.5f, -9.0f}, {5.0f, 3.0f, 0.6f}},
                  {{-14.0f, 1.5f, -8.0f}, {5.0f, 3.0f, 0.6f}}, {{16.0f, 0.6f, -2.0f}, {3.5f, 1.2f, 0.5f}}, {{-17.0f, 0.6f, 10.0f}, {0.5f, 1.2f, 3.5f}}};
    int coverNumber = 0;
    for (const auto& one : covers) addBlock(game, "Cover " + std::to_string(++coverNumber), yard, cover, one.center, one.size);
    // A deck in the north-west corner, two meters up, with a ramp to it: to shoot from above.
    addBlock(game, "Deck", yard, cover, Vec3{-20.5f, 1.0f, -20.5f}, Vec3{9, 2.0f, 9});
    {
        const float run = 7.0f, rise = 2.0f, thick = 0.3f;
        float slope = std::atan2(rise, run), length = std::sqrt(run * run + rise * rise);
        Vec3 middle{-16.0f + run * 0.5f - std::sin(slope) * thick * 0.5f, rise * 0.5f - std::cos(slope) * thick * 0.5f, -22.5f};
        addBlock(game, "Deck Ramp", yard, wood, middle, Vec3{length, thick, 3.0f}, Quat(-slope, Vec3{0, 0, 1}));
    }
    // A room of walls in the north-east, open to the south and the west: what is in it is not seen from the line.
    addBlock(game, "Room West", yard, wall, Vec3{11.0f, 1.5f, -20.5f}, Vec3{0.6f, 3.0f, 5.0f});
    addBlock(game, "Room South", yard, wall, Vec3{19.0f, 1.5f, -14.0f}, Vec3{8.0f, 3.0f, 0.6f});
    addBlock(game, "Room Middle", yard, wall, Vec3{17.0f, 1.5f, -19.5f}, Vec3{0.6f, 3.0f, 5.0f});

    // Crates that stay, and crates and barrels that a shot or a shoulder moves.
    const Vec3 heavy[] = {{-6.0f, 0.75f, 1.0f}, {-6.0f, 0.75f, -0.6f}, {11.5f, 0.75f, 9.0f}, {2.0f, 0.75f, 7.5f}, {-19.0f, 0.75f, -1.0f}, {20.5f, 0.75f, -8.0f}};
    for (const Vec3& at : heavy) addBlock(game, "Big Crate", yard, wood, at, Vec3{1.5f, 1.5f, 1.5f});
    Entity loose = game.add("Loose");
    const Vec3 light[] = {{-6.0f, 1.9f, 0.2f}, {11.5f, 1.9f, 9.0f}, {3.6f, 0.4f, 7.2f}, {-12.5f, 0.4f, 12.0f}, {17.5f, 0.4f, 3.0f}};
    for (const Vec3& at : light) {
        Entity crate = game.shape("Crate", loose, "cube", wood, at, Vec3{0.8f, 0.8f, 0.8f}, heading(20.0f * at.x));
        Collider solid;
        solid.halfExtents = Vec3{0.4f, 0.4f, 0.4f};
        scene.add(crate, solid);
        RigidBody body;
        body.mass = 8;
        body.rotates = true;
        body.friction = 0.6f;
        scene.add(crate, body);
    }
    const Vec3 drums[] = {{9.0f, 0.55f, -1.0f}, {9.9f, 0.55f, -1.5f}, {-15.5f, 0.55f, 3.0f}, {0.8f, 0.55f, -11.5f}};
    for (const Vec3& at : drums) {
        Entity drum = game.shape("Barrel", loose, "cylinder", barrel, at, Vec3{0.7f, 1.1f, 0.7f});
        Collider solid;
        solid.halfExtents = Vec3{0.33f, 0.55f, 0.33f};
        scene.add(drum, solid);
        RigidBody body;
        body.mass = 14;
        body.rotates = true;
        body.friction = 0.5f;
        scene.add(drum, body);
    }

    // ---- twelve plates: five in the open, near to far; two on the walls; two that go from side to side; three in the room and on the deck
    Entity targets = game.add("Targets");
    const struct {
        Vec3 foot;
        float facing;                   // degrees: 180 looks south, at the line
    } plates[] = {{{-11.0f, 0, 4.0f}, 180},  {{6.0f, 0, 1.0f}, 180},    {{-5.0f, 0, -8.0f}, 180},  {{2.0f, 0, -16.0f}, 180}, {{12.6f, 0, -6.0f}, 180},
                  {{-14.0f, 3.0f, -8.0f}, 180}, {{4.5f, 3.0f, -9.0f}, 180}, {{14.0f, 0, -21.0f}, 180}, {{21.0f, 0, -18.5f}, 135}, {{-21.0f, 2.0f, -22.0f}, 210}};
    int number = 0;
    for (const auto& one : plates) addTarget(game, "Target " + std::to_string(++number), targets, one.foot, one.facing, white, red, steel);
    // The two that move, along the north wall: each on a carriage that goes and comes back.
    for (int i = 0; i < 2; i++) {
        Entity carriage = game.add("Carriage " + std::to_string(i + 1), targets, Vec3{i ? 6.0f : -6.0f, 0, i ? -23.0f : -21.5f});
        kit::Shuttle rail;                      // to the other end of its rail and back
        rail.offset = Vec3{i ? -9.0f : 9.0f, 0, 0};
        rail.seconds = i ? 5.0f : 6.5f;
        scene.add(carriage, rail);
        game.shape("Trolley", carriage, "cube", steel, Vec3{0, 0.08f, 0.05f}, Vec3{0.7f, 0.16f, 0.4f}, Quat{}, false);
        addTarget(game, "Target " + std::to_string(++number), carriage, Vec3{0, 0.1f, 0}, 180, white, red, steel);
    }

    // ---- four machines that hunt: asleep at their places until they see the player. (Each is at the top of the
    //      scene, not in a group: it moves by itself, and its place is the world's.)
    const Vec3 lairs[] = {{-18.0f, 1.6f, -3.0f}, {19.0f, 1.6f, 6.0f}, {-2.5f, 1.6f, -20.0f}, {19.5f, 1.6f, -21.0f}};
    int hunterNumber = 0;
    for (const Vec3& at : lairs) addHunter(game, "Hunter " + std::to_string(++hunterNumber), at, 180, shell, eye, steel);
    // What they leave behind, and what a shot leaves where it lands: prefabs, spawned.
    game.texts["Prefabs/Burst.prefab"] = prefabOf([](Scene& made) {
        Entity burst = effects::create(made, effects::Preset::Explosion, Vec3{0, 0, 0});
        if (Name* name = made.get<Name>(burst)) name->value = "Burst";
        if (kit::Explosion* blast = made.get<kit::Explosion>(burst)) blast->damage = 0, blast->radius = 3, blast->force = 5;
        if (Light* light = made.get<Light>(burst)) light->intensity = 26;
        kit::AudioSource sound;
        sound.clip = "Audio/down.wav";
        sound.comesFromHere = true;
        sound.fullWithin = 12;
        made.add(burst, sound);
    });
    game.texts["Prefabs/Impact.prefab"] = prefabOf([](Scene& made) {
        Entity impact = made.create("Impact");
        made.add(impact, Transform{});
        ParticleEmitter sparks;
        sparks.rate = 0;
        sparks.burstAtStart = 12;
        sparks.lifetimeMin = 0.12f, sparks.lifetimeMax = 0.4f;
        sparks.speedMin = 1.2f, sparks.speedMax = 5.0f;
        sparks.spread = radians(65.0f);
        sparks.gravity = Vec3{0, -9.0f, 0};
        sparks.drag = 0.6f;
        sparks.sizeStart = 0.05f, sparks.sizeEnd = 0.01f;
        sparks.colorStart = Vec4{4.0f, 3.0f, 1.4f, 1};
        sparks.colorEnd = Vec4{1.6f, 0.5f, 0.1f, 0};
        sparks.maxParticles = 16;
        made.add(impact, sparks);
        kit::Lifetime gone;
        gone.seconds = 0.7f;
        made.add(impact, gone);
    });

    // ---- rounds and bandages
    Entity supplies = game.add("Supplies");
    const Vec3 roundsAt[] = {{-20.5f, 2.2f, -18.5f}, {14.0f, 0.2f, -17.0f}, {-9.0f, 0.2f, 7.2f}};
    for (const Vec3& at : roundsAt) {
        Entity box = game.shape("Rounds", supplies, "cube", rounds, at, Vec3{0.42f, 0.26f, 0.28f});
        game.shape("Lid", box, "cube", stripe, Vec3{0, 0.52f, 0}, Vec3{1.02f, 0.1f, 1.02f}, Quat{}, false);
        Collider touch = Collider::sphere(0.8f);
        touch.isTrigger = true;
        scene.add(box, touch);
        kit::Supply gives;
        gives.rounds = 30;
        gives.sound = "Audio/pickup.wav";
        scene.add(box, gives);
    }
    const Vec3 bandagesAt[] = {{15.5f, 0.2f, -1.0f}, {-19.0f, 0.2f, 11.5f}};
    for (const Vec3& at : bandagesAt) {
        Entity box = game.shape("Bandages", supplies, "cube", bandage, at, Vec3{0.36f, 0.26f, 0.36f});
        game.shape("Cross", box, "cube", cross, Vec3{0, 0.51f, 0}, Vec3{0.7f, 0.04f, 0.22f}, Quat{}, false);
        game.shape("Cross Across", box, "cube", cross, Vec3{0, 0.51f, 0}, Vec3{0.22f, 0.04f, 0.7f}, Quat{}, false);
        Collider touch = Collider::sphere(0.8f);
        touch.isTrigger = true;
        scene.add(box, touch);
        kit::Supply gives;
        gives.rounds = 0;
        gives.health = 40;
        gives.sound = "Audio/pickup.wav";
        scene.add(box, gives);
    }

    addShooter(game, Vec3{0, 0.05f, 19.5f}, gunmetal, accent);

    // ---- its keys: the mouse fires (and not Space, which jumps), R loads, Shift runs
    InputMap keys = InputMap::standard();
    for (InputAction& action : keys.actions) {
        if (action.name != "fire") continue;
        action.keys.clear();
        action.buttons = {GamepadButton::RightShoulder};
        action.mouseButtons = {MouseButton::Left};
    }
    InputAction load;
    load.name = "reload";
    load.keys = {Key::R};
    load.buttons = {GamepadButton::X};
    keys.actions.push_back(load);
    InputAction run;
    run.name = "run";
    run.keys = {Key::LeftShift};
    run.buttons = {GamepadButton::LeftStick};
    keys.actions.push_back(run);
    game.inputActions = keys.toText();

    // ---- on the screen: what is down, what is in the rifle, how the player is
    Entity interface = addInterface(game, "score", "DOWN", 16, "Yard cleared!",
                                    {"W A S D   move", "Shift   run", "Space   jump", "Mouse   look, fire", "R   load", "Esc   free the pointer"}, "", 0, 0, "");
    const Vec4 glass{0.04f, 0.045f, 0.055f, 0.62f}, rim{1, 1, 1, 0.16f};
    kit::UIPanel plate;
    plate.anchor = Vec2{1, 1};
    plate.offset = Vec2{-112, -62};
    plate.size = Vec2{184, 76};
    plate.color = glass;
    plate.border = 1.5f;
    plate.borderColor = rim;
    scene.add(game.add("Rounds Panel", interface), plate);
    kit::HudText inIt;
    inIt.text = "{ammo}";
    inIt.anchor = Vec2{1, 1};
    inIt.offset = Vec2{-146, -88};
    inIt.size = 40;
    inIt.color = Vec4{1.0f, 0.62f, 0.28f, 1};
    scene.add(game.add("Rounds", interface), inIt);
    kit::HudText besides;
    besides.text = "/ {ammoReserve}";
    besides.anchor = Vec2{1, 1};
    besides.offset = Vec2{-74, -76};
    besides.size = 20;
    besides.color = Vec4{1, 1, 1, 0.7f};
    scene.add(game.add("Rounds Besides", interface), besides);
    kit::UIBar loading;
    loading.value = "ammoLoading";
    loading.most = 1;
    loading.anchor = Vec2{1, 1};
    loading.offset = Vec2{-112, -34};
    loading.size = Vec2{160, 5};
    loading.color = Vec4{1.0f, 0.62f, 0.28f, 1};
    loading.backColor = Vec4{1, 1, 1, 0.1f};
    scene.add(game.add("Loading", interface), loading);
    kit::UIBar health;
    health.value = "health";
    health.most = 100;
    health.anchor = Vec2{0.5f, 1};
    health.offset = Vec2{0, -34};
    health.size = Vec2{280, 12};
    health.color = Vec4{0.36f, 0.82f, 0.42f, 1};
    health.backColor = Vec4{0, 0, 0, 0.5f};
    scene.add(game.add("Health", interface), health);
    kit::DamageFlash hurt;
    hurt.sound = "Audio/hurt.wav";
    scene.add(game.add("Hurt", interface), hurt);
}

}  // namespace

// ---------------------------------------------------------------- all of them, and writing them

const std::vector<std::string>& names() {
    static const std::vector<std::string> all = {"Car Game", "Plane Game", "Boat Game", "Third Person Game", "Shooter Game", "Online Game"};
    return all;
}

bool build(const std::string& name, Game& game) {
    kit::registerKit();
    if (name == "Car Game") buildCarGame(game);
    else if (name == "Plane Game") buildPlaneGame(game);
    else if (name == "Boat Game") buildBoatGame(game);
    else if (name == "Third Person Game") buildThirdPersonGame(game);
    else if (name == "Shooter Game") buildShooterGame(game);
    else if (name == "Online Game") buildOnlineGame(game);
    else return false;
    game.world.scene.updateTransforms();
    return true;
}

bool write(Game& game, const std::string& directory, std::string* error) {
    auto fail = [&](const std::string& what) {
        if (error) *error = what;
        return false;
    };
    std::error_code code;
    for (const char* folder : {"Assets/Scenes", "Assets/Materials", "Assets/Models", "Assets/Audio"}) {
        fs::create_directories(fs::path(directory) / folder, code);
        if (code) return fail("cannot make " + directory + "/" + folder);
    }
    Project project;
    project.name = game.folder;
    project.directory = fs::absolute(fs::path(directory), code).lexically_normal().string();
    project.startScene = "Scenes/Main.scene";
    project.file = project.filePath();
    if (!project.save(error)) return false;
    if (!text::writeFile(directory + "/template.txt", game.description + "\n")) return fail("cannot write template.txt");
    for (const NamedMaterial& named : game.materials) {
        std::string text = writeMaterial(*named.material);
        // The picture of its color is named in its file: there is no GPU here to hold the picture itself.
        auto picture = game.pictures.find(named.name);
        if (picture != game.pictures.end()) text += "baseColorTexture \"" + picture->second + "\"\n";
        if (!text::writeFile(directory + "/Assets/" + named.material->assetName, text)) return fail("cannot write " + named.material->assetName);
    }
    for (const auto& [file, text] : game.models) {
        if (!text::writeFile(directory + "/Assets/" + file, text)) return fail("cannot write " + file);
    }
    for (const auto& [file, text] : game.texts) {
        fs::create_directories((fs::path(directory) / "Assets" / file).parent_path(), code);
        if (!text::writeFile(directory + "/Assets/" + file, text)) return fail("cannot write " + file);
    }
    for (const auto& [file, bytes] : game.files) {
        fs::create_directories((fs::path(directory) / "Assets" / file).parent_path(), code);
        std::ofstream out(directory + "/Assets/" + file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), long(bytes.size()));
        if (!out) return fail("cannot write " + file);
    }
    if (!game.inputActions.empty() && !text::writeFile(directory + "/Assets/Input.actions", game.inputActions)) return fail("cannot write Input.actions");
    game.world.scene.updateTransforms();
    if (!saveSceneFile(directory + "/Assets/Scenes/Main.scene", game.world.scene, {}, game.world.sceneSettings())) return fail("cannot write the scene");
    return true;
}

}  // namespace starter

// ---------------------------------------------------------------- the tests: each game, played

using namespace molten;

namespace {

namespace fs = std::filesystem;

/// Where the three are written for the tests: the folder MOLTEN_WRITE_TEMPLATES names (the
/// engine's Templates, to make them anew), or one that is thrown away.
std::string starterFolder(const std::string& name) {
    const char* asked = std::getenv("MOLTEN_WRITE_TEMPLATES");
    fs::path root = asked && *asked ? fs::path(asked) : fs::temp_directory_path() / "molten-starter-games";
    return (root / name).string();
}

/// False in a program that has a game's own scripts linked in under the names of the kit's
/// components (its own Follow, Motor, DriveControls): a scene read there gets the game's, and
/// the starter games, which are made of the kit's, cannot be played. The engine's own test
/// program has none, and plays them.
bool kitIsItself() {
    kit::registerKit();
    Scene scratch;
    Entity entity = scratch.create("x");
    for (const char* name : {"DriveControls", "Motor", "Follow"}) {
        const ComponentType* type = ComponentRegistry::instance().find(name);
        if (!type || !type->add) return false;
        type->add(scratch, entity);
    }
    return scratch.get<kit::DriveControls>(entity) && scratch.get<kit::Motor>(entity) && scratch.get<kit::Follow>(entity);
}

/// A starter game built, written, and read back as the player reads it: from its files.
struct Played {
    World world;
    bool isLoaded = false;
    std::string folder;

    explicit Played(const std::string& name) {
        script::registerStandardNodes();
        kit::registerKit();
        folder = starterFolder(name);
        starter::Game game;
        std::string error;
        if (!starter::build(name, game) || !starter::write(game, folder, &error)) {
            std::fprintf(stderr, "%s: %s\n", name.c_str(), error.c_str());
            return;
        }
        world.assetRoot = folder + "/Assets";
        isLoaded = world.loadSceneNow("Scenes/Main.scene");
    }
    void frames(int count) {
        for (int i = 0; i < count; i++) world.advance(1.0 / 60.0);
    }
    void seconds(float time) { frames(int(time * 60.0f)); }
    Vec3 positionOf(const char* name) {
        const Transform* transform = world.scene.get<Transform>(world.find(name));
        return transform ? transform->worldPosition() : Vec3{0, -999, 0};
    }
    void keys(std::initializer_list<Key> held) {
        for (Key key : {Key::W, Key::A, Key::S, Key::D, Key::Q, Key::E, Key::Space, Key::LeftShift, Key::LeftControl, Key::F, Key::B}) {
            bool isHeld = false;
            for (Key wanted : held) isHeld = isHeld || wanted == key;
            world.input.handleKey(key, isHeld);
        }
    }
    int count(const char* prefix) {
        int found = 0;
        world.scene.each<Name>([&](Entity, Name& name) {
            if (name.value.compare(0, std::strlen(prefix), prefix) == 0) found++;
        });
        return found;
    }
};

}  // namespace

TEST(starter_models_face_outward_and_read_back) {
    // Every model of every game: a closed skin whose faces look out (it encloses room), and
    // a file the engine's own reader takes, with as many triangles as its faces make.
    for (const std::string& name : starter::names()) {
        starter::Game game;
        CHECK(starter::build(name, game));
        CHECK(!game.models.empty() && !game.materials.empty() && !game.description.empty());
        for (const auto& [file, text] : game.models) {
            std::optional<MeshData> read = MeshData::parseOBJ(text);
            CHECK(read.has_value());
            if (!read) continue;
            CHECK(read->indices.size() >= 3 && read->indices.size() % 3 == 0);
            CHECK(read->normals.size() == read->positions.size());
            bool isSound = true;
            for (size_t i = 0; i < read->positions.size() && i < read->normals.size(); i++) {
                isSound = isSound && std::isfinite(read->positions[i].x) && std::abs(simd::length(read->normals[i]) - 1.0f) < 0.01f;
            }
            CHECK(isSound);
        }
    }
    // The shapes they are made from.
    starter::Model box;
    box.box(Vec3{-1, -2, -3}, Vec3{1, 2, 3});
    CHECK_NEAR(box.volume(), 2 * 4 * 6, 1e-3);
    starter::Model ring;
    ring.ring(5.0f, 0.5f, 48, 24);
    CHECK_NEAR(ring.volume(), 2 * kPi * kPi * 5.0f * 0.25f, 0.6);          // a torus: 2 pi^2 R r^2
    starter::Model prism;
    prism.loft({{Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{1, 1, 0}, Vec3{0, 1, 0}}, {Vec3{0, 0, 2}, Vec3{1, 0, 2}, Vec3{1, 1, 2}, Vec3{0, 1, 2}}});
    CHECK_NEAR(prism.volume(), 2, 1e-4);
}

TEST(starter_car_game_is_driven_round_its_circuit_to_every_star) {
    if (!kitIsItself()) return;                                  // a game's scripts stand in for the kit here: see kitIsItself
    Played game("Car Game");
    CHECK(game.isLoaded);
    if (!game.isLoaded) return;
    CHECK(game.count("Star ") == 8);
    // It stands on its wheels, on the road, and waits.
    game.seconds(2);
    Vec3 start = game.positionOf("Car");
    CHECK(start.y > 0.3f && start.y < 0.9f);
    const kit::VehicleBody* body = game.world.scene.get<kit::VehicleBody>(game.world.find("Car"));
    CHECK(body && body->isOnGround && body->airspeed() < 0.5f);
    CHECK(game.world.value("stars") == 0);

    // Driven as a person would, star to star: toward the next one, slower when it is off to the side.
    float fastest = 0;
    int frames = 0;
    for (; frames < 60 * 240 && game.world.value("stars") < 8; frames++) {
        // The nearest star still there.
        Vec3 car = game.positionOf("Car"), goal{0, 0, 0};
        float nearest = 1e9f;
        game.world.scene.each<kit::Collectible, Transform>([&](Entity, kit::Collectible&, Transform& place) {
            float away = simd::distance(place.worldPosition(), car);
            if (away < nearest) nearest = away, goal = place.worldPosition();
        });
        // Asked for again every frame: a star that is collected is destroyed, and what is left moves up.
        const Transform* place = game.world.scene.get<Transform>(game.world.find("Car"));
        const kit::VehicleBody* driven = game.world.scene.get<kit::VehicleBody>(game.world.find("Car"));
        Vec3 ahead = rotate(place->rotation, Vec3{0, 0, -1}), right = rotate(place->rotation, Vec3{1, 0, 0});
        Vec3 toGoal = simd::normalize(Vec3{goal.x - car.x, 0, goal.z - car.z});
        float side = simd::dot(toGoal, right), front = simd::dot(toGoal, ahead);
        float speed = driven->speed() * 3.6f;
        fastest = std::max(fastest, speed);
        // Well off to one side, or behind: slowed right down, as a person would, and turned on the spot nearly.
        bool isOffLine = std::abs(side) > 0.35f || front < 0;
        float wanted = isOffLine ? 24.0f : 95.0f;
        std::vector<Key> held;
        if (speed > wanted + 8) held.push_back(Key::S);
        else if (speed < wanted) held.push_back(Key::W);
        if (side > 0.06f || (front < 0 && side >= 0)) held.push_back(Key::D);
        else if (side < -0.06f || front < 0) held.push_back(Key::A);
        for (Key key : {Key::W, Key::A, Key::S, Key::D}) {
            bool isHeld = false;
            for (Key one : held) isHeld = isHeld || one == key;
            game.world.input.handleKey(key, isHeld);
        }
        game.world.advance(1.0 / 60.0);
        if (std::getenv("STARTER_TRACE") && frames % 120 == 0) {
            std::fprintf(stderr, "  car %5.1fs at (%6.1f %5.2f %6.1f) goal (%6.1f %6.1f) side %+.2f front %+.2f %3.0f km/h stars %d\n", double(frames) / 60.0, double(car.x), double(car.y), double(car.z),
                         double(goal.x), double(goal.z), double(side), double(front), double(speed), int(game.world.value("stars")));
        }
    }
    std::fprintf(stderr, "car game: %d stars in %.0f s, fastest %.0f km/h\n", int(game.world.value("stars")), double(frames) / 60.0, double(fastest));
    CHECK(game.world.value("stars") == 8);
    CHECK(game.count("Star ") == 0);
    CHECK(fastest > 60);
    // And it is still on its wheels.
    CHECK(rotate(game.world.scene.get<Transform>(game.world.find("Car"))->rotation, Vec3{0, 1, 0}).y > 0.7f);
    CHECK(game.world.value("won") == 1);
}

TEST(starter_car_game_car_knocks_the_crates_about) {
    if (!kitIsItself()) return;                                  // a game's scripts stand in for the kit here: see kitIsItself
    Played game("Car Game");
    if (!game.isLoaded) return;
    game.seconds(1);
    // Put in front of the wall of crates, and driven into it.
    Entity car = game.world.find("Car");
    Vec3 crates{16, 0, -6};                                       // where the wall of crates stands
    kit::VehicleBody* body = game.world.scene.get<kit::VehicleBody>(car);
    Transform* place = game.world.scene.get<Transform>(car);
    place->position = crates + Vec3{0, 0.62f, 22};
    place->rotation = Quat{};
    game.frames(30);
    float highestBefore = 0;
    game.world.scene.each<RigidBody, Name>([&](Entity, RigidBody& crate, Name& name) {
        if (name.value == "Crate") highestBefore = std::max(highestBefore, crate.position.y);
    });
    CHECK(highestBefore > 3.0f);                                 // a wall four high
    game.keys({Key::W});
    game.seconds(6);
    game.keys({});
    game.seconds(2);
    int moved = 0;
    game.world.scene.each<RigidBody, Transform, Name>([&](Entity, RigidBody&, Transform& at, Name& name) {
        if (name.value == "Crate" && (std::abs(at.worldPosition().z - crates.z) > 1.0f || at.worldPosition().y < 0.3f)) moved++;
    });
    CHECK(moved >= 4);                                           // the wall came down
    CHECK(game.positionOf("Car").z < crates.z + 6);             // and the car went on through it, or nearly
    (void)body;
}

TEST(starter_plane_game_takes_off_and_flies_through_its_rings) {
    if (!kitIsItself()) return;                                  // a game's scripts stand in for the kit here: see kitIsItself
    Played game("Plane Game");
    CHECK(game.isLoaded);
    if (!game.isLoaded) return;
    CHECK(game.count("Ring ") == 8);
    game.seconds(2);
    Entity plane = game.world.find("Plane");
    const kit::VehicleBody* body = game.world.scene.get<kit::VehicleBody>(plane);
    CHECK(body && body->isOnGround && body->airspeed() < 0.5f);
    const float startZ = game.positionOf("Plane").z;

    // Full power down the runway; back on the stick at 110; then toward each ring in turn.
    float runway = 0, highest = 0;
    int frames = 0;
    for (; frames < 60 * 300 && game.world.value("rings") < 8; frames++) {
        Vec3 at = game.positionOf("Plane");
        highest = std::max(highest, at.y);
        body = game.world.scene.get<kit::VehicleBody>(plane);      // again every frame: a ring collected is destroyed, and what is left moves up
        float kmh = body->airspeed() * 3.6f;
        std::vector<Key> held = {Key::LeftShift};
        if (body->isOnGround || at.y < 12) {
            if (kmh > 108) held.push_back(Key::S);
            if (body->isOnGround) runway = startZ - at.z;
        } else {
            // The nearest ring ahead: bank toward it, and hold the height it is at.
            Vec3 goal{0, 0, 0};
            float nearest = 1e9f;
            game.world.scene.each<kit::Collectible, Transform>([&](Entity, kit::Collectible&, Transform& place) {
                float away = simd::distance(place.worldPosition(), at);
                if (away < nearest) nearest = away, goal = place.worldPosition();
            });
            const Transform* place = game.world.scene.get<Transform>(plane);
            Vec3 right = rotate(place->rotation, Vec3{1, 0, 0}), ahead = rotate(place->rotation, Vec3{0, 0, -1});
            Vec3 toGoal = simd::normalize(goal - at);
            if (std::getenv("STARTER_TRACE") && frames % 120 == 0) {
                std::fprintf(stderr, "  plane %5.1fs at (%6.0f %5.0f %6.0f) goal (%6.0f %5.0f %6.0f) %3.0f km/h bank %+.2f rings %d\n", double(frames) / 60.0, double(at.x), double(at.y), double(at.z),
                             double(goal.x), double(goal.y), double(goal.z), double(kmh), double(right.y), int(game.world.value("rings")));
            }
            Vec3 flat = simd::normalize(Vec3{toGoal.x, 0, toGoal.z}), flatAhead = simd::normalize(Vec3{ahead.x, 0, ahead.z});
            float turn = flat.x * flatAhead.z - flat.z * flatAhead.x;        // above zero: the ring is to the left
            float bank = right.y;                                            // above zero: left wing low... the right wing is up
            float wanted = std::clamp(turn * 2.6f, -0.62f, 0.62f);
            if (bank < wanted - 0.06f) held.push_back(Key::A);
            else if (bank > wanted + 0.06f) held.push_back(Key::D);
            float climb = toGoal.y - ahead.y;
            if (climb > 0.03f) held.push_back(Key::S);
            else if (climb < -0.05f) held.push_back(Key::W);
        }
        for (Key key : {Key::W, Key::A, Key::S, Key::D, Key::LeftShift}) {
            bool isHeld = false;
            for (Key wanted : held) isHeld = isHeld || wanted == key;
            game.world.input.handleKey(key, isHeld);
        }
        game.world.advance(1.0 / 60.0);
    }
    std::fprintf(stderr, "plane game: %d rings in %.0f s, off the ground after %.0f m, highest %.0f m\n", int(game.world.value("rings")), double(frames) / 60.0, double(runway),
                 double(highest));
    CHECK(runway > 100 && runway < 800);                         // off well before the runway ends
    CHECK(highest > 60);
    CHECK(game.world.value("rings") == 8);
    CHECK(game.world.value("won") == 1);
}

TEST(starter_boat_game_floats_and_rounds_its_buoys) {
    if (!kitIsItself()) return;                                  // a game's scripts stand in for the kit here: see kitIsItself
    Played game("Boat Game");
    CHECK(game.isLoaded);
    if (!game.isLoaded) return;
    CHECK(game.count("Gate ") == 10 && game.count("Buoy ") == 10);
    // It settles on the water, level; the buoys bob beside it and do not sink or fly off.
    game.seconds(5);
    Entity boat = game.world.find("Boat");
    const Transform* place = game.world.scene.get<Transform>(boat);
    CHECK(place && std::abs(place->position.y) < 0.8f);
    CHECK(rotate(place->rotation, Vec3{0, 1, 0}).y > 0.95f);
    CHECK(game.world.scene.get<kit::Buoyancy>(boat)->isInWater);
    game.world.scene.each<kit::Buoyancy, Transform, Name>([&](Entity, kit::Buoyancy&, Transform& at, Name& name) {
        if (name.value.compare(0, 5, "Buoy ") == 0) CHECK(std::abs(at.worldPosition().y) < 1.6f);
    });

    // Steered from buoy to buoy.
    float fastest = 0;
    int frames = 0;
    for (; frames < 60 * 300 && game.world.value("buoys") < 10; frames++) {
        Vec3 at = game.positionOf("Boat"), goal{0, 0, 0};
        float nearest = 1e9f;
        game.world.scene.each<kit::Collectible, Transform>([&](Entity, kit::Collectible&, Transform& gate) {
            float away = simd::distance(gate.worldPosition(), at);
            if (away < nearest) nearest = away, goal = gate.worldPosition();
        });
        place = game.world.scene.get<Transform>(boat);         // again every frame: a gate collected is destroyed, and what is left moves up
        Vec3 right = rotate(place->rotation, Vec3{1, 0, 0});
        Vec3 toGoal = simd::normalize(Vec3{goal.x - at.x, 0, goal.z - at.z});
        float side = simd::dot(toGoal, right);
        // Off to one side it eases off, as a person would: slower, it turns tighter.
        bool isWide = std::abs(side) > 0.45f || simd::dot(toGoal, rotate(place->rotation, Vec3{0, 0, -1})) < 0;
        bool isFast = game.world.value("knots") > 14;
        if (side > 0.05f) game.keys(isWide && isFast ? std::initializer_list<Key>{Key::D} : std::initializer_list<Key>{Key::W, Key::D});
        else if (side < -0.05f) game.keys(isWide && isFast ? std::initializer_list<Key>{Key::A} : std::initializer_list<Key>{Key::W, Key::A});
        else game.keys({Key::W});
        game.world.advance(1.0 / 60.0);
        fastest = std::max(fastest, game.world.value("knots"));
        if (std::getenv("STARTER_TRACE") && frames % 120 == 0) {
            std::fprintf(stderr, "  boat %5.1fs at (%6.1f %5.2f %6.1f) goal (%6.1f %6.1f) side %+.2f knots %4.1f up %.2f buoys %d\n", double(frames) / 60.0, double(at.x), double(at.y), double(at.z),
                         double(goal.x), double(goal.z), double(side), double(game.world.value("knots")), double(rotate(place->rotation, Vec3{0, 1, 0}).y), int(game.world.value("buoys")));
        }
    }
    std::fprintf(stderr, "boat game: %d buoys in %.0f s, fastest %.0f knots\n", int(game.world.value("buoys")), double(frames) / 60.0, double(fastest));
    CHECK(game.world.value("buoys") == 10);
    CHECK(fastest > 15 && fastest < 40);
    place = game.world.scene.get<Transform>(boat);
    CHECK(rotate(place->rotation, Vec3{0, 1, 0}).y > 0.8f);     // still the right way up
    CHECK(game.world.value("won") == 1);
}

TEST(starter_games_are_templates_a_project_can_start_from) {
    // Written as folders, they are what the New Project page lists; and a project made from
    // one has its scene, its materials and its models.
    for (const std::string& name : starter::names()) Played written(name);
    fs::path root = fs::path(starterFolder("x")).parent_path();
    std::vector<ProjectTemplate> found = projectTemplates(root.string());
    int listed = 0;
    for (const ProjectTemplate& item : found) {
        for (const std::string& name : starter::names()) {
            if (item.name == name) {
                listed++;
                CHECK(!item.description.empty());
            }
        }
    }
    CHECK(listed == int(starter::names().size()));
    fs::path scratch = fs::temp_directory_path() / "molten-starter-project";
    fs::remove_all(scratch);
    std::string error;
    std::optional<Project> made = Project::create(scratch.string(), "My Boat", &error, (root / "Boat Game").string());
    CHECK(made.has_value());
    if (made) {
        CHECK(made->startScene == "Scenes/Main.scene");
        CHECK(fs::exists(fs::path(made->assetsDirectory()) / "Scenes/Main.scene"));
        CHECK(fs::exists(fs::path(made->assetsDirectory()) / "Materials/Sea.material"));
        CHECK(fs::exists(fs::path(made->assetsDirectory()) / "Models/Hull.obj"));
        // The sea is water, and says so in its file.
        std::optional<std::string> sea = text::readFile(made->assetsDirectory() + "/Materials/Sea.material");
        Material read;
        CHECK(sea && readMaterial(*sea, read) && read.isWater && read.baseColor.w < 0.5f);
    }
    fs::remove_all(scratch);
}

TEST(starter_third_person_game_is_walked_to_every_gem) {
    if (!kitIsItself()) return;
    Played game("Third Person Game");
    CHECK(game.isLoaded);
    if (!game.isLoaded) return;
    World& world = game.world;
    CHECK(game.count("Gem ") == 10);
    // The figure is a model with bones, its colors a picture, and what it does is in a file a person can read.
    CHECK(fs::exists(fs::path(game.folder) / "Assets/Models/Figure.glb") && fs::exists(fs::path(game.folder) / "Assets/Models/Figure.png"));
    std::optional<std::string> look = text::readFile(game.folder + "/Assets/Materials/Figure.material");
    CHECK(look && look->find("baseColorTexture \"Models/Figure.png\"") != std::string::npos);
    std::optional<std::string> written = text::readFile(game.folder + "/Assets/Animations/Player.animator");
    std::optional<AnimatorController> states = written ? AnimatorController::parse(*written) : std::nullopt;
    CHECK(states && states->states.size() == 4 && states->transitions.size() == 11);

    // It comes to rest on its square and stands there, breathing.
    game.seconds(1);
    Vec3 start = game.positionOf("Player");
    CHECK_NEAR(start.y, 0.12f, 0.03f);
    auto stateNow = [&] {
        const Animator* animator = world.scene.get<Animator>(world.find("Figure"));
        return animator ? animator->currentState() : std::string();
    };
    CHECK(stateNow() == "Idle");
    // The view has asked for the pointer: the mouse turns it with no button held.
    CHECK(world.input.wantsPointerCaptured());

    // Walked as a person would: toward each place in turn, jumping where there is a gap. The
    // camera has not been turned, so "forward" is north (-z) and the stick's two axes are x and z.
    struct Leg {
        float x, z;
        float jumpWithin;               // this near the place, it jumps (0: it does not)
    };
    const Leg route[] = {{0, 11, 0},           {-4.5f, 8.2f, 0},       {-9, 7.9f, 0},        {-9, 3.2f, 0},     {-11.5f, -3.5f, 0}, {-5.0f, -1, 0},
                         {0, -1, 0},           {8.5f, -1, 0},          {12.2f, -1.0f, 2.45f}, {14.9f, -2.9f, 2.5f}, {15.4f, -6.1f, 2.45f}, {13.4f, -8.9f, 2.6f},
                         {10.6f, -9.4f, 2.0f}, {9, -10.5f, 0},         {6.0f, -10.5f, 0},    {-3.2f, -10.5f, 0}, {-6.0f, -11.5f, 0}};
    bool went = false, jumped = false, fell = false;
    float highest = 0;
    int frames = 0;
    for (const Leg& leg : route) {
        bool hasJumped = false;
        for (int spent = 0; spent < 60 * 20; spent++, frames++) {
            Vec3 at = game.positionOf("Player");
            Vec2 to{leg.x - at.x, leg.z - at.z};
            float away = simd::length(to);
            if (away < 0.4f) break;
            Vec2 push = to / away;
            world.input.setVirtualAxis("moveX", push.x);
            world.input.setVirtualAxis("moveZ", push.y);
            const kit::CharacterMover* mover = world.scene.get<kit::CharacterMover>(world.find("Player"));
            bool jumpsNow = leg.jumpWithin > 0 && !hasJumped && away < leg.jumpWithin && mover && mover->isGrounded;
            world.input.setVirtualButton("jump", jumpsNow);
            hasJumped = hasJumped || jumpsNow;
            world.advance(1.0 / 60.0);
            std::string state = stateNow();
            went = went || state == "Move", jumped = jumped || state == "Jump", fell = fell || state == "Fall";
            highest = std::max(highest, at.y);
            if (std::getenv("STARTER_TRACE") && frames % 30 == 0) {
                std::fprintf(stderr, "  walker %5.1fs at (%6.2f %5.2f %6.2f) to (%5.1f %5.1f) %s gems %d\n", double(frames) / 60.0, double(at.x), double(at.y), double(at.z), double(leg.x),
                             double(leg.z), state.c_str(), int(world.value("gems")));
            }
        }
    }
    world.input.setVirtualAxis("moveX", 0);
    world.input.setVirtualAxis("moveZ", 0);
    world.input.setVirtualButton("jump", false);
    game.seconds(1);
    std::fprintf(stderr, "third person game: %d gems in %.0f s, highest %.1f m\n", int(world.value("gems")), double(frames) / 60.0, double(highest));
    CHECK(world.value("gems") == 10);
    CHECK(game.count("Gem ") == 0);
    CHECK(world.value("won") == 1);
    CHECK(highest > 3.1f);                                       // it has been on the tower
    // Its animator went with it: going, off the ground, falling, and now standing again.
    CHECK(went && jumped && fell);
    CHECK(stateNow() == "Idle");
    // Esc gives the pointer back, for the button that starts over; a click takes it again.
    world.input.handleKey(Key::Escape, true);
    world.advance(1.0 / 60.0);
    world.input.handleKey(Key::Escape, false);
    CHECK(!world.input.wantsPointerCaptured());
    world.input.handleMouseButton(MouseButton::Left, true);
    world.advance(1.0 / 60.0);
    world.input.handleMouseButton(MouseButton::Left, false);
    CHECK(world.input.wantsPointerCaptured());
}

TEST(starter_shooter_game_is_shot_through_to_the_last_plate) {
    if (!kitIsItself()) return;
    Played game("Shooter Game");
    CHECK(game.isLoaded);
    if (!game.isLoaded) return;
    World& world = game.world;
    Scene& scene = world.scene;
    CHECK(game.count("Target ") == 12 && game.count("Hunter ") == 4);
    // What a shot leaves and what a machine leaves are prefabs beside the scene, and its keys are its own.
    CHECK(fs::exists(fs::path(game.folder) / "Assets/Prefabs/Impact.prefab") && fs::exists(fs::path(game.folder) / "Assets/Prefabs/Burst.prefab"));
    game.seconds(0.5f);
    CHECK(world.input.wantsPointerCaptured());
    CHECK(world.value("ammo") == 30 && world.value("ammoReserve") == 90 && world.value("health") == 100);
    // Space jumps, and is not the trigger.
    world.input.handleKey(Key::Space, true);
    game.frames(3);
    world.input.handleKey(Key::Space, false);
    CHECK(game.positionOf("Player").y > 0.1f);
    game.frames(50);
    CHECK(world.value("ammo") == 30);

    int frames = 0, cooldown = 0;
    float lowestHealth = 100;
    Entity player = world.find("Player");
    auto eyeNow = [&] { return scene.get<Transform>(world.find("Camera")); };
    // The view turned toward a point as a hand on a mouse turns it: a few degrees a frame. How far off it was, in degrees.
    auto turnTo = [&](Vec3 point) {
        const Transform* eye = eyeNow();
        Vec3 ahead = safeNormalize(transformDirection(eye->world, Vec3{0, 0, -1})), to = safeNormalize(point - eye->worldPosition());
        float aside = degrees(std::atan2(-to.x, -to.z) - std::atan2(-ahead.x, -ahead.z));
        while (aside > 180) aside -= 360;
        while (aside < -180) aside += 360;
        float up = degrees(std::asin(std::clamp(to.y, -1.0f, 1.0f)) - std::asin(std::clamp(ahead.y, -1.0f, 1.0f)));
        const float most = 7.0f, sensitivity = 0.12f;
        world.input.handleMouseMove(Vec2{0, 0}, Vec2{-std::clamp(aside, -most, most) / sensitivity, -std::clamp(up, -most, most) / sensitivity});
        return std::sqrt(aside * aside + up * up);
    };
    auto step = [&](bool trigger) {
        world.input.handleMouseButton(MouseButton::Left, trigger);
        world.advance(1.0 / 60.0);
        frames++;
        cooldown--;
        lowestHealth = std::min(lowestHealth, world.value("health"));
    };
    // A shot at a time at what `where` finds, until there is nothing more to find or the time is up.
    auto shootAt = [&](const std::function<bool(Vec3&)>& where, float seconds) {
        Vec3 point{0, 0, 0};
        for (int spent = 0; spent < int(seconds * 60) && where(point); spent++) {
            bool isOnIt = turnTo(point) < 0.3f;
            bool trigger = isOnIt && cooldown <= 0;
            if (trigger) cooldown = 11;
            step(trigger);
        }
        step(false);
    };
    // A machine that can be seen from here: the nearest.
    auto hunterInSight = [&](Vec3& at) {
        Vec3 eye = eyeNow()->worldPosition();
        float nearest = 1e9f;
        scene.each<kit::Hunter, Transform>([&](Entity entity, kit::Hunter&, Transform& place) {
            Vec3 there = place.worldPosition();
            float away = simd::length(there - eye);
            if (away > 26 || away >= nearest) return;
            auto seen = world.physics.raycast(scene, Ray{eye, (there - eye) / away}, away + 1, 0xFFFFFFFFu, player);
            if (seen && seen->entity == entity) nearest = away, at = there;
        });
        return nearest < 1e8f;
    };
    // A plate still standing that can be seen from here.
    auto plateInSight = [&](int number, Vec3& at) {
        Entity target = world.find("Target " + std::to_string(number));
        const kit::ShootingTarget* state = scene.get<kit::ShootingTarget>(target);
        if (!state || state->isDown) return false;
        bool found = false;
        scene.each<Name, Transform>([&](Entity entity, Name& name, Transform& place) {
            if (name.value == "Plate" && Physics::isUnder(scene, entity, target)) at = place.worldPosition(), found = true;
        });
        if (!found) return false;
        Vec3 eye = eyeNow()->worldPosition();
        float away = simd::length(at - eye);
        auto seen = world.physics.raycast(scene, Ray{eye, (at - eye) / away}, away + 0.5f, 0xFFFFFFFFu, player);
        return seen && Physics::isUnder(scene, seen->entity, target);
    };
    auto clearWhatIsSeen = [&] {
        Vec3 at{0, 0, 0};
        for (int again = 0; again < 8 && hunterInSight(at); again++) shootAt(hunterInSight, 6);
        for (int number = 1; number <= 12; number++) {
            if (!plateInSight(number, at)) continue;
            shootAt([&](Vec3& point) { return plateInSight(number, point); }, 6);
            for (int again = 0; again < 8 && hunterInSight(at); again++) shootAt(hunterInSight, 6);
        }
    };
    auto walkTo = [&](Vec3 place) {
        for (int spent = 0; spent < 60 * 25; spent++) {
            Vec3 at = game.positionOf("Player");
            if (simd::length(Vec2{place.x - at.x, place.z - at.z}) < 0.5f) break;
            Vec3 threat{0, 0, 0};
            if (hunterInSight(threat)) {
                // Something is coming: stood still for, and shot.
                world.input.handleKey(Key::W, false);
                world.input.handleKey(Key::LeftShift, false);
                for (int still = 0; still < 8; still++) step(false);
                shootAt(hunterInSight, 6);
                continue;
            }
            float off = turnTo(Vec3{place.x, eyeNow()->worldPosition().y, place.z});
            world.input.handleKey(Key::W, off < 25);
            world.input.handleKey(Key::LeftShift, off < 8);
            step(false);
            if (std::getenv("STARTER_TRACE") && frames % 60 == 0) {
                std::fprintf(stderr, "  shooter %5.1fs at (%6.2f %5.2f %6.2f) to (%5.1f %5.1f) down %d health %3.0f rounds %2.0f+%3.0f\n", double(frames) / 60.0, double(at.x), double(at.y), double(at.z),
                             double(place.x), double(place.z), int(world.value("score")), double(world.value("health")), double(world.value("ammo")), double(world.value("ammoReserve")));
            }
        }
        world.input.handleKey(Key::W, false);
        world.input.handleKey(Key::LeftShift, false);
        for (int still = 0; still < 12; still++) step(false);
    };

    // From the line first; then round the yard, to where the rest can be seen from.
    clearWhatIsSeen();
    int fromTheLine = int(world.value("score"));
    const Vec3 route[] = {{7.5f, 0, 16.8f}, {9.5f, 0, 9.0f},     {11.2f, 0, 2.0f},    {11.2f, 0, -10.0f}, {13.5f, 0, -15.0f}, {20.5f, 0, -15.6f},
                          {13.5f, 0, -15.0f}, {12.5f, 0, -12.0f}, {-4.0f, 0, -13.0f}, {-10.0f, 0, -14.0f}, {-12.0f, 0, 0.0f}};
    for (const Vec3& place : route) {
        if (world.value("score") >= 16) break;
        walkTo(place);
        clearWhatIsSeen();
    }
    game.seconds(1);
    const kit::Weapon* rifle = scene.get<kit::Weapon>(world.find("Rifle"));
    std::fprintf(stderr, "shooter game: %d of 16 down in %.0f s (%d from the line), %d shots, %d hits, lowest health %.0f\n", int(world.value("score")), double(frames) / 60.0, fromTheLine,
                 rifle ? rifle->shots : 0, rifle ? rifle->hits : 0, double(lowestHealth));
    CHECK(fromTheLine >= 4);
    CHECK(world.value("score") == 16);
    CHECK(world.value("won") == 1);
    CHECK(game.count("Hunter ") == 0);
    CHECK(world.value("health") > 0);
    // Every round is accounted for: what was fired came out of the magazine and what was carried.
    CHECK(rifle && rifle->shots > 20 && rifle->hits >= 24);
    int boxesLeft = 0;
    scene.each<kit::Supply>([&](Entity, kit::Supply& supply) { boxesLeft += supply.rounds > 0 ? 1 : 0; });
    if (rifle) CHECK(rifle->shots + rifle->inMagazine + rifle->reserve == 120 + 30 * (3 - boxesLeft));
}

TEST(starter_online_game_is_played_by_three_at_once) {
    if (!kitIsItself()) return;
    Played hosting("Online Game");
    CHECK(hosting.isLoaded);
    if (!hosting.isLoaded) return;
    // The player is not in the scene: it is the prefab everyone gets one of.
    CHECK(fs::exists(fs::path(hosting.folder) / "Assets/Prefabs/Player.prefab"));
    CHECK(!hosting.world.find("Player").isValid() && hosting.count("Gem ") == 10 && hosting.count("Spawn ") == 6);
    // Alone and asked nothing yet, it shows its menu, and the pointer is out for it.
    hosting.frames(2);
    kit::NetworkSession* session = kit::sessionOf(hosting.world);
    CHECK(session && !session->isOnline() && hosting.world.input.isPointerFree());

    // Two more machines read the same folder, and join the first.
    World anna, bruno;
    for (World* machine : {&anna, &bruno}) {
        machine->assetRoot = hosting.folder + "/Assets";
        CHECK(machine->loadSceneNow("Scenes/Main.scene"));
    }
    MemoryNetwork network;
    std::unique_ptr<NetTransport> end = network.open(7777);
    NetAddress hostAt = end->address();
    session->listenOn(hosting.world, std::move(end), true);
    kit::sessionOf(anna)->joinOn(anna, network.open(5001), hostAt);
    kit::sessionOf(bruno)->joinOn(bruno, network.open(5002), hostAt);
    std::vector<World*> machines = {&hosting.world, &anna, &bruno};
    auto run = [&](float seconds) {
        for (int i = 0; i < int(seconds * 60); i++) {
            network.advance(1.0 / 60.0);
            for (World* machine : machines) machine->advance(1.0 / 60.0);
        }
    };
    auto placeOf = [](World& world, const char* name) {
        const Transform* place = world.scene.get<Transform>(world.find(name));
        return place ? place->worldPosition() : Vec3{0, -999, 0};
    };
    auto stateOf = [](World& world, const char* player) {
        std::string state;
        Entity root = world.find(player);
        world.scene.each<Animator, Transform>([&](Entity entity, Animator& animator, Transform&) {
            if (Physics::isUnder(world.scene, entity, root)) state = animator.currentState();
        });
        return state;
    };
    run(1.0);
    for (World* machine : machines) {
        kit::NetworkSession* theirs = kit::sessionOf(*machine);
        CHECK(theirs && theirs->isOnline() && theirs->playerCount() == 3);
        CHECK(machine->find("Player").isValid());
        CHECK(!machine->input.isPointerFree() && machine->input.wantsPointerCaptured());       // in the game, the view has the pointer
    }
    CHECK(anna.find("Player 1").isValid() && anna.find("Player 3").isValid() && bruno.find("Player 2").isValid());
    // Each stands at a place of their own about the square, and stands there on every machine.
    Vec3 annaAt = placeOf(anna, "Player");
    CHECK(simd::length(annaAt - placeOf(hosting.world, "Player")) > 1.5f);
    CHECK(simd::length(placeOf(hosting.world, "Player 2") - annaAt) < 0.05f && simd::length(placeOf(bruno, "Player 2") - annaAt) < 0.05f);
    CHECK(stateOf(bruno, "Player 2") == "Idle");

    // Anna walks to the first gem. On Bruno's machine her figure is seen going; the server counts the gem for everyone.
    bool seenGoing = false;
    for (int frame = 0; frame < 60 * 8 && hosting.world.value("gems") < 1; frame++) {
        Vec3 at = placeOf(anna, "Player");
        Vec2 to{0 - at.x, 11.0f - at.z};
        Vec2 push = to / std::max(simd::length(to), 0.01f);
        anna.input.setVirtualAxis("moveX", push.x);
        anna.input.setVirtualAxis("moveZ", push.y);
        run(1.0f / 60.0f);
        seenGoing = seenGoing || stateOf(bruno, "Player 2") == "Move";
    }
    anna.input.setVirtualAxis("moveX", 0);
    anna.input.setVirtualAxis("moveZ", 0);
    run(0.6f);
    CHECK(seenGoing);
    for (World* machine : machines) {
        CHECK(machine->value("gems") == 1);
        CHECK(!machine->find("Gem 1").isValid() && machine->find("Gem 2").isValid());
    }
    CHECK(simd::length(placeOf(bruno, "Player 2") - placeOf(anna, "Player")) < 0.05f);
    CHECK(stateOf(bruno, "Player 2") == "Idle");
    // The host shoves a crate; the others see it where the host has it, and cannot shove it themselves.
    Entity crate = hosting.world.find("Crate");
    CHECK(hosting.world.scene.get<RigidBody>(crate) && !anna.scene.get<RigidBody>(anna.find("Crate")));
    hosting.world.scene.get<RigidBody>(crate)->velocity = Vec3{3, 0, 0};
    run(1.0f);
    CHECK(simd::length(placeOf(anna, "Crate") - placeOf(hosting.world, "Crate")) < 0.1f);
    CHECK(placeOf(anna, "Crate").x > -3.4f);
}
