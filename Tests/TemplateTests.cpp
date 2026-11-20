// The project templates, played without a screen: each is loaded as the player would
// load it and driven with the keys a person would press. What is checked is that the
// game can be played and won or lost, which is what a template is for.
#include "Test.hpp"

#include "Core/Paths.hpp"
#include "Core/World.hpp"
#include "Kit/Kit.hpp"
#include "Script/Runtime.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>

using namespace molten;

namespace {

std::string templateAssets(const char* name) {
    // The example projects are not part of the engine: they are beside it (or where
    // MOLTEN_PROJECTS says), written there by Tools/make_templates.py.
    const char* given = std::getenv("MOLTEN_PROJECTS");
    for (std::string root : {std::string(given ? given : "../MoltenProjects"), std::string("../MoltenProjects"), std::string("../../MoltenProjects"),
                             findRootDirectory() + "/../MoltenProjects", findRootDirectory() + "/Templates"}) {
        if (std::filesystem::exists(root + "/" + name)) return root + "/" + name + "/Assets";
    }
    return std::string("../MoltenProjects/") + name + "/Assets";
}

struct Play {
    World world;
    explicit Play(const char* name) {
        script::registerStandardNodes();
        kit::registerKit();
        world.assetRoot = templateAssets(name);
        isLoaded = world.loadSceneNow("Scenes/Main.scene");
        // Straight into the race: the title screen waits for a player the tests do not have.
        if (!keepsTitle) setField("GameMenu", "showsTitle", "0");
    }
    explicit Play(const char* name, bool withTitle) : keepsTitle(withTitle) {
        script::registerStandardNodes();
        kit::registerKit();
        world.assetRoot = templateAssets(name);
        isLoaded = world.loadSceneNow("Scenes/Main.scene");
        if (!keepsTitle) setField("GameMenu", "showsTitle", "0");
    }
    bool keepsTitle = false;
    /// A field of every component of a type the game's scripts declare (the tests do not know their types).
    void setField(const char* typeName, const char* fieldName, const char* text) {
        const ComponentType* type = ComponentRegistry::instance().find(typeName);
        const Field* field = type ? type->findField(fieldName) : nullptr;
        if (!field) return;
        world.scene.each<Name>([&](Entity entity, Name&) {
            if (void* component = type->get(world.scene, entity)) field->setValueText(component, text);
        });
    }
    bool isLoaded = false;
    void frames(int count) {
        for (int i = 0; i < count; i++) world.advance(1.0 / 60.0);
    }
    Vec3 positionOf(const char* name) {
        const Transform* transform = world.scene.get<Transform>(world.find(name));
        return transform ? transform->worldPosition() : Vec3{0, -999, 0};
    }
    /// Holds the keys that take `who` toward a point on the ground, for one frame.
    void steer(const char* who, Vec3 goal, float deadZone = 0.15f) {
        Vec3 from = positionOf(who);
        world.input.handleKey(Key::A, goal.x < from.x - deadZone);
        world.input.handleKey(Key::D, goal.x > from.x + deadZone);
        world.input.handleKey(Key::W, goal.z < from.z - deadZone);
        world.input.handleKey(Key::S, goal.z > from.z + deadZone);
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

// ---------------------------------------------------------------- what the games needed
#include "Kit/Effects.hpp"
#include "Project/Project.hpp"
#include "Rendering/Canvas.hpp"
#include "Scene/SceneFile.hpp"
TEST(templates_are_listed_and_a_project_can_start_from_one) {
    std::string templates = std::filesystem::path(templateAssets("Driving")).parent_path().parent_path().string();
    std::vector<ProjectTemplate> found = projectTemplates(templates);
    // The example projects, by name: the driving game, and the showcase of everything.
    const ProjectTemplate* driving = nullptr;
    for (const ProjectTemplate& candidate : found) {
        if (candidate.name == "Driving") driving = &candidate;
    }
    CHECK(!found.empty() && driving && !driving->description.empty());

    namespace fs = std::filesystem;
    fs::path scratch = fs::temp_directory_path() / "molten-template-test";
    fs::remove_all(scratch);
    fs::create_directories(scratch);
    std::string error;
    std::optional<Project> project = Project::create(scratch.string(), "My Racer", &error, driving ? driving->directory : "");
    CHECK(project.has_value());
    if (project) {
        CHECK(project->name == "My Racer");
        CHECK(project->startScene == "Scenes/Main.scene");
        CHECK(fs::exists(fs::path(project->assetsDirectory()) / "Scenes/Main.scene"));
        CHECK(fs::exists(fs::path(project->assetsDirectory()) / "Scripts/LapTimer.cpp"));
        CHECK(fs::exists(fs::path(project->assetsDirectory()) / "Models/Fiat500/fiat500.glb"));
        CHECK(!fs::exists(fs::path(project->directory) / "template.txt"));     // the copy is a project, not a template
        CHECK(fs::exists(project->filePath()));
        // And the copy plays: its car stands on the road.
        World world;
        world.assetRoot = project->assetsDirectory();
        CHECK(world.loadSceneNow(project->startScene));
        for (int i = 0; i < 120; i++) world.advance(1.0 / 60.0);
        const Transform* car = world.scene.get<Transform>(world.find("Car"));
        CHECK(car && car->worldPosition().y > 0.1f && car->worldPosition().y < 1.0f);
    }
    fs::remove_all(scratch);
}
TEST(a_prefab_is_part_of_a_scene_with_only_what_it_uses) {
    Scene scene;
    auto red = std::make_shared<Material>();
    auto blue = std::make_shared<Material>();
    std::vector<NamedMaterial> materials = {{"Red", red}, {"Blue", blue}};
    Entity ship = scene.create("Ship");
    scene.add(ship, Transform{});
    scene.add(ship, MeshRenderer{nullptr, blue});
    Entity gun = scene.create("Gun");
    Transform gunTransform;
    gunTransform.parent = ship;
    gunTransform.position = Vec3{0, 0, -1};
    scene.add(gun, gunTransform);
    Entity other = scene.create("Other");
    scene.add(other, Transform{});
    scene.add(other, MeshRenderer{nullptr, red});

    std::vector<Entity> part = {ship, gun};
    std::string text = writeScene(scene, materials, RenderSettings{}, &part);
    CHECK(text.find("settings") == std::string::npos);
    CHECK(text.find("\"Blue\"") != std::string::npos);
    CHECK(text.find("\"Red\"") == std::string::npos);
    CHECK(text.find("\"Other\"") == std::string::npos);

    // Spawned twice, it is there twice, each gun under its own ship.
    World world;
    Entity first = world.spawnFromText(text, Vec3{5, 0, 0});
    Entity second = world.spawnFromText(text, Vec3{-5, 0, 0});
    CHECK(world.scene.isAlive(first) && world.scene.isAlive(second) && first != second);
    int guns = 0;
    world.scene.each<Name, Transform>([&](Entity, Name& name, Transform& transform) {
        if (name.value != "Gun") return;
        guns++;
        CHECK(transform.parent == first || transform.parent == second);
        CHECK(std::abs(std::abs(transform.worldPosition().x) - 5) < 1e-4f);
    });
    CHECK(guns == 2);
    const MeshRenderer* renderer = world.scene.get<MeshRenderer>(first);
    CHECK(renderer && renderer->material && renderer->material != blue);      // its own copy, read from the text
}

TEST(restarting_returns_to_the_scene_as_it_was_given) {
    kit::registerKit();
    World world;
    std::string text = "molten-scene 1\nentity \"Faller\"\n  transform 0 3 0 0 0 0 1 1 1 1\n  component \"RestartWhenBelow\"\n    field height \"-1\"\n"
                       "  component \"Mover\"\n    field velocity \"0 -10 0\"\n    field isLocal \"0\"\n";
    int loads = 0;
    world.onSceneLoaded = [&] { loads++; };
    CHECK(world.loadSceneText("Unsaved", text));
    world.setValue("score", 7);
    bool wentBack = false;
    float lowest = 99;
    for (int i = 0; i < 120; i++) {
        float before = world.scene.get<Transform>(world.find("Faller"))->position.y;
        world.advance(1.0 / 60.0);
        float after = world.scene.get<Transform>(world.find("Faller"))->position.y;
        lowest = std::min(lowest, after);
        if (after > before + 1) wentBack = true;
    }
    CHECK(wentBack);                        // no file anywhere: it came back from the text
    CHECK(lowest < -0.9f && lowest > -1.5f);
    CHECK(loads >= 2);
    CHECK(world.value("score") == 0);
    CHECK(world.sceneName() == "Unsaved");
}

TEST(effects_are_made_ready_to_use) {
    kit::registerKit();
    World world;
    Entity fire = effects::create(world.scene, effects::Preset::Fire, Vec3{0, 0, 0});
    Entity smoke = effects::create(world.scene, effects::Preset::Smoke, Vec3{3, 0, 0});
    Entity blast = effects::create(world.scene, effects::Preset::Explosion, Vec3{0, 0.5f, 4});
    // Something near the explosion, something far.
    auto crate = [&](Vec3 position) {
        Entity entity = world.scene.create("Crate");
        Transform transform;
        transform.position = position;
        world.scene.add(entity, transform);
        Collider collider;
        collider.shape = ColliderShape::Sphere;
        collider.radius = 0.4f;
        world.scene.add(entity, collider);
        RigidBody body;
        body.gravityScale = 0;
        world.scene.add(entity, body);
        return entity;
    };
    Entity near = crate(Vec3{1.5f, 0.5f, 4}), far = crate(Vec3{30, 0.5f, 4});
    world.sceneSettings().wind = Vec3{2, 0, 0};

    float fireLightAtStart = world.scene.get<Light>(fire)->intensity;
    bool flickered = false;
    for (int i = 0; i < 45; i++) {
        world.advance(1.0 / 60.0);
        if (std::abs(world.scene.get<Light>(fire)->intensity - fireLightAtStart) > 0.2f) flickered = true;
    }
    CHECK(flickered);
    CHECK(world.scene.get<ParticleEmitter>(fire)->particles.size() > 20);
    CHECK(world.scene.get<RigidBody>(near)->velocity.x > 1.0f);        // thrown away from the blast
    CHECK(simd::length(world.scene.get<RigidBody>(far)->velocity) < 1e-4f);
    CHECK(world.scene.isAlive(blast));
    CHECK(world.scene.get<Light>(blast)->intensity < 1.0f);            // the flash is over

    // Smoke goes with the wind; the fire's flames mostly do not.
    for (int i = 0; i < 120; i++) world.advance(1.0 / 60.0);
    float drift = 0;
    const ParticleEmitter* emitter = world.scene.get<ParticleEmitter>(smoke);
    for (const Particle& particle : emitter->particles) drift += particle.position.x - 3.0f;
    CHECK(!emitter->particles.empty() && drift / float(emitter->particles.size()) > 0.5f);
    CHECK(!world.scene.isAlive(blast));                                // and the explosion cleaned up after itself
    for (int i = 0; i < int(effects::Preset::Count); i++) CHECK(effects::emitter(effects::Preset(i)).maxParticles > 0);
}

TEST(canvas_area_moves_and_clips_what_is_drawn_in_it) {
    Canvas canvas;
    canvas.begin(Vec2{1000, 600});
    canvas.rect(Vec2{0, 0}, Vec2{10, 10}, Vec4{1, 1, 1, 1});
    canvas.beginArea(Vec2{200, 100}, Vec2{400, 300});
    CHECK(canvas.size().x == 400 && canvas.size().y == 300);
    canvas.rect(Vec2{0, 0}, Vec2{10, 10}, Vec4{1, 1, 1, 1});
    canvas.endArea();
    CHECK(canvas.size().x == 1000);
    canvas.rect(Vec2{0, 0}, Vec2{10, 10}, Vec4{1, 1, 1, 1});
    CHECK(canvas.batches().size() == 3);
    if (canvas.batches().size() == 3) {
        const Canvas::Batch& inside = canvas.batches()[1];
        CHECK(inside.isClipped && inside.clipMin.x == 200 && inside.clipMin.y == 100 && inside.clipMax.x == 600 && inside.clipMax.y == 400);
        CHECK(canvas.vertices()[inside.firstVertex].position.x == 200 && canvas.vertices()[inside.firstVertex].position.y == 100);
        CHECK(!canvas.batches()[2].isClipped);
        CHECK(canvas.vertices()[canvas.batches()[2].firstVertex].position.x == 0);
    }
}

TEST(a_material_is_a_file_that_scenes_name) {
    Material brick;
    brick.baseColor = Vec4{0.6f, 0.2f, 0.1f, 1};
    brick.roughness = 0.85f;
    brick.isUnlit = true;
    brick.isDoubleSided = true;
    std::string text = writeMaterial(brick);
    CHECK(text.compare(0, 17, "molten-material 1") == 0);
    CHECK(text.find("shader unlit") != std::string::npos);
    Material read;
    read.assetName = "Materials/Brick.material";
    CHECK(readMaterial(text, read));
    CHECK(read.isUnlit && read.isDoubleSided && read.roughness == 0.85f && read.baseColor.x == 0.6f);
    CHECK(read.assetName == "Materials/Brick.material");           // reading keeps whose file it is
    CHECK(writeMaterial(read) == text);
    CHECK(!readMaterial("molten-scene 1\n", read));

    // In a scene, a mesh that uses an asset says only its name; a material of the scene's own is written out.
    Scene scene;
    auto asset = std::make_shared<Material>(read);
    auto own = std::make_shared<Material>();
    Entity a = scene.create("A"), b = scene.create("B");
    scene.add(a, Transform{});
    scene.add(a, MeshRenderer{nullptr, asset});
    scene.add(b, Transform{});
    scene.add(b, MeshRenderer{nullptr, own});
    std::string written = writeScene(scene, {{"Own", own}, {"Brick", asset}}, RenderSettings{});
    CHECK(written.find("materialAsset \"Materials/Brick.material\"") != std::string::npos);
    CHECK(written.find("material \"Own\"") != std::string::npos);
    CHECK(written.find("material \"Brick\"") == std::string::npos);

    // Read back, the asset is asked for by name, once per mesh that uses it.
    Scene loaded;
    std::vector<NamedMaterial> materials;
    RenderSettings settings;
    SceneAssets assets;
    std::vector<std::string> asked;
    assets.material = [&](const std::string& name) {
        asked.push_back(name);
        return asset;
    };
    CHECK(readScene(written, loaded, materials, settings, assets));
    CHECK(asked.size() == 1 && asked[0] == "Materials/Brick.material");
    CHECK(materials.size() == 1 && materials[0].name == "Own");
    CHECK(loaded.get<MeshRenderer>(loaded.find("A"))->material == asset);
    CHECK(writeScene(loaded, materials, settings) == written);
    // With nobody to ask, the name survives a save.
    Scene blind;
    CHECK(readScene(written, blind, materials, settings, SceneAssets{}));
    CHECK(writeScene(blind, materials, settings) == written);
}

TEST(input_actions_are_named_saved_and_read) {
    InputMap standard = InputMap::standard();
    std::string text = standard.toText();
    CHECK(text.find("axis \"moveX\" negative A Left positive D Right gamepad LeftX") != std::string::npos);
    CHECK(text.find("button \"jump\" keys Space gamepad A") != std::string::npos);
    CHECK(InputMap::fromText(text).toText() == text);

    Input input;                            // starts with the standard actions
    CHECK(input.hasAction("jump") && input.hasAction("moveX") && !input.hasAction("dance"));
    input.handleKey(Key::Left, true);
    CHECK(input.axis("moveX") == -1);
    input.handleKey(Key::Left, false);
    input.handleKey(Key::W, true);
    CHECK(input.axis("moveZ") == -1);

    // A project that says otherwise is obeyed.
    InputMap custom = InputMap::fromText("molten-input 1\nbutton \"jump\" keys J Enter mouse Right\naxis \"moveX\" negative Q positive E\n");
    input.apply(custom);
    CHECK(!input.hasAction("moveZ"));
    input.handleKey(Key::J, true);
    CHECK(input.isDown("jump") && input.wasPressed("jump"));
    input.handleKey(Key::E, true);
    CHECK(input.axis("moveX") == 1);
    CHECK(std::string(keyName(Key::Num7)) == "7" && keyByName("esc") == Key::Escape && keyByName("f5") == Key::F5 && keyByName("nope") == Key::Unknown);
}

#include "Kit/Vehicles.hpp"

TEST(template_driving_can_be_driven) {
    Play play("Driving");
    CHECK(play.isLoaded);
    play.frames(90);
    kit::VehicleBody* car = play.world.scene.get<kit::VehicleBody>(play.world.find("Car"));
    kit::Motor* motor = play.world.scene.get<kit::Motor>(play.world.find("Car"));
    kit::Wheel* front = play.world.scene.get<kit::Wheel>(play.world.find("Wheel FL"));
    kit::Wheel* rear = play.world.scene.get<kit::Wheel>(play.world.find("Wheel RR"));
    CHECK(car && front && rear);        // (the Motor is the engine's own, or the game's script of that name: then `motor` is null)
    if (!car || !front || !rear) return;
    CHECK(front->state.isOnGround && rear->state.isOnGround);
    CHECK(std::abs(car->speed()) < 0.2f);
    // It stands on its tires: the model, which hangs below the car's middle, has its feet on the road.
    const Transform* model = play.world.scene.get<Transform>(play.world.find("Model"));
    CHECK(model && std::abs(model->worldPosition().y) < 0.06f);

    // Foot down along the start straight.
    float startX = car->body.position.x;
    play.world.input.handleKey(Key::W, true);
    float deepest = 0, shallowest = 1;
    for (int frame = 0; frame < 60 * 3; frame++) {
        play.frames(1);
        deepest = std::max(deepest, front->state.compression);
        shallowest = std::min(shallowest, front->state.compression);
    }
    CHECK(play.world.value("speed") > 45);
    CHECK(car->body.position.x > startX + 18);                          // along the start straight
    CHECK(deepest - shallowest > 0.003f);                               // the nose rose as it pulled away
    CHECK(car->body.up().y > 0.8f);
    // The wheels of the model follow the simulation.
    const Transform* wheel = play.world.scene.get<Transform>(play.world.find("Wheel FL"));
    CHECK(wheel && wheel->position.x < -0.6f && wheel->position.y < 0);
    // Another engine, on the move.
    play.world.input.handleKey(Key::Num4, true);
    play.frames(2);
    if (motor) CHECK(motor->engine == "v8");
    play.world.input.handleKey(Key::Num4, false);
    // Brake as hard as the pedal goes, steering a little: it stops straight, in a distance a car stops in,
    // without spinning. Locked wheels would slide on for twice as far and turn it round.
    play.world.input.handleKey(Key::W, false);
    play.world.input.handleKey(Key::S, true);
    play.world.input.handleKey(Key::D, true);
    play.frames(20);
    play.world.input.handleKey(Key::D, false);
    float brakedFrom = car->body.position.x;
    float fastest = car->speed();
    int frames = 0;
    while (car->speed() > 0.5f && frames < 60 * 6) {
        play.frames(1);
        frames++;
    }
    play.world.input.handleKey(Key::S, false);
    float distance = car->body.position.x - brakedFrom;
    CHECK(distance < fastest * fastest / (2 * 6.0f) + 4.0f);            // better than 0.6 g, all the way down
    Vec3 nose = rotate(car->body.orientation, Vec3{0, 0, -1});
    CHECK(nose.x > 0.75f);                                              // still pointing along the straight, give or take the steering it was given
    CHECK(simd::length(car->body.angularVelocity) < 0.4f);              // and not turning round
    // The read-outs the interface shows.
    // (They come from the project's own scripts, which are part of this program only when it is built with them.)
    if (ComponentRegistry::instance().find("LapTimer")) {
        CHECK(play.world.hasValue("revs") && play.world.hasValue("gearIndex") && play.world.hasValue("lapTime"));
    }
}

TEST(template_driving_laps_its_circuit) {
    Play play("Driving");
    CHECK(play.isLoaded);
    kit::VehicleBody* car = play.world.scene.get<kit::VehicleBody>(play.world.find("Car"));
    CHECK(car != nullptr);
    if (!car) return;
    // The middle of the road, from the scene.
    std::vector<Vec3> line;
    for (int number = 1;; number++) {
        const Transform* point = play.world.scene.get<Transform>(play.world.find("Waypoint " + std::to_string(number)));
        if (!point) break;
        line.push_back(point->worldPosition());
    }
    CHECK(line.size() > 40);
    if (line.size() < 8) return;
    int count = int(line.size());
    auto bendAt = [&](int k) {
        Vec3 a = line[size_t((k + 1) % count)] - line[size_t(k % count)], b = line[size_t((k + 3) % count)] - line[size_t((k + 2) % count)];
        return std::abs(std::atan2(a.x * b.z - a.z * b.x, a.x * b.x + a.z * b.z)) / 12.0f;       // radians per meter
    };
    play.frames(60);
    // A driver that looks ahead along the middle of the road, slows for what it sees coming and steers at it.
    float farthestOff = 0, fastest = 0;
    int frames = 0;
    for (; frames < 60 * 90 && play.world.value("lap") < 2.5f; frames++) {
        Vec3 at = car->body.position;
        int nearest = 0;
        float best = 1e9f;
        for (int k = 0; k < count; k++) {
            float away = simd::length(Vec3{line[size_t(k)].x - at.x, 0, line[size_t(k)].z - at.z});
            if (away < best) best = away, nearest = k;
        }
        farthestOff = std::max(farthestOff, best);
        float speed = car->speed();
        fastest = std::max(fastest, speed);
        Vec3 goal = line[size_t((nearest + 2 + int(speed / 9.0f)) % count)];
        Vec3 ahead = rotate(car->body.orientation, Vec3{0, 0, -1}), toGoal = safeNormalize(Vec3{goal.x - at.x, 0, goal.z - at.z});
        float turn = std::atan2(ahead.x * toGoal.z - ahead.z * toGoal.x, ahead.x * toGoal.x + ahead.z * toGoal.z);
        float sharpest = 1e-4f;
        for (int k = 0; k < 7; k++) sharpest = std::max(sharpest, bendAt(nearest + k));
        float wanted = std::clamp(std::sqrt(7.5f / sharpest), 9.0f, 30.0f);
        kit::Controls controls;
        controls.steer = std::clamp(turn * 2.2f, -1.0f, 1.0f);
        controls.throttle = speed < wanted - 0.5f ? 1.0f : 0.0f;
        controls.brake = speed > wanted + 1.5f ? std::clamp((speed - wanted) * 0.25f, 0.2f, 1.0f) : 0.0f;
        car->setControls(controls);
        play.frames(1);
    }
    float bestLap = play.world.value("bestLap");
    if (ComponentRegistry::instance().find("LapTimer")) {      // the laps are counted by the project's own script
        CHECK(play.world.value("lap") >= 3);                // two laps timed
        CHECK(bestLap > 20 && bestLap < 60);
    }
    CHECK(farthestOff < 6.0f);                          // it stayed on the road, between the walls
    CHECK(car->body.up().y > 0.8f);
}

#include "Assets/Gltf.hpp"
#include "Assets/MeshData.hpp"
#include "Core/TextFile.hpp"
TEST(template_models_are_read_as_meshes) {
    std::string models = templateAssets("Driving") + "/Models/";
    for (const char* name : {"circuit_road.obj", "circuit_lines.obj", "circuit_curb_red.obj", "circuit_start.obj", "trees_leaves.obj"}) {
        std::optional<std::string> contents = text::readFile(models + name);
        CHECK(contents.has_value());
        if (!contents) continue;
        std::optional<MeshData> mesh = MeshData::parseOBJ(*contents);
        CHECK(mesh.has_value() && mesh->indices.size() >= 36 && mesh->normals.size() == mesh->positions.size());
    }
    // The car is a glTF model in parts: its body's pieces in their places and one wheel around its hub.
    std::optional<GltfModel> car = GltfModel::load(models + "Fiat500/fiat500.glb");
    CHECK(car.has_value());
    if (car) {
        std::vector<GltfModel::Part> parts = car->parts();
        CHECK(parts.size() > 20);
        AABB body, wheel;
        bool hasBody = false, hasWheel = false;
        for (const GltfModel::Part& part : parts) {
            MeshData mesh = car->part(part);
            CHECK(!mesh.indices.empty() && mesh.normals.size() == mesh.positions.size() && mesh.tangents.size() == mesh.positions.size());
            AABB bounds = mesh.bounds();
            AABB& all = part.name.rfind("Wheel ", 0) == 0 ? wheel : body;
            bool& has = part.name.rfind("Wheel ", 0) == 0 ? hasWheel : hasBody;
            if (!has) all = bounds;
            else all.min = simd::min(all.min, bounds.min), all.max = simd::max(all.max, bounds.max);
            has = true;
        }
        CHECK(hasBody && hasWheel);
        CHECK_NEAR(body.max.z - body.min.z, 3.1, 0.15);                 // a small car's length
        CHECK(body.min.y > -0.01f && body.max.y < 1.65f);               // standing on the ground, luggage and all
        CHECK_NEAR(wheel.max.y, 0.28, 0.01);                            // a wheel around its own middle
        CHECK_NEAR(wheel.min.y, -0.28, 0.01);
        // "file#3" names a part; a bare name is the whole model.
        int part = -2;
        CHECK(gltfFileOf("Models/car.glb#3", &part) == "Models/car.glb" && part == 3);
        CHECK(gltfFileOf("Models/car.glb", &part) == "Models/car.glb" && part == -1);
        CHECK(isGltfFile("Models/car.glb#12") && !isGltfFile("Models/car.obj"));
    }
}

TEST(joystick_axes_drive_actions) {
    Input input;
    // The standard actions are for walking and driving; flying adds its own.
    CHECK(!input.hasAction("yaw") && !input.hasAction("flaps"));
    input.apply(InputMap::flying());
    CHECK(!input.hasJoystick("throttleLever") && input.axis("moveX") == 0);
    JoystickState radio;
    radio.connected = true;
    radio.name = "Radio";
    radio.axes = {0.5f, -1.0f, -1.0f, 0.01f};
    input.handleJoystick(radio);
    CHECK_NEAR(input.axis("moveX"), 0.49, 0.02);                        // the first axis rolls
    CHECK(input.axis("moveZ") == -1 && input.axis("yaw") == 0);         // the dead zone swallows a hair off center
    CHECK(input.hasJoystick("throttleLever") && input.axis("throttleLever") == -1);
    input.handleKey(Key::D, true);
    CHECK(input.axis("moveX") == 1);                                    // the keys still work beside it
    // An action keeps its joystick axis through the file.
    InputMap map = InputMap::fromText(InputMap::flying().toText());
    for (const InputAction& action : map.actions) {
        if (action.name == "throttleLever") CHECK(action.joystickAxis == 2 && !action.joystickInvert);
    }
    map.actions[0].joystickInvert = true;
    CHECK(InputMap::fromText(map.toText()).actions[0].joystickInvert);
}


#include "Script/Convert.hpp"

TEST(visual_scripts_become_code) {
    // A star that turns and is picked up: what a visual script of a pickup looks like on disk.
    std::optional<std::string> text = std::string(R"(molten-graph 1
node 1 "OnUpdate" 60 60
node 2 "Rotate" 320 60
  value "axis" "0 1 0"
  value "degrees" "180"
node 3 "OnTriggerEnter" 60 260
node 4 "Branch" 320 260
node 5 "OtherIs" 60 400
  value "name" "Player"
node 6 "AddValue" 580 260
  value "name" "stars"
  value "amount" "1"
node 7 "DestroySelf" 840 260
link 1 "then" 2 "in"
link 3 "then" 4 "in"
link 5 "result" 4 "condition"
link 4 "true" 6 "in"
link 6 "then" 7 "in"
)");
    std::optional<script::Graph> graph = script::Graph::fromText(*text);
    CHECK(graph.has_value());
    std::string code = script::graphToCpp(*graph, script::typeNameFor("star pickup"), "Scripts/Star.graph");
    CHECK(code.find("struct StarPickup {") != std::string::npos && code.find("MOLTEN_COMPONENT(StarPickup)") != std::string::npos);
    CHECK(code.find("void update(World& world, Entity self, float dt)") != std::string::npos);
    CHECK(code.find("call(\"Rotate\", {{\"axis\", script::Value::vector(0, 1, 0)}, {\"degrees\", script::Value::number(180)}});") != std::string::npos);
    CHECK(code.find("void onTriggerEnter(World& world, Entity self, Entity other)") != std::string::npos);
    CHECK(code.find("if (call.value(\"OtherIs\", {{\"name\", script::Value::string(\"Player\")}}, \"result\").asBool()) {") != std::string::npos);
    CHECK(code.find("call(\"DestroySelf\", {});") != std::string::npos);
    // Code made from a graph keeps it, and goes back to the same graph; edited, it says so.
    std::string kept = script::graphToCppKeepingGraph(*graph, "StarPickup", "Scripts/Star.graph");
    bool wasChanged = true;
    std::string keptName;
    std::optional<script::Graph> back = script::graphKeptIn(kept, &wasChanged, &keptName);
    CHECK(back && !wasChanged && keptName == "StarPickup" && back->toText() == graph->toText());
    std::string edited = kept;
    edited.replace(edited.find("struct StarPickup {"), 19, "struct StarPickup { int extra = 0;");
    CHECK(script::graphKeptIn(edited, &wasChanged) && wasChanged);
    CHECK(!script::graphKeptIn(code));                  // written without the graph: nothing to go back to

    // And a node called from code does what the box does.
    World world;
    Entity entity = world.scene.create("Thing");
    world.scene.add(entity, Transform{});
    script::ScriptState state;
    script::Call call(world, entity, 1.0f, state);
    CHECK(call("Rotate", {{"axis", script::Value::vector(0, 1, 0)}, {"degrees", script::Value::number(90)}}) == "then");
    CHECK(std::abs(world.scene.get<Transform>(entity)->rotation.vector.y - std::sin(kPi / 4)) < 1e-3f);
    call("AddValue", {{"name", script::Value::string("stars")}, {"amount", script::Value::number(2)}});
    CHECK(world.value("stars") == 2);
}

TEST(water_and_fog_settings_are_kept) {
    Material water;
    CHECK(readMaterial("molten-material 1\nshader water\nbaseColor 0.03 0.2 0.27 1\nroughness 0.05\nwater 0.35 1\n", water) && water.isWater);
    CHECK_NEAR(water.waveScale, 0.35, 1e-5);
    std::string written = writeMaterial(water);
    CHECK(written.find("shader water") != std::string::npos && written.find("water 0.35 1") != std::string::npos);
    // The fog's shape goes through a scene file and back.
    RenderSettings fog;
    fog.fogHeightFalloff = 0.006f;
    fog.fogSunScatter = 5.0f;
    Scene empty, back;
    std::vector<NamedMaterial> none;
    RenderSettings read;
    CHECK(readScene(writeScene(empty, none, fog), back, none, read, SceneAssets{}));
    CHECK_NEAR(read.fogHeightFalloff, 0.006, 1e-5);
    CHECK_NEAR(read.fogSunScatter, 5, 1e-4);

    // Something that floats comes to rest at the water's surface instead of falling through it.
    World world;
    kit::registerKit();
    Entity crate = world.scene.create("Crate");
    Transform transform;
    transform.position = Vec3{0, 3, 0};
    world.scene.add(crate, transform);
    world.scene.add(crate, RigidBody{});
    world.scene.add(crate, Collider::sphere(0.4f));
    world.scene.add(crate, kit::Buoyancy{});
    for (int i = 0; i < 60 * 8; i++) world.advance(1.0 / 60.0);
    float height = world.scene.get<Transform>(crate)->position.y;
    CHECK(height > -0.6f && height < 0.4f);
    CHECK(std::abs(world.scene.get<RigidBody>(crate)->velocity.y) < 0.3f);
}

TEST(traced_lighting_settings_are_kept) {
    Scene scene;
    RenderSettings settings;
    settings.rayTracedOcclusion = true;
    settings.occlusionRayCount = 6;
    settings.rayTracedReflections = true;
    settings.rayTracedBounce = true;
    settings.bounceIntensity = 1.5f;
    settings.fogShaftSteps = 16;
    settings.sky.cloudsHaveDepth = true;
    std::vector<NamedMaterial> materials;
    std::string text = writeScene(scene, materials, settings);
    CHECK(text.find("rayTracedLighting 1 6 1 1 1.5") != std::string::npos);
    Scene read;
    RenderSettings back;
    CHECK(readScene(text, read, materials, back, SceneAssets{}));
    CHECK(back.rayTracedOcclusion && back.occlusionRayCount == 6 && back.rayTracedReflections && back.rayTracedBounce);
    CHECK_NEAR(back.bounceIntensity, 1.5, 1e-5);
    CHECK(back.fogShaftSteps == 16 && back.sky.cloudsHaveDepth && text.find("volumetrics 1 16") != std::string::npos);
}

TEST(touch_controls_drive_the_same_actions_as_keys) {
    World world;
    Entity pad = world.scene.create("Stick");
    world.scene.add(pad, Transform{});
    world.scene.add(pad, kit::TouchStick{});
    Entity jump = world.scene.create("Jump");
    world.scene.add(jump, Transform{});
    world.scene.add(jump, kit::TouchButton{});
    kit::TouchStick* stick = world.scene.get<kit::TouchStick>(pad);
    kit::TouchButton* button = world.scene.get<kit::TouchButton>(jump);

    // Nobody touches: nothing is pushed.
    stick->update(world, pad, 0.016f);
    button->update(world, jump, 0.016f);
    CHECK_NEAR(world.input.axis("moveX"), 0.0f, 1e-6f);
    CHECK(!world.input.isDown("jump"));

    // A thumb lands on the stick, at the lower left (the screen is taken to be 1280 by 720), and drags right and up.
    Input::Touch thumb;
    thumb.id = 7;
    thumb.position = Vec2{120, 600};
    thumb.hasBegun = true;
    world.input.handleTouches({thumb});
    stick->update(world, pad, 0.016f);
    CHECK(stick->isHeld);
    thumb.hasBegun = false;
    thumb.position = Vec2{151, 569};
    world.input.handleTouches({thumb});
    stick->update(world, pad, 0.016f);
    CHECK_NEAR(world.input.axis("moveX"), 0.5f, 0.01f);
    CHECK_NEAR(world.input.axis("moveZ"), -0.5f, 0.01f);       // up on the stick is forward
    // Dragged far: full deflection, never more.
    thumb.position = Vec2{600, 600};
    world.input.handleTouches({thumb});
    stick->update(world, pad, 0.016f);
    CHECK_NEAR(world.input.axis("moveX"), 1.0f, 1e-4f);

    // A second finger on the button, at the lower right.
    Input::Touch tap;
    tap.id = 9;
    tap.position = button->place(Vec2{1280, 720});
    tap.hasBegun = true;
    world.input.handleTouches({thumb, tap});
    button->update(world, jump, 0.016f);
    CHECK(world.input.isDown("jump") && world.input.wasPressed("jump"));
    world.input.endFrame();
    button->update(world, jump, 0.016f);
    CHECK(world.input.isDown("jump") && !world.input.wasPressed("jump"));     // held, not pressed again

    // Everything let go.
    world.input.handleTouches({});
    stick->update(world, pad, 0.016f);
    button->update(world, jump, 0.016f);
    CHECK(!stick->isHeld);
    CHECK_NEAR(world.input.axis("moveX"), 0.0f, 1e-6f);
    CHECK(!world.input.isDown("jump"));
    CHECK(world.input.hasTouched());
}

TEST(effects_have_presets_files_and_a_life_of_three_colors) {
    for (const std::string& name : effectPresets()) {
        ParticleEmitter preset = effectPreset(name);
        // Written and read back, it is the same effect.
        ParticleEmitter back;
        CHECK(readEffect(writeEffect(preset), back));
        CHECK(writeEffect(back) == writeEffect(preset));
    }
    CHECK(!readEffect("not an effect", *std::make_unique<ParticleEmitter>()));
    ParticleEmitter fire = effectPreset("Fire");
    CHECK(fire.hasMiddle);
    // At its middle a particle has the middle color; before and after, something between.
    Vec4 middle = particleColor(fire, fire.middleAt);
    CHECK_NEAR(middle.x, fire.colorMiddle.x, 1e-4f);
    CHECK_NEAR(particleColor(fire, 0.0f).x, fire.colorStart.x, 1e-4f);
    CHECK_NEAR(particleColor(fire, 1.0f).w, fire.colorEnd.w, 1e-4f);
    CHECK(particleSize(fire, fire.middleAt * 0.5f) > fire.sizeStart && particleSize(fire, fire.middleAt * 0.5f) < fire.sizeMiddle);

    // In a scene file, what is not the usual survives.
    Scene scene;
    Entity e = scene.create("Fire");
    scene.add(e, Transform{});
    scene.add(e, fire);
    std::vector<NamedMaterial> materials;
    RenderSettings settings;
    std::string text = writeScene(scene, materials, settings);
    Scene read;
    CHECK(readScene(text, read, materials, settings, SceneAssets{}));
    bool found = false;
    read.each<ParticleEmitter, Transform>([&](Entity, ParticleEmitter& emitter, Transform&) {
        found = true;
        CHECK(emitter.hasMiddle);
        CHECK_NEAR(emitter.colorMiddle.y, fire.colorMiddle.y, 1e-5f);
        CHECK_NEAR(emitter.turbulence, fire.turbulence, 1e-5f);
    });
    CHECK(found);

    // An explosion bursts once, bounces nothing, and ends; sparks land and bounce.
    Scene play;
    Entity boom = play.create("Boom");
    play.add(boom, Transform{});
    play.add(boom, effectPreset("Explosion"));
    play.updateTransforms();
    updateParticles(play, 0.016f);
    CHECK(play.get<ParticleEmitter>(boom)->particles.size() == 220);
    for (int i = 0; i < 200; i++) updateParticles(play, 0.016f);
    CHECK(play.get<ParticleEmitter>(boom)->particles.empty());

    Entity sparks = play.create("Sparks");
    Transform above;
    above.position = Vec3{0, 0.5f, 0};
    play.add(sparks, above);
    ParticleEmitter falling = effectPreset("Sparks");
    falling.direction = Vec3{0, -1, 0};
    falling.lifetimeMin = falling.lifetimeMax = 2.0f;
    play.add(sparks, falling);
    play.updateTransforms();
    for (int i = 0; i < 60; i++) updateParticles(play, 0.016f);
    float lowest = 1e9f;
    for (const Particle& particle : play.get<ParticleEmitter>(sparks)->particles) lowest = std::min(lowest, particle.position.y);
    CHECK(lowest >= -1e-4f);
}

TEST(interface_buttons_are_clicked_with_the_mouse_where_the_game_is_shown) {
    World world;
    Entity entity = world.scene.create("Play");
    world.scene.add(entity, Transform{});
    kit::UIButton made;
    made.action = "jump";
    made.value = "clicks";
    made.amount = 2;
    world.scene.add(entity, made);
    kit::UIButton* button = world.scene.get<kit::UIButton>(entity);
    // The button is in the middle of a 1280 by 720 screen: at 640, 360.

    // In an editor the game is shown from 300, 100 of the window at half its size: a click at
    // 620, 280 of the window is a click at 640, 360 of the game.
    world.input.setPicture(Vec2{300, 100}, 2.0f);
    world.input.handleMouseMove(Vec2{620, 280}, Vec2{0, 0});
    CHECK_NEAR(world.input.mousePosition().x, 640.0f, 1e-3f);
    CHECK_NEAR(world.input.mousePosition().y, 360.0f, 1e-3f);
    button->update(world, entity, 0.016f);
    CHECK(button->isHovered && !button->isHeld);

    world.input.handleMouseButton(MouseButton::Left, true);
    button->update(world, entity, 0.016f);
    CHECK(button->isHeld && world.input.isDown("jump"));
    CHECK(!button->wasClicked);
    world.input.endFrame();
    world.input.handleMouseButton(MouseButton::Left, false);
    button->update(world, entity, 0.016f);
    CHECK(button->wasClicked);
    CHECK(!world.input.isDown("jump"));
    CHECK_NEAR(world.value("clicks"), 2.0f, 1e-6f);
    world.input.endFrame();
    button->update(world, entity, 0.016f);
    CHECK(!button->wasClicked);

    // Pressed on it and let go somewhere else: not a click.
    world.input.handleMouseButton(MouseButton::Left, true);
    button->update(world, entity, 0.016f);
    world.input.endFrame();
    world.input.handleMouseMove(Vec2{0, 0}, Vec2{0, 0});
    world.input.handleMouseButton(MouseButton::Left, false);
    button->update(world, entity, 0.016f);
    CHECK(!button->wasClicked);
    CHECK_NEAR(world.value("clicks"), 2.0f, 1e-6f);

    // A bar shows a value as a share of its most; the pieces are placed from an anchor.
    CHECK_NEAR(kit::placeOnScreen(Vec2{1000, 500}, Vec2{1, 1}, Vec2{-90, -110}).x, 910.0f, 1e-4f);
    CHECK(!world.hasValue("health"));
    world.setValue("health", 50);
    CHECK(world.hasValue("health"));
}

// ---------------------------------------------------------------- the showcase
#include "Water/Liquid.hpp"

TEST(the_showcase_runs_and_its_pieces_do_what_they_show) {
    Play play("Showcase");
    if (!play.isLoaded) return;                         // made only on asking (make_templates.py --showcase)
    if (!play.isLoaded) return;
    Vec3 lastDomino = play.positionOf("Domino 26");
    Vec3 lastBall = play.positionOf("Cradle Ball 5");
    Vec3 wreckingBall = play.positionOf("Wrecking Ball");
    play.frames(60 * 8);
    // The car stands where it started, on its wheels, and no station is near it.
    const kit::VehicleBody* car = play.world.scene.get<kit::VehicleBody>(play.world.find("Car"));
    CHECK(car && std::abs(car->speed()) < 0.3f && car->body.position.y > 0.2f && car->body.position.y < 0.6f);
    CHECK(play.world.value("station") == 0);
    // The dominoes have all gone down: the last one is lower than it stood.
    CHECK(play.positionOf("Domino 26").y < lastDomino.y - 0.3f);
    // The first ball of the cradle swung into the rest, and the last one was knocked away.
    CHECK(simd::distance(play.positionOf("Cradle Ball 5"), lastBall) > 0.05f);
    // The wrecking ball swings on its joint: it stays at its length from the pivot.
    Vec3 swung = play.positionOf("Wrecking Ball");
    CHECK(simd::distance(swung, wreckingBall) > 0.5f);
    // Something floats in the water basin, near its surface.
    float floating = play.positionOf("Buoy").y;
    CHECK(floating > 0.2f && floating < 1.6f);
    // The tanks hold their liquid.
    const Liquid* water = play.world.scene.get<Liquid>(play.world.find("Water Liquid"));
    CHECK(water && water->dropCount() > 300);
    if (water) {
        Vec3 tank = play.positionOf("Water Liquid");
        int outside = 0;
        for (Vec3 drop : water->positions) outside += std::abs(drop.x - tank.x) > 1.6f || std::abs(drop.z - tank.z) > 1.6f || drop.y < -0.2f;
        CHECK(outside < int(water->dropCount() / 20));
    }
    // Every star is still there, turning.
    CHECK(play.count("Star ") == 8 && play.world.value("stars") == 0);
    // Balls come down the chute.
    CHECK(play.count("Ball") >= 4);
    // Nothing has flown off or become not-a-number.
    int lost = 0;
    play.world.scene.each<RigidBody, Transform>([&](Entity, RigidBody&, Transform& transform) {
        Vec3 p = transform.worldPosition();
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || p.y < -5 || p.y > 60) lost++;
    });
    CHECK(lost == 0);
}

TEST(the_showcase_car_drives_to_a_station_and_collects_a_star) {
    Play play("Showcase");
    if (!play.isLoaded) return;                         // made only on asking (make_templates.py --showcase)
    kit::VehicleBody* car = play.world.scene.get<kit::VehicleBody>(play.world.find("Car"));
    if (!car) return;
    play.frames(60);
    // Toward the star ahead and to the right (the first, at 22.5 degrees), then on to the physics station behind.
    Vec3 star = play.positionOf("Star 1");
    for (int i = 0; i < 60 * 12 && play.world.value("stars") < 1; i++) {
        Vec3 at = car->body.position;
        Vec3 nose = rotate(car->body.orientation, Vec3{0, 0, -1});
        Vec3 to = star - at;
        float side = nose.x * to.z - nose.z * to.x;           // which way to turn
        kit::Controls controls;
        controls.throttle = car->speed() < 6 ? 0.6f : 0.0f;
        controls.steer = std::clamp(side * 0.4f, -1.0f, 1.0f);
        car->setControls(controls);
        play.frames(1);
    }
    CHECK(play.world.value("stars") == 1);
    CHECK(play.count("Star ") == 7);
}

TEST(the_driving_car_holds_a_turn_on_the_throttle_and_can_be_drifted) {
    // Full throttle into a long turn: stability control lets it slide a little and no more.
    auto worstSlide = [](float stability, float handbrakeFor, FILE* note) {
        Play play("Driving");
        kit::VehicleBody* car = play.world.scene.get<kit::VehicleBody>(play.world.find("Car"));
        if (!car) return -1.0f;
        car->stability = stability;
        // A wide flat floor beyond the circuit, which ends where its ground does: room for a long turn.
        Entity floor = play.world.scene.create("Test Floor");
        Transform floorPlace;
        floorPlace.position = Vec3{0, -0.5f, 260};
        if (Transform* at = play.world.scene.get<Transform>(floor)) *at = floorPlace;
        else play.world.scene.add(floor, floorPlace);
        Collider wide;
        wide.halfExtents = Vec3{600, 0.5f, 600};
        play.world.scene.add(floor, wide);
        play.world.scene.updateTransforms();
        // Out there, far from the walls, facing the open.
        if (Transform* place = play.world.scene.get<Transform>(play.world.find("Car"))) place->position = Vec3{0, place->position.y, 260};
        play.frames(30);
        float worst = 0;
        for (int i = 0; i < 60 * 6; i++) {
            kit::Controls controls;
            controls.throttle = 1;
            controls.steer = i > 60 * 2 ? 0.4f : 0.0f;
            controls.handbrake = i > 60 * 2 && i < 60 * 2 + int(handbrakeFor * 60) ? 1.0f : 0.0f;
            car->setControls(controls);
            play.frames(1);
            float along = simd::dot(car->body.velocity, car->body.forward()), across = simd::dot(car->body.velocity, car->body.right());
            if (along > 3 && i > 60 * 2) worst = std::max(worst, std::abs(std::atan2(across, along)) * 57.3f);
            if (note && i % 20 == 0) {
                std::fprintf(note, "%.0f %d speed %.1f slide %.1f at %.1f %.1f %.1f up %.2f\n", double(stability), i, double(along * 3.6f),
                             double(std::atan2(across, along) * 57.3f), double(car->body.position.x), double(car->body.position.y), double(car->body.position.z),
                             double(car->body.up().y));
            }
        }
        return worst;
    };
    FILE* note = std::getenv("MOLTEN_TURN_NOTE") ? stderr : nullptr;
    float held = worstSlide(0.85f, 0, note);
    float loose = worstSlide(0.0f, 0, note);
    float drifted = worstSlide(0.85f, 0.5f, note);
    std::fprintf(stderr, "turn: held %.1f loose %.1f drifted %.1f\n", double(held), double(loose), double(drifted));
    CHECK(held >= 0 && held < 30);                  // it may slide, but it does not spin
    CHECK(drifted > 8 && drifted < 70);             // the handbrake starts a drift, and it is caught
}

// ---------------------------------------------------------------- lighting quality, interface scale
#include "Rendering/Renderer.hpp"

TEST(lighting_quality_levels_are_recognized_and_kept) {
    RenderSettings settings;
    CHECK(lightingQualityOf(settings) == LightingQuality::Low);
    for (LightingQuality level : {LightingQuality::Medium, LightingQuality::High, LightingQuality::Ultra, LightingQuality::Low}) {
        applyLightingQuality(settings, level);
        CHECK(lightingQualityOf(settings) == level);
    }
    applyLightingQuality(settings, LightingQuality::High);
    settings.occlusionRayCount = 7;
    CHECK(lightingQualityOf(settings) == LightingQuality::Custom);
    // The reflections' limit goes through a scene file.
    settings.reflectionMaxRoughness = 0.42f;
    settings.reflectionStrength = 0.7f;
    Scene empty, read;
    std::vector<NamedMaterial> none;
    RenderSettings back;
    CHECK(readScene(writeScene(empty, none, settings), read, none, back, SceneAssets{}));
    CHECK_NEAR(back.reflectionMaxRoughness, 0.42, 1e-5);
    CHECK_NEAR(back.reflectionStrength, 0.7, 1e-5);
}

TEST(the_interface_scales_with_the_screen_it_is_shown_on) {
    CHECK_NEAR(World::interfaceScaleFor(Vec2{1280, 720}, Vec2{1280, 720}, 0.5f), 1.0, 1e-5);
    CHECK_NEAR(World::interfaceScaleFor(Vec2{640, 360}, Vec2{1280, 720}, 0.5f), 0.5, 1e-5);
    CHECK_NEAR(World::interfaceScaleFor(Vec2{2560, 720}, Vec2{1280, 720}, 0.0f), 2.0, 1e-5);       // by width
    CHECK_NEAR(World::interfaceScaleFor(Vec2{2560, 720}, Vec2{1280, 720}, 1.0f), 1.0, 1e-5);       // by height
    // Drawn into a scaled area, a shape lands where the scale says.
    Canvas canvas;
    canvas.begin(Vec2{640, 360});
    canvas.beginArea(Vec2{0, 0}, Vec2{640, 360}, 0.5f);
    CHECK_NEAR(canvas.size().x, 1280, 1e-3);
    canvas.rect(Vec2{1000, 600}, Vec2{100, 100}, Vec4{1, 1, 1, 1});
    canvas.endArea();
    float largestX = 0;
    for (const auto& vertex : canvas.vertices()) largestX = std::max(largestX, vertex.position.x);
    CHECK_NEAR(largestX, 550, 1.0);
}

TEST(the_driving_car_is_driven_by_touch_on_a_phone) {
    // A phone held sideways, in points: the stick and the pedals are where they are drawn.
    Play play("Driving");
    CHECK(play.isLoaded);
    kit::VehicleBody* car = play.world.scene.get<kit::VehicleBody>(play.world.find("Car"));
    CHECK(car != nullptr);
    if (!car) return;
    Vec2 screen{852, 393};
    auto drawn = [&]() {
        Canvas canvas;
        canvas.begin(screen);
        play.world.drawComponents(canvas);
    };
    play.frames(20);
    drawn();
    // Where GAS is on the screen: its place in the interface, times the interface's scale.
    const kit::UIButton* gas = play.world.scene.get<kit::UIButton>(play.world.find("Gas"));
    CHECK(gas != nullptr);
    if (!gas) return;
    float scale = play.world.interfaceScale;
    Vec2 logical = screen / scale;
    Vec2 middle = Vec2{gas->anchor.x * logical.x, gas->anchor.y * logical.y} + gas->offset;
    Input::Touch finger;
    finger.id = 7;
    finger.position = middle * scale;
    finger.hasBegun = true;
    for (int i = 0; i < 120; i++) {
        play.world.input.handleTouches({finger});
        finger.hasBegun = false;
        play.frames(1);
        drawn();
    }
    std::fprintf(stderr, "touch gas: scale %.3f at (%.0f %.0f) held %d isDown %d speed %.1f km/h throttle %.2f\n", double(scale),
                 double(finger.position.x), double(finger.position.y), int(gas->isHeld), int(play.world.input.isDown("gas")),
                 double(car->speed() * 3.6f), double(car->controls.throttle));
    CHECK(gas->isHeld && play.world.input.isDown("gas"));
    CHECK(car->speed() * 3.6f > 10);
    // A second finger on the left arrow steers left, the first still on the gas.
    const kit::UIButton* left = play.world.scene.get<kit::UIButton>(play.world.find("Steer Left"));
    CHECK(left != nullptr);
    if (!left) return;
    Input::Touch thumb;
    thumb.id = 8;
    thumb.position = (Vec2{left->anchor.x * logical.x, left->anchor.y * logical.y} + left->offset) * scale;
    thumb.hasBegun = true;
    for (int i = 0; i < 30; i++) {
        play.world.input.handleTouches({finger, thumb});
        thumb.hasBegun = false;
        play.frames(1);
        drawn();
    }
    CHECK(play.world.input.isDown("steerLeft") && !play.world.input.isDown("steerRight"));
    CHECK(car->controls.steer < -0.8f);
    // A phone shows its touch controls from the start: no touch needed first.
    Input fresh;
    CHECK(!fresh.hasTouched());
    fresh.isTouchScreen = true;
    CHECK(fresh.hasTouched());
}

TEST(a_games_own_component_wins_in_either_order_it_is_declared) {
    // In the editor the game's code is loaded after the engine's; in the iPhone player it is
    // linked in, and may be declared first. Either way the game's is the one used.
    ComponentRegistry& registry = ComponentRegistry::instance();
    for (bool gameFirst : {true, false}) {
        std::string name = gameFirst ? "OrderTestA" : "OrderTestB";
        ComponentType game, engine;
        game.name = engine.name = name;
        game.module = &ComponentRegistry::linkedGameModule;
        engine.category = "Vehicles";
        if (gameFirst) {
            registry.add(game);
            registry.add(engine);
        } else {
            registry.add(engine);
            registry.add(game);
        }
        const ComponentType* used = registry.find(name);
        CHECK(used && used->module == &ComponentRegistry::linkedGameModule && used->category == "Vehicles");
        int waiting = 0;
        for (const ComponentType& type : registry.replaced) waiting += type.name == name && type.module == nullptr;
        CHECK(waiting == 1);
        registry.remove(name);
    }
}

// ---------------------------------------------------------------- the phone's icon
#include "Assets/AppIcon.hpp"

TEST(an_icon_drawn_in_a_transparent_margin_fills_the_phones_square) {
    // A dark rounded square in a wide transparent margin, a bright mark in its middle: what a Mac icon looks like.
    const int size = 200;
    std::vector<uint8_t> pixels(size_t(size) * size * 4, 0);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float dx = std::max(std::abs(float(x) - 99.5f) - 40.0f, 0.0f), dy = std::max(std::abs(float(y) - 99.5f) - 40.0f, 0.0f);
            if (x < 40 || x >= 160 || y < 40 || y >= 160 || dx * dx + dy * dy > 400) continue;    // corners rounded by 20
            uint8_t* p = &pixels[(size_t(y) * size + size_t(x)) * 4];
            bool mark = std::abs(x - 100) < 10 && std::abs(y - 100) < 10;
            p[0] = mark ? 250 : 30, p[1] = mark ? 120 : 32, p[2] = mark ? 40 : 38, p[3] = 255;
        }
    }
    std::vector<uint8_t> icon = fullBleedIcon(pixels, size, size, 64);
    CHECK(icon.size() == 64 * 64 * 4);
    bool allOpaque = true, anyLight = false;
    for (size_t i = 0; i < icon.size(); i += 4) {
        allOpaque = allOpaque && icon[i + 3] == 255;
        anyLight = anyLight || (icon[i] > 200 && icon[i + 1] > 200 && icon[i + 2] > 200);
    }
    CHECK(allOpaque && !anyLight);                                  // no white frame, no white corners
    CHECK(std::abs(int(icon[0]) - 30) <= 3 && std::abs(int(icon[2]) - 38) <= 3);       // a corner: the square's own color
    const uint8_t* middle = &icon[(32 * 64 + 32) * 4];
    CHECK(middle[0] > 200 && middle[1] > 90);                        // the mark, in the middle, filling more of it
}

