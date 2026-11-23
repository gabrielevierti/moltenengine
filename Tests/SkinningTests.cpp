// Posing a model with bones on the GPU (Rendering/Skinning.hpp) must give what posing it on the
// processor gave: the same vertices, in the same order, so that the mesh made at rest can simply
// have its vertices moved. The kernel's arithmetic is SkinSource::poseOnCpu's (Mesh.metal).
#include "Test.hpp"
#include "StarterFigure.hpp"

#include "Assets/Gltf.hpp"
#include "Rendering/Skinning.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>

using namespace molten;

namespace {

std::optional<GltfModel> walkingFigure() {
    starter::Figure figure = starter::makeFigure();
    std::filesystem::path folder = std::filesystem::temp_directory_path() / "molten-skinning";
    std::filesystem::create_directories(folder);
    std::string path = (folder / "Figure.glb").string();
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(figure.glb.data()), long(figure.glb.size()));
    return GltfModel::load(path);
}

float largestGap(const std::vector<float>& posed, const MeshData& expected, int first, int count) {
    float gap = 0;
    for (size_t v = 0; v < expected.positions.size(); v++) {
        const float* at = &posed[v * MeshData::kFloatsPerVertex + size_t(first)];
        const float want[3] = {count == 3 && first == 0 ? expected.positions[v].x : expected.normals[v].x,
                               count == 3 && first == 0 ? expected.positions[v].y : expected.normals[v].y,
                               count == 3 && first == 0 ? expected.positions[v].z : expected.normals[v].z};
        for (int i = 0; i < 3; i++) gap = std::max(gap, std::abs(at[i] - want[i]));
    }
    return gap;
}

}  // namespace

TEST(a_model_posed_on_the_gpu_is_where_the_processor_put_it) {
    std::optional<GltfModel> model = walkingFigure();
    CHECK(model.has_value());
    if (!model) return;
    std::shared_ptr<SkinSource> source = SkinSource::build(*model);
    MeshData rest = model->assembled(model->restPose());
    CHECK(source->vertexCount == rest.positions.size() && source->vertexCount > 0);
    CHECK(source->paletteSize == model->nodes.size() + model->skins[0].joints.size());

    // Half way through a step of the walk, and at the top of a jump.
    for (const char* clip : {"Walk", "Jump"}) {
        GltfPose pose = model->restPose();
        model->sample(model->findAnimation(clip), 0.4f, pose);
        MeshData expected = model->assembled(pose);
        std::vector<Mat4> palette;
        source->palette(*model, pose, palette);
        std::vector<float> posed;
        source->poseOnCpu(palette, posed);
        CHECK(posed.size() == expected.positions.size() * MeshData::kFloatsPerVertex);
        // Positions to a tenth of a millimeter, normals to a thousandth.
        CHECK(largestGap(posed, expected, 0, 3) < 1e-4f);
        CHECK(largestGap(posed, expected, 3, 4) < 1e-3f);
        // And the box the renderer culls with holds every vertex.
        AABB box = source->bounds(palette);
        bool inside = true;
        for (const Vec3& p : expected.positions) inside = inside && simd::all(p >= box.min - 1e-4f) && simd::all(p <= box.max + 1e-4f);
        CHECK(inside);
        // ...without being much bigger than the mesh (a box per bone, each turned with it).
        Vec3 extent = box.max - box.min, tight = expected.bounds().max - expected.bounds().min;
        CHECK(extent.x < tight.x * 1.6f + 0.1f && extent.y < tight.y * 1.6f + 0.1f && extent.z < tight.z * 1.6f + 0.1f);
    }
}

TEST(a_model_without_bones_is_posed_by_its_nodes) {
    // Two parts on two nodes, the second under the first: no skin, every vertex follows its node.
    GltfModel model;
    model.nodes.resize(2);
    model.nodes[0].mesh = 0;
    model.nodes[0].children = {1};
    model.nodes[1].parent = 0;
    model.nodes[1].mesh = 0;
    model.nodes[1].translation = Vec3{0, 2, 0};
    GltfPrimitive triangle;
    triangle.mesh.positions = {Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 1, 0}};
    triangle.mesh.normals = {Vec3{0, 0, 1}, Vec3{0, 0, 1}, Vec3{0, 0, 1}};
    triangle.mesh.uvs = {Vec2{0, 0}, Vec2{1, 0}, Vec2{0, 1}};
    triangle.mesh.indices = {0, 1, 2};
    model.meshes.push_back({triangle});
    GltfPose pose = model.restPose();
    pose.translations[0] = Vec3{5, 0, 0};
    pose.rotations[1] = Quat(radians(90.0f), Vec3{0, 0, 1});
    std::shared_ptr<SkinSource> source = SkinSource::build(model);
    std::vector<Mat4> palette;
    source->palette(model, pose, palette);
    std::vector<float> posed;
    source->poseOnCpu(palette, posed);
    MeshData expected = model.assembled(pose);
    CHECK(source->vertexCount == 6 && expected.positions.size() == 6);
    CHECK(largestGap(posed, expected, 0, 3) < 1e-5f);
}
