#include "Test.hpp"

#include "Animation/Animator.hpp"
#include "Assets/Gltf.hpp"
#include "Core/Json.hpp"
#include "Core/TextFile.hpp"
#include "Kit/Kit.hpp"
#include "Rendering/Material.hpp"

#include <cstring>
#include <fstream>

using namespace molten;

namespace {

/// A strip of two bones: the lower half of a sheet follows the first, the upper half the
/// second, which an animation called "Bend" turns a quarter of the way round in a second.
struct Strip {
    std::vector<uint8_t> bytes;
    std::string json;

    template <typename T>
    void put(const std::vector<T>& values) {
        size_t at = bytes.size();
        bytes.resize(at + values.size() * sizeof(T));
        std::memcpy(bytes.data() + at, values.data(), values.size() * sizeof(T));
    }
    Strip() {
        put<float>({0, 0, 0, 1, 0, 0, 0, 2, 0, 1, 2, 0});                               // positions
        put<uint8_t>({0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});                 // joints
        put<float>({1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});                   // weights
        put<uint16_t>({0, 1, 2, 1, 3, 2});                                              // triangles
        put<float>({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1});      // inverse bind
        put<float>({0, 1});                                                             // times
        put<float>({0, 0, 0, 1, 0, 0, 0.70710678f, 0.70710678f});                       // rotations
        json = R"({"asset":{"version":"2.0"},
          "buffers":[{"byteLength":308BUFFER}],
          "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":48},{"buffer":0,"byteOffset":48,"byteLength":16},{"buffer":0,"byteOffset":64,"byteLength":64},
                         {"buffer":0,"byteOffset":128,"byteLength":12},{"buffer":0,"byteOffset":140,"byteLength":128},{"buffer":0,"byteOffset":268,"byteLength":8},
                         {"buffer":0,"byteOffset":276,"byteLength":32}],
          "accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3"},{"bufferView":1,"componentType":5121,"count":4,"type":"VEC4"},
                       {"bufferView":2,"componentType":5126,"count":4,"type":"VEC4"},{"bufferView":3,"componentType":5123,"count":6,"type":"SCALAR"},
                       {"bufferView":4,"componentType":5126,"count":2,"type":"MAT4"},{"bufferView":5,"componentType":5126,"count":2,"type":"SCALAR"},
                       {"bufferView":6,"componentType":5126,"count":2,"type":"VEC4"}],
          "materials":[{"name":"Skin è","pbrMetallicRoughness":{"baseColorFactor":[0.8,0.2,0.1,1],"metallicFactor":0,"roughnessFactor":0.5}}],
          "meshes":[{"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":1,"WEIGHTS_0":2},"indices":3,"material":0}]}],
          "nodes":[{"name":"Root","mesh":0,"skin":0,"children":[1]},{"name":"Bone0","children":[2]},{"name":"Bone1","translation":[0,1,0]}],
          "skins":[{"joints":[1,2],"inverseBindMatrices":4}],
          "animations":[{"name":"Bend","channels":[{"sampler":0,"target":{"node":2,"path":"rotation"}}],"samplers":[{"input":5,"output":6,"interpolation":"LINEAR"}]}],
          "scenes":[{"nodes":[0]}],"scene":0})";
    }
    std::string withBuffer(const std::string& uri) const {
        std::string out = json;
        out.replace(out.find("BUFFER"), 6, uri);
        return out;
    }
    /// The whole model as one .glb file.
    void writeGlb(const std::string& path) const {
        std::string text = withBuffer("");
        while (text.size() % 4) text += ' ';
        std::ofstream file(path, std::ios::binary);
        auto u32 = [&](uint32_t value) { file.write(reinterpret_cast<const char*>(&value), 4); };
        file.write("glTF", 4);
        u32(2);
        u32(uint32_t(12 + 8 + text.size() + 8 + bytes.size()));
        u32(uint32_t(text.size()));
        u32(0x4E4F534A);
        file.write(text.data(), long(text.size()));
        u32(uint32_t(bytes.size()));
        u32(0x004E4942);
        file.write(reinterpret_cast<const char*>(bytes.data()), long(bytes.size()));
    }
};

/// A World that keeps the last mesh it was asked to show.
struct Stage : World {
    MeshData shown;
    int count = 0;
    void showMesh(Entity, const MeshData& data) override {
        shown = data;
        count++;
    }
};

}  // namespace