// ---------------------------------------------------------------- the race: limits, recovery, pits, rain, menus
namespace {
/// True if the game's scripts these tests need were built in (they are the Driving project's).
bool hasScripts(std::initializer_list<const char*> names) {
    for (const char* name : names) {
        const ComponentType* type = ComponentRegistry::instance().find(name);
        if (!type) return false;
    }
    return true;
}
/// Puts the car somewhere, standing still, facing along the world's +x.
void placeCar(Play& play, Vec3 at, Quat turned = axisAngle(Vec3{0, 1, 0}, radians(-90.0f))) {
    kit::VehicleBody* car = play.world.scene.get<kit::VehicleBody>(play.world.find("Car"));
    Transform* place = play.world.scene.get<Transform>(play.world.find("Car"));
    if (!car || !place) return;
    place->position = at;
    place->rotation = turned;
    car->body.position = at;
    car->body.orientation = turned;
    car->body.velocity = Vec3{0, 0, 0};
    car->body.angularVelocity = Vec3{0, 0, 0};
    play.world.scene.updateTransforms();
}
}  // namespace

TEST(a_lap_with_four_wheels_on_the_grass_does_not_count) {
    if (!hasScripts({"LapTimer", "Tyres"})) return;
    Play play("Driving");
    CHECK(play.isLoaded);
    if (!play.world.find("Waypoint 1").isValid()) return;
    play.frames(30);
    CHECK(play.world.value("lapValid") == 1);                       // on the grid, on the road
    placeCar(play, Vec3{0, 0.4f, 40});                              // the infield: grass all round
    play.frames(40);
    CHECK(play.world.value("lapValid") == 0);
    // The pit lane is not grass.
    Play pits("Driving");
    pits.frames(10);
    placeCar(pits, Vec3{-30, 0.4f, -16.5f});
    pits.frames(40);
    CHECK(pits.world.value("lapValid") == 1 && pits.world.value("inPitLane") == 1);
    CHECK_NEAR(pits.world.value("speedLimit"), 60 / 3.6, 0.01);
}

