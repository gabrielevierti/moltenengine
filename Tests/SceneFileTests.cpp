#include "Test.hpp"

#include "Scene/SceneFile.hpp"

using namespace molten;

namespace {

// Builds a scene that uses every kind of line the format has (except meshes and
// textures, which need a GPU).
void buildScene(Scene& scene, std::vector<NamedMaterial>& materials) {
    auto stone = std::make_shared<Material>();
    stone->baseColor = Vec4{0.8f, 0.7f, 0.6f, 1};
    stone->roughness = 0.85f;
    stone->uvScale = Vec2{2, 3};
    auto glass = std::make_shared<Material>();
    glass->baseColor = Vec4{0.2f, 0.5f, 1.0f, 0.4f};
    glass->alphaMode = AlphaMode::Blend;
    glass->emissive = Vec3{0, 0.5f, 2.5f};
    glass->isDoubleSided = true;
    materials = {{"Stone", stone}, {"Blue \"glass\"", glass}};

    Entity sun = scene.create("Sun");
    Transform sunTransform;
    sunTransform.rotation = lookRotation(Vec3{0.3f, -1, -0.2f});
    scene.add(sun, sunTransform);
    Light light;
    light.type = LightType::Directional;
    light.color = Vec3{1, 0.95f, 0.8f};
    light.intensity = 3.5f;
    light.castsShadows = true;
    scene.add(sun, light);

    Entity cart = scene.create("Cart");
    Transform cartTransform;
    cartTransform.position = Vec3{1.5f, 0.25f, -3};
    cartTransform.scale = Vec3{2, 0.5f, 1};
    scene.add(cart, cartTransform);
    MeshRenderer renderer;
    renderer.material = glass;
    renderer.tint = Vec4{1, 0.5f, 0.5f, 1};
    renderer.castsShadows = false;
    scene.add(cart, renderer);
    scene.add(cart, Collider::box(Vec3{1, 0.25f, 0.5f}));
    RigidBody body;
    body.type = BodyType::Kinematic;
    body.friction = 0.9f;
    scene.add(cart, body);

    Entity wheel = scene.create("Front wheel");
    Transform wheelTransform;
    wheelTransform.position = Vec3{0.4f, -0.5f, 0};
    wheelTransform.parent = cart;
    scene.add(wheel, wheelTransform);
    Collider capsule = Collider::capsule(0.2f, 0.4f);
    capsule.isTrigger = true;
    capsule.offset = Vec3{0, 0.1f, 0};
    scene.add(wheel, capsule);

    Entity camera = scene.create("Camera");
    Transform cameraTransform;
    cameraTransform.position = Vec3{0, 2, 8};
    scene.add(camera, cameraTransform);
    Camera lens;
    lens.fovY = radians(45.0f);
    lens.farPlane = 250;
    scene.add(camera, lens);

    Entity fountain = scene.create("Fountain");
    scene.add(fountain, Transform{});
    ParticleEmitter emitter;
    emitter.rate = 60;
    emitter.colorStart = Vec4{2, 1, 0.5f, 1};
    emitter.isAdditive = false;
    emitter.maxParticles = 128;
    scene.add(fountain, emitter);

    Entity tool = scene.create("Editor camera");        // belongs to a tool: must not be saved
    scene.add(tool, Transform{});
    scene.add(tool, Camera{});
    scene.add(tool, EditorOnly{});
}

}  // namespace

TEST(scene_file_round_trip_keeps_everything) {
    Scene scene;
    std::vector<NamedMaterial> materials;
    buildScene(scene, materials);
    RenderSettings settings;
    settings.exposure = 1.4f;
    settings.fogDensity = 0.01f;
    settings.sky.zenithColor = Vec3{0.1f, 0.2f, 0.6f};
    settings.bloomEnabled = false;
    std::string text = writeScene(scene, materials, settings);
    CHECK(text.find("Editor camera") == std::string::npos);

    Scene loaded;
    std::vector<NamedMaterial> loadedMaterials;
    RenderSettings loadedSettings;
    CHECK(readScene(text, loaded, loadedMaterials, loadedSettings, SceneAssets{}));
    CHECK(loaded.entityCount() == 5);
    CHECK(loadedMaterials.size() == 2);
    CHECK(loadedMaterials[1].name == "Blue \"glass\"");
    CHECK(loadedMaterials[1].material->alphaMode == AlphaMode::Blend);
    CHECK_NEAR(loadedMaterials[1].material->emissive.z, 2.5, 1e-6);
    CHECK_NEAR(loadedSettings.exposure, 1.4, 1e-6);
    CHECK(!loadedSettings.bloomEnabled);

    Entity cart = loaded.find("Cart");
    Entity wheel = loaded.find("Front wheel");
    CHECK(loaded.get<Transform>(wheel)->parent == cart);
    CHECK(loaded.get<MeshRenderer>(cart)->material == loadedMaterials[1].material);
    CHECK(loaded.get<RigidBody>(cart)->type == BodyType::Kinematic);
    CHECK(loaded.get<Collider>(wheel)->shape == ColliderShape::Capsule);
    CHECK(loaded.get<Collider>(wheel)->isTrigger);
    CHECK(loaded.get<Light>(loaded.find("Sun"))->castsShadows);
    CHECK_NEAR(loaded.get<Camera>(loaded.find("Camera"))->farPlane, 250, 1e-6);
    CHECK(loaded.get<ParticleEmitter>(loaded.find("Fountain"))->maxParticles == 128);

    // Writing what was read gives the same text, to the last digit.
    CHECK(writeScene(loaded, loadedMaterials, loadedSettings) == text);
}

TEST(scene_file_skips_what_it_does_not_know_and_rejects_other_files) {
    Scene scene;
    std::vector<NamedMaterial> materials;
    RenderSettings settings;
    const char* text =
        "learnmetal-scene 1\n"
        "# a comment\n"
        "settings\n"
        "  exposure 2\n"
        "  somethingFromTheFuture 1 2 3\n"
        "material \"Paint\"\n"
        "  roughness 0.25\n"
        "entity \"Lamp post\"\n"
        "  transform 1 2 3\n"                   // rotation and scale left out: defaults
        "  hologram 7\n"
        "  light spot 1 0 0 4\n"
        "  parent 5\n";                         // no such entity: ignored
    CHECK(readScene(text, scene, materials, settings, SceneAssets{}));
    CHECK_NEAR(settings.exposure, 2, 1e-6);
    CHECK(materials.size() == 1);
    CHECK_NEAR(materials[0].material->roughness, 0.25, 1e-6);
    Entity lamp = scene.find("Lamp post");
    CHECK(lamp.isValid());
    CHECK_NEAR(scene.get<Transform>(lamp)->position.y, 2, 1e-6);
    CHECK_NEAR(scene.get<Transform>(lamp)->scale.x, 1, 1e-6);
    CHECK(!scene.get<Transform>(lamp)->parent.isValid());
    CHECK(scene.get<Light>(lamp)->type == LightType::Spot);
    CHECK_NEAR(scene.get<Light>(lamp)->intensity, 4, 1e-6);

    Scene other;
    CHECK(!readScene("{ \"json\": true }", other, materials, settings, SceneAssets{}));
    CHECK(other.entityCount() == 0);
}