TEST(json_is_read) {
    std::optional<Json> value = Json::parse(R"( {"a": [1, -2.5e1, true, null, "x\n\"y\""], "b": {"c": {}}} )");
    CHECK(value.has_value());
    CHECK((*value)["a"].size() == 5 && (*value)["a"][1].asNumber() == -25 && (*value)["a"][2].boolean && (*value)["a"][3].isNull());
    CHECK((*value)["a"][4].asString() == "x\n\"y\"" && (*value)["b"].has("c") && (*value)["missing"]["deeper"].isNull());
    CHECK(!Json::parse("{\"a\": }").has_value() && !Json::parse("[1, 2").has_value() && !Json::parse("{} trailing").has_value());
}

TEST(gltf_models_are_read_with_their_bones_and_animations) {
    Strip strip;
    CHECK(strip.bytes.size() == 308);
    strip.writeGlb("/tmp/molten_strip.glb");
    std::string error;
    std::optional<GltfModel> model = GltfModel::load("/tmp/molten_strip.glb", &error);
    CHECK(model.has_value());
    if (!model) {
        printf("    %s\n", error.c_str());
        return;
    }
    CHECK(model->nodes.size() == 3 && model->nodes[2].parent == 1 && model->nodes[1].parent == 0 && model->nodes[0].skin == 0);
    CHECK(model->meshes.size() == 1 && model->meshes[0].size() == 1);
    const GltfPrimitive& primitive = model->meshes[0][0];
    CHECK(primitive.mesh.positions.size() == 4 && primitive.mesh.indices.size() == 6 && primitive.joints.size() == 4);
    CHECK(primitive.joints[3][0] == 1 && primitive.weights[3].x == 1);
    CHECK(std::abs(primitive.mesh.normals[0].z) > 0.99f);                           // worked out, since the file has none
    CHECK(model->materials.size() == 1 && model->materials[0].name == "Skin \xC3\xA8");
    CHECK_NEAR(model->materials[0].baseColor.x, 0.8, 1e-5);
    CHECK(model->skins.size() == 1 && model->skins[0].joints.size() == 2);
    CHECK_NEAR(model->skins[0].inverseBind[1].columns[3].y, -1, 1e-6);
    CHECK(model->animations.size() == 1 && model->findAnimation("Bend") == 0 && model->animations[0].duration == 1);

    // At rest it is the sheet as it was modeled.
    MeshData rest = model->assembled(model->restPose());
    CHECK(rest.positions.size() == 4 && simd::length(rest.positions[3] - Vec3{1, 2, 0}) < 1e-5f);
    // Bent all the way, the upper half has swung a quarter turn about the second bone.
    GltfPose pose = model->restPose();
    model->sample(0, 1.0f, pose);
    MeshData bent = model->assembled(pose);
    CHECK(simd::length(bent.positions[0] - Vec3{0, 0, 0}) < 1e-5f && simd::length(bent.positions[1] - Vec3{1, 0, 0}) < 1e-5f);
    CHECK(simd::length(bent.positions[2] - Vec3{-1, 1, 0}) < 1e-4f && simd::length(bent.positions[3] - Vec3{-1, 2, 0}) < 1e-4f);
    // Halfway, an eighth of a turn.
    pose = model->restPose();
    model->sample(0, 0.5f, pose);
    MeshData half = model->assembled(pose);
    CHECK(simd::length(half.positions[2] - Vec3{-0.70710678f, 1.70710678f, 0}) < 1e-4f);

    // The same model as text with its data inside it reads the same.
    static const char* digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    for (size_t i = 0; i < strip.bytes.size(); i += 3) {
        uint32_t group = uint32_t(strip.bytes[i]) << 16 | (i + 1 < strip.bytes.size() ? uint32_t(strip.bytes[i + 1]) << 8 : 0) | (i + 2 < strip.bytes.size() ? strip.bytes[i + 2] : 0);
        encoded += digits[group >> 18];
        encoded += digits[(group >> 12) & 63];
        encoded += i + 1 < strip.bytes.size() ? digits[(group >> 6) & 63] : '=';
        encoded += i + 2 < strip.bytes.size() ? digits[group & 63] : '=';
    }
    std::optional<GltfModel> inlined = GltfModel::parse(strip.withBuffer(",\"uri\":\"data:application/octet-stream;base64," + encoded + "\""), {}, "");
    CHECK(inlined.has_value() && inlined->animations.size() == 1 && inlined->meshes[0][0].mesh.positions[3].y == 2);
    // A file that is not a model says so instead of crashing.
    CHECK(!GltfModel::parse("{\"meshes\":[]}", {}, "", &error).has_value() && !error.empty());
    CHECK(!GltfModel::parse("not json", {}, "").has_value());
}