TEST(an_upturned_car_is_put_back_on_its_wheels) {
    if (!hasScripts({"DriveControls", "Tyres"})) return;
    Play play("Driving");
    if (!play.world.find("Waypoint 1").isValid()) return;
    play.frames(10);
    // On its roof on the straight.
    placeCar(play, Vec3{30, 1.0f, 0}, axisAngle(Vec3{0, 0, 1}, radians(180.0f)) * axisAngle(Vec3{0, 1, 0}, radians(-90.0f)));
    kit::VehicleBody* car = play.world.scene.get<kit::VehicleBody>(play.world.find("Car"));
    CHECK(car && car->body.up().y < 0);
    play.frames(60 * 3);
    CHECK(car && car->body.up().y > 0.9f);
    // Still facing along the straight, and near where it was.
    CHECK(car && std::abs(car->body.forward().x) > 0.9f && std::abs(car->body.position.x - 30) < 3);
    // R does the same at once.
    placeCar(play, Vec3{-20, 1.0f, 0}, axisAngle(Vec3{0, 0, 1}, radians(90.0f)));
    play.world.input.handleKey(Key::R, true);
    play.frames(1);
    play.world.input.handleKey(Key::R, false);
    play.frames(2);
    CHECK(car && car->body.up().y > 0.95f);
}

TEST(a_stop_in_the_pit_box_changes_the_tyres) {
    if (!hasScripts({"Tyres"})) return;
    Play play("Driving");
    if (!play.world.find("Pit Box").isValid()) return;
    play.frames(10);
    const Transform* box = play.world.scene.get<Transform>(play.world.find("Pit Box"));
    CHECK(box != nullptr);
    if (!box) return;
    // Worn tyres first: a hard few seconds of wheelspin on the spot.
    CHECK(play.world.value("tyreCompound") == 1);
    Vec3 at = box->worldPosition();
    placeCar(play, Vec3{at.x, 0.4f, at.z});
    play.frames(30);
    CHECK(play.world.value("inPitLane") == 1);
    play.world.input.handleKey(Key::Num4, true);                   // wet tyres for the next stop
    play.frames(1);
    play.world.input.handleKey(Key::Num4, false);
    CHECK(play.world.value("nextCompound") == 3);
    play.frames(30);
    CHECK(play.world.value("pitStop") > 0);                         // stopped in the box: the stop has begun
    kit::VehicleBody* car = play.world.scene.get<kit::VehicleBody>(play.world.find("Car"));
    play.world.input.handleKey(Key::W, true);                      // the driver cannot leave before it is done
    play.frames(60);
    CHECK(car && std::abs(car->speed()) < 0.5f);
    play.frames(60 * 4);
    CHECK(play.world.value("pitStop") == 0 && play.world.value("tyreCompound") == 3 && play.world.value("tyreWear") == 0);
    play.frames(60);
    CHECK(car && car->speed() > 1.0f);                              // and then it goes
    play.world.input.handleKey(Key::W, false);
}