TEST(an_animator_plays_a_clip) {
    Strip strip;
    strip.writeGlb("/tmp/molten_strip.glb");
    kit::registerKit();
    Stage stage;
    stage.assetRoot = "/tmp";
    Entity entity = stage.scene.create("Sheet");
    stage.scene.add(entity, Transform{});
    Animator animator;
    animator.model = "molten_strip.glb";
    animator.clip = "Bend";
    animator.loops = false;
    animator.blendTime = 0;
    stage.scene.add(entity, animator);
    for (int i = 0; i < 30; i++) stage.advance(1.0 / 60.0);                          // half a second
    CHECK(stage.count == 30);
    CHECK(simd::length(stage.shown.positions[2] - Vec3{-0.70710678f, 1.70710678f, 0}) < 0.03f);
    for (int i = 0; i < 60; i++) stage.advance(1.0 / 60.0);
    const Animator* played = stage.scene.get<Animator>(entity);
    CHECK(played->isFinished() && played->time == 1);
    CHECK(simd::length(stage.shown.positions[3] - Vec3{-1, 2, 0}) < 1e-3f);          // it stays at the end
}

TEST(an_animator_controller_is_read_and_written) {
    const char* text = R"(molten-animator 1
parameter float speed 0
parameter bool grounded 1
parameter trigger jump
state Locomotion blend speed
  motion Idle 0
  motion Walk 2
  motion Run 6
state Jump loops 0
  motion Jump
  event Audio/jump.wav 0
  event Land 0.8
start Locomotion
transition Locomotion -> Jump when jump
transition Any Locomotion when grounded true and speed > 0.5 blend 0.1
transition Jump Locomotion exit 0.9
)";
    std::string error;
    std::optional<AnimatorController> controller = AnimatorController::parse(text, &error);
    CHECK(controller.has_value());
    if (!controller) {
        printf("    %s\n", error.c_str());
        return;
    }
    CHECK(controller->parameters.size() == 3 && controller->states.size() == 2 && controller->transitions.size() == 3);
    CHECK(controller->states[0].blendBy == "speed" && controller->states[0].motions[2].at == 6 && !controller->states[1].loops);
    CHECK(controller->transitions[0].to == "Jump" && controller->transitions[0].conditions[0].test == AnimatorController::Condition::Test::Triggered);
    CHECK(controller->transitions[1].conditions.size() == 2 && controller->transitions[1].blend == 0.1f);
    CHECK(controller->transitions[2].exitAt == 0.9f && controller->states[1].events[1].name == "Land");
    std::optional<AnimatorController> again = AnimatorController::parse(controller->toText());
    CHECK(again && again->toText() == controller->toText());
    // A blend tree plays the two motions around the value, in proportion.
    std::vector<float> weights = AnimatorController::blendWeights(controller->states[0].motions, 4);
    CHECK(weights[0] == 0 && std::abs(weights[1] - 0.5f) < 1e-5f && std::abs(weights[2] - 0.5f) < 1e-5f);
    CHECK(AnimatorController::blendWeights(controller->states[0].motions, -1)[0] == 1);
    CHECK(AnimatorController::blendWeights(controller->states[0].motions, 9)[2] == 1);
    // Mistakes are said, with their line.
    CHECK(!AnimatorController::parse("molten-animator 1\nstate A\ntransition A B\n", &error) && error.find("B") != std::string::npos);
    CHECK(!AnimatorController::parse("state A\n", &error));
}

TEST(an_animator_goes_from_state_to_state_as_its_parameters_say) {
    Strip strip;
    strip.writeGlb("/tmp/molten_strip.glb");
    text::writeFile("/tmp/molten_strip.animator", R"(molten-animator 1
parameter trigger jump
parameter bool grounded 1
state Stand
  motion Bend
  speed 0
state Leap loops 0
  motion Bend
  event Up 0.5
state Air
  motion Bend
transition Stand Leap when jump
transition Leap Air exit 1
transition Air Stand when grounded true
)");
    kit::registerKit();
    Stage stage;
    stage.assetRoot = "/tmp";
    Entity entity = stage.scene.create("Sheet");
    stage.scene.add(entity, Transform{});
    Animator animator;
    animator.model = "molten_strip.glb";
    animator.controller = "molten_strip.animator";
    stage.scene.add(entity, animator);
    stage.advance(1.0 / 60.0);
    Animator* playing = stage.scene.get<Animator>(entity);
    CHECK(playing->currentState() == "Stand");
    playing->setBool("grounded", false);
    playing->setTrigger("jump");
    stage.advance(1.0 / 60.0);
    CHECK(playing->currentState() == "Leap" && playing->parameter("jump") == 0);      // the trigger was used up
    bool heard = false;
    for (int i = 0; i < 70 && playing->currentState() == "Leap"; i++) {
        stage.advance(1.0 / 60.0);
        for (const std::string& event : playing->firedEvents) heard = heard || event == "Up";
    }
    CHECK(heard);
    CHECK(playing->currentState() == "Air");                                         // after the clip's end
    playing->setBool("grounded", true);
    stage.advance(1.0 / 60.0);
    CHECK(playing->currentState() == "Stand");
    CHECK(stage.count > 60);
}

TEST(two_bone_ik_puts_the_end_of_a_limb_on_its_target) {
    // A leg standing straight up from the origin: hip, knee one meter up, foot one more.
    GltfModel model;
    model.nodes.resize(3);
    model.nodes[0].name = "Hip";
    model.nodes[1].name = "Knee";
    model.nodes[1].parent = 0;
    model.nodes[1].translation = Vec3{0, 1, 0};
    model.nodes[2].name = "Foot";
    model.nodes[2].parent = 1;
    model.nodes[2].translation = Vec3{0, 1, 0};
    model.nodes[0].children = {1};
    model.nodes[1].children = {2};
    auto footAt = [&](const GltfPose& pose) { return xyz(model.globalMatrices(pose)[2].columns[3]); };
    for (Vec3 target : {Vec3{1, 1, 0}, Vec3{0.3f, 0.8f, 0.6f}, Vec3{-1.2f, 0.5f, 0.2f}}) {
        GltfPose pose = model.restPose();
        Vec3 pole{0, 1, 1};
        CHECK(solveTwoBoneIK(model, pose, 2, target, &pole, 1.0f));
        CHECK(simd::length(footAt(pose) - target) < 1e-3f);
        // The knee bends toward the pole's side.
        Vec3 knee = xyz(model.globalMatrices(pose)[1].columns[3]);
        CHECK(knee.z > -1e-3f);
    }
    // Out of reach: as far toward it as the leg goes.
    GltfPose pose = model.restPose();
    solveTwoBoneIK(model, pose, 2, Vec3{3, 0, 0}, nullptr, 1.0f);
    CHECK(footAt(pose).x > 1.95f);
    // Half the weight: halfway there, more or less.
    GltfPose half = model.restPose();
    solveTwoBoneIK(model, half, 2, Vec3{1, 1, 0}, nullptr, 0.0f);
    CHECK(simd::length(footAt(half) - Vec3{0, 2, 0}) < 1e-4f);
    CHECK(!solveTwoBoneIK(model, half, 1, Vec3{1, 1, 0}, nullptr, 1.0f));              // the knee has no grandparent
}

TEST(root_motion_moves_the_entity_and_keeps_the_body_over_it) {
    // The strip, its first bone walking forward 2 meters in the second its clip lasts.
    Strip strip;
    strip.put<float>({0, 0, 0, 0, 0, -2});                                          // translations
    auto swap = [&](const std::string& from, const std::string& to) {
        size_t at = strip.json.find(from);
        if (at != std::string::npos) strip.json.replace(at, from.size(), to);
    };
    swap("\"byteLength\":308BUFFER", "\"byteLength\":332BUFFER");
    swap("{\"buffer\":0,\"byteOffset\":276,\"byteLength\":32}]", "{\"buffer\":0,\"byteOffset\":276,\"byteLength\":32},{\"buffer\":0,\"byteOffset\":308,\"byteLength\":24}]");
    swap("{\"bufferView\":6,\"componentType\":5126,\"count\":2,\"type\":\"VEC4\"}]",
         "{\"bufferView\":6,\"componentType\":5126,\"count\":2,\"type\":\"VEC4\"},{\"bufferView\":7,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"}]");
    swap("\"channels\":[{\"sampler\":0,\"target\":{\"node\":2,\"path\":\"rotation\"}}]",
         "\"channels\":[{\"sampler\":0,\"target\":{\"node\":2,\"path\":\"rotation\"}},{\"sampler\":1,\"target\":{\"node\":1,\"path\":\"translation\"}}]");
    swap("\"samplers\":[{\"input\":5,\"output\":6,\"interpolation\":\"LINEAR\"}]",
         "\"samplers\":[{\"input\":5,\"output\":6,\"interpolation\":\"LINEAR\"},{\"input\":5,\"output\":7,\"interpolation\":\"LINEAR\"}]");
    strip.writeGlb("/tmp/molten_walker.glb");
    kit::registerKit();
    Stage stage;
    stage.assetRoot = "/tmp";
    Entity walker = stage.scene.create("Walker");
    Transform placed;
    placed.rotation = Quat(radians(90.0f), Vec3{0, 1, 0});                   // turned: forward (-z) is -x
    stage.scene.add(walker, placed);
    Animator animator;
    animator.model = "molten_walker.glb";
    animator.clip = "Bend";
    animator.blendTime = 0;
    animator.rootMotion = true;
    stage.scene.add(walker, animator);
    for (int i = 0; i < 30; i++) stage.advance(1.0 / 60.0);                  // half a second: a meter
    Vec3 at = stage.scene.get<Transform>(walker)->position;
    CHECK(std::abs(at.x + 1) < 0.05f && std::abs(at.z) < 1e-3f);
    // The body stays over its entity: the sheet's corner where it is when standing.
    CHECK(simd::length(stage.shown.positions[0] - Vec3{0, 0, 0}) < 1e-3f);
    // Round again: it goes on forward, it does not jump back.
    for (int i = 0; i < 60; i++) stage.advance(1.0 / 60.0);                  // a second and a half: three meters
    at = stage.scene.get<Transform>(walker)->position;
    CHECK(std::abs(at.x + 3) < 0.1f);
}