TEST(rain_wets_the_road_and_the_tyres_grip_less) {
    if (!hasScripts({"Weather", "Tyres"})) return;
    auto gripIn = [](float weather, float compound) {
        Play play("Driving");
        play.world.setPreference("weather", weather);
        play.world.setPreference("startCompound", compound);
        play.frames(20);
        float grip = 0;
        play.world.scene.each<kit::Wheel>([&](Entity, kit::Wheel& wheel) { grip += wheel.grip; });
        return std::make_pair(grip, play.world.value("wetness"));
    };
    auto [dry, dryWet] = gripIn(0, 1);
    auto [wet, wetWet] = gripIn(2, 1);
    auto [rainTyre, rainTyreWet] = gripIn(2, 3);
    CHECK(dryWet < 0.01f && wetWet > 0.99f && rainTyreWet > 0.99f);
    CHECK(wet < dry * 0.7f);                                        // mediums on a wet road: two thirds of the grip
    CHECK(rainTyre > wet * 1.4f);                                   // wets in the wet: nearly all of it back
    // And the rain falls.
    Play play("Driving");
    play.world.setPreference("weather", 2);
    play.frames(30);
    const ParticleEmitter* rain = play.world.scene.get<ParticleEmitter>(play.world.find("Rain"));
    CHECK(rain && rain->isEmitting && !rain->particles.empty() && rain->stretch > 0);
}

TEST(the_game_opens_on_its_title_and_pauses) {
    if (!hasScripts({"GameMenu"})) return;
    Play play("Driving", true);
    if (!play.world.find("Title Screen").isValid()) return;
    play.frames(5);
    auto isShown = [&](const char* name) {
        const Transform* group = play.world.scene.get<Transform>(play.world.find(name));
        return group && group->isActiveInHierarchy;
    };
    CHECK(isShown("Title Screen") && !isShown("Interface") && play.world.time.timeScale == 0);
    play.world.input.handleKey(Key::Enter, true);
    play.frames(1);
    play.world.input.handleKey(Key::Enter, false);
    play.frames(2);
    CHECK(!isShown("Title Screen") && isShown("Interface") && play.world.time.timeScale == 1);
    play.world.input.handleKey(Key::Escape, true);
    play.frames(1);
    play.world.input.handleKey(Key::Escape, false);
    play.frames(1);
    CHECK(isShown("Pause Screen") && play.world.time.timeScale == 0);
    // The settings apply as they are chosen: the picture, the sound, the size of the buttons.
    play.world.setPreference("quality", 0);
    play.world.setPreference("volume", 0);
    play.world.setPreference("controls", 2);
    play.frames(1);
    CHECK_NEAR(play.world.sceneSettings().renderScale, 0.7, 1e-4);
    CHECK(!play.world.sceneSettings().rayTracedShadows && play.world.soundVolume == 0);
    const kit::UIButton* gas = play.world.scene.get<kit::UIButton>(play.world.find("Gas"));
    CHECK(gas && gas->radius > 100);
}