TEST(animation_layers_play_over_part_of_the_body) {
    Strip strip;
    strip.writeGlb("/tmp/molten_strip.glb");
    // The base stands still; a layer bends the second bone (its clip, played to its end, turns it
    // a quarter round), by as much as "aim" says.
    text::writeFile("/tmp/molten_layers.animator", R"(molten-animator 1
parameter float aim 1
state Stand
  motion Bend
  speed 0
layer Arms mask Bone1 weight aim
  state Bent loops 0
    motion Bend
layer Breath additive weight 0
  state Breathe loops 0
    motion Bend
)");
    std::string error;
    std::optional<AnimatorController> parsed = AnimatorController::parse(*text::readFile("/tmp/molten_layers.animator"), &error);
    CHECK(parsed && parsed->layers.size() == 2 && parsed->layers[0].mask[0] == "Bone1" && parsed->layers[0].weightBy == "aim" &&
          parsed->layers[1].additive && parsed->layers[1].weight == 0 && parsed->layers[0].start == "Bent");
    if (!parsed) {
        printf("    %s\n", error.c_str());
        return;
    }
    std::optional<AnimatorController> again = AnimatorController::parse(parsed->toText(), &error);
    CHECK(again && again->toText() == parsed->toText() && again->states.size() == 1);
    CHECK(!AnimatorController::parse("molten-animator 1\nstate A\n  motion X\nlayer L weight nothing\n  state B\n", &error) &&
          error.find("nothing") != std::string::npos);

    kit::registerKit();
    Stage stage;
    stage.assetRoot = "/tmp";
    Entity entity = stage.scene.create("Sheet");
    stage.scene.add(entity, Transform{});
    Animator animator;
    animator.model = "molten_strip.glb";
    animator.controller = "molten_layers.animator";
    stage.scene.add(entity, animator);
    for (int i = 0; i < 80; i++) stage.advance(1.0 / 60.0);
    Animator* playing = stage.scene.get<Animator>(entity);
    auto angle = [&](int node) { return 2 * std::acos(std::clamp(std::abs(playing->pose.rotations[size_t(node)].vector.w), 0.0f, 1.0f)); };
    CHECK(std::abs(angle(2) - 1.5707963f) < 0.02f);          // the bone of the mask: bent
    CHECK(angle(1) < 1e-4f);                                 // the one above it: as the base has it
    playing->setFloat("aim", 0.5f);
    stage.advance(1.0 / 60.0);
    CHECK(std::abs(angle(2) - 0.785398f) < 0.02f);           // half way
    // Added: a quarter round on top of a quarter round.
    playing->setFloat("aim", 1);
    std::string added = R"(molten-animator 1
state Stand
  motion Bend
  speed 0
layer Bend
  state Bent loops 0
    motion Bend
layer Breath additive
  state Breathe loops 0
    motion Bend
)";
    text::writeFile("/tmp/molten_layers2.animator", added);
    playing->controller = "molten_layers2.animator";
    for (int i = 0; i < 80; i++) stage.advance(1.0 / 60.0);
    CHECK(std::abs(angle(2) - 3.14159f) < 0.03f);
}