TEST(preferences_are_kept_from_one_game_to_the_next) {
    std::string file = "/tmp/molten-test-preferences/Preferences.txt";
    std::filesystem::remove_all("/tmp/molten-test-preferences");
    {
        World first;
        first.preferencesFile = file;
        CHECK(first.preference("bestLap", -1) == -1);
        first.setPreference("bestLap", 41.25f);
        first.setPreference("weather", 2);
    }
    World second;
    second.preferencesFile = file;
    CHECK_NEAR(second.preference("bestLap"), 41.25, 1e-4);
    CHECK(second.preference("weather") == 2 && second.hasPreference("weather") && !second.hasPreference("volume"));
    World none;                                                     // with no file, kept for the run only
    none.setPreference("a", 3);
    CHECK(none.preference("a") == 3);
}

#include <chrono>
TEST(profile_the_driving_game_when_asked) {
    // MOLTEN_PROFILE=1: how long a frame of the race takes on this machine, in the worst
    // weather, with the car driven round. Not a check: a measurement, to compare.
    if (!std::getenv("MOLTEN_PROFILE")) return;
    Play play("Driving");
    play.world.setPreference("weather", 2);
    play.frames(30);
    play.world.input.handleKey(Key::W, true);
    auto begin = std::chrono::steady_clock::now();
    int frames = std::atoi(std::getenv("MOLTEN_PROFILE")) > 1 ? std::atoi(std::getenv("MOLTEN_PROFILE")) : 600;
    for (int i = 0; i < frames; i++) {
        play.world.input.handleKey(Key::D, (i / 90) % 3 == 1);
        play.frames(1);
    }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count() / frames;
    size_t drops = 0;
    play.world.scene.each<ParticleEmitter>([&](Entity, ParticleEmitter& emitter) { drops += emitter.particles.size(); });
    std::fprintf(stderr, "profile: %.3f ms a frame, %zu particles, %zu entities\n", ms, drops, play.world.scene.entityCount());
}
