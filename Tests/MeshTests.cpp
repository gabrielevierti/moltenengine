#include "Test.hpp"

#include "Assets/MeshData.hpp"

using namespace molten;

namespace {

/// Every triangle must be counter-clockwise seen from outside, that is, its geometric
/// normal must agree with the vertex normals.
bool windingMatchesNormals(const MeshData& mesh) {
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        uint32_t a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
        Vec3 face = simd::cross(mesh.positions[b] - mesh.positions[a], mesh.positions[c] - mesh.positions[a]);
        if (simd::length(face) < 1e-9f) continue;       // degenerate triangle at a pole
        Vec3 average = mesh.normals[a] + mesh.normals[b] + mesh.normals[c];
        if (simd::dot(face, average) <= 0) return false;
    }
    return true;
}

bool tangentsAreValid(const MeshData& mesh) {
    for (size_t i = 0; i < mesh.vertexCount(); i++) {
        Vec3 t = xyz(mesh.tangents[i]);
        if (std::abs(simd::length(t) - 1.0f) > 1e-3f) return false;
        if (std::abs(simd::dot(t, mesh.normals[i])) > 1e-3f) return false;
        if (std::abs(std::abs(mesh.tangents[i].w) - 1.0f) > 1e-6f) return false;
    }
    return true;
}

}  // namespace

TEST(cube_can_repeat_its_texture_per_world_unit) {
    MeshData box = MeshData::cube(Vec3{2, 1, 4}, 0.5f);
    CHECK(windingMatchesNormals(box));
    CHECK(tangentsAreValid(box));
    // The top face (+Y, the third one) is 2 wide and 4 deep: 1 repeat by 2 repeats.
    Vec2 largest{0, 0};
    for (size_t i = 8; i < 12; i++) largest = simd::max(largest, box.uvs[i]);
    CHECK_NEAR(largest.x, 1.0f, 1e-6f);
    CHECK_NEAR(largest.y, 2.0f, 1e-6f);
    // The +X face is 4 wide (along z) and 1 tall.
    largest = Vec2{0, 0};
    for (size_t i = 0; i < 4; i++) largest = simd::max(largest, box.uvs[i]);
    CHECK_NEAR(largest.x, 2.0f, 1e-6f);
    CHECK_NEAR(largest.y, 0.5f, 1e-6f);
}

TEST(cube_has_24_vertices_and_outward_faces) {
    MeshData cube = MeshData::cube(Vec3{2, 4, 6});
    CHECK(cube.vertexCount() == 24);
    CHECK(cube.indices.size() == 36);
    CHECK(windingMatchesNormals(cube));
    CHECK(tangentsAreValid(cube));
    AABB bounds = cube.bounds();
    CHECK_NEAR(bounds.max.x, 1, 1e-6); CHECK_NEAR(bounds.max.y, 2, 1e-6); CHECK_NEAR(bounds.min.z, -3, 1e-6);
    CHECK(cube.interleaved().size() == 24 * MeshData::kFloatsPerVertex);
}

TEST(primitives_have_consistent_winding_and_tangents) {
    auto hills = [](float x, float z) { return std::sin(x * 0.7f) * 0.8f + std::cos(z * 0.5f) * 0.6f; };
    for (const MeshData& mesh : {MeshData::plane(4, 4, 3, 2), MeshData::sphere(1.5f, 24, 12), MeshData::cylinder(1, 2, 20),
                                 MeshData::cone(1, 2, 20), MeshData::heightfield(12, 8, 24, 16, hills, 3)}) {
        CHECK(windingMatchesNormals(mesh));
        CHECK(tangentsAreValid(mesh));
        CHECK(mesh.normals.size() == mesh.vertexCount() && mesh.uvs.size() == mesh.vertexCount());
        for (uint32_t index : mesh.indices) CHECK(index < mesh.vertexCount());
    }
    MeshData sphere = MeshData::sphere(1.5f, 24, 12);
    for (Vec3 p : sphere.positions) CHECK_NEAR(simd::length(p), 1.5, 1e-4);
}

TEST(heightfield_follows_its_function_and_leans_its_normals_downhill) {
    // A ramp that rises one unit for every two units of x.
    MeshData ramp = MeshData::heightfield(10, 6, 5, 3, [](float x, float) { return x * 0.5f; });
    CHECK(ramp.vertexCount() == 6 * 4);
    CHECK(ramp.indices.size() == 5 * 3 * 6);
    for (size_t i = 0; i < ramp.vertexCount(); i++) {
        CHECK_NEAR(ramp.positions[i].y, ramp.positions[i].x * 0.5f, 1e-5);
        CHECK_NEAR(ramp.normals[i].x, -0.4472f, 1e-3);     // perpendicular to the slope: (-1, 2, 0) / sqrt(5)
        CHECK_NEAR(ramp.normals[i].y, 0.8944f, 1e-3);
    }
    AABB bounds = ramp.bounds();
    CHECK_NEAR(bounds.min.x, -5, 1e-5); CHECK_NEAR(bounds.max.z, 3, 1e-5); CHECK_NEAR(bounds.max.y, 2.5f, 1e-5);

    MeshData cone = MeshData::cone(1, 2, 16);
    CHECK_NEAR(cone.bounds().max.y, 1, 1e-6); CHECK_NEAR(cone.bounds().min.y, -1, 1e-6);
}

TEST(tangent_frames_point_along_u_and_up_the_texture) {
    // For every built-in primitive, the tangents written by hand must agree with the
    // ones derived from the texture coordinates.
    for (MeshData mesh : {MeshData::cube(), MeshData::plane(2, 2, 2), MeshData::sphere(1, 16, 8), MeshData::cylinder(1, 2, 16)}) {
        std::vector<Vec4> authored = mesh.tangents;
        mesh.computeTangents();
        int compared = 0;
        for (size_t i = 0; i < mesh.vertexCount(); i++) {
            // Skip the poles and cap centers, where the direction of u is not defined.
            if (std::abs(mesh.normals[i].y) > 0.999f && std::abs(mesh.positions[i].x) < 1e-4f && std::abs(mesh.positions[i].z) < 1e-4f) continue;
            CHECK(simd::dot(xyz(authored[i]), xyz(mesh.tangents[i])) > 0.9f);
            CHECK_NEAR(authored[i].w, mesh.tangents[i].w, 1e-6);
            compared++;
        }
        CHECK(compared > 0);
    }
}

TEST(compute_normals_matches_sphere_normals) {
    MeshData sphere = MeshData::sphere(1, 32, 16);
    std::vector<Vec3> exact = sphere.normals;
    sphere.computeNormals();
    for (size_t i = 0; i < sphere.vertexCount(); i++) {
        if (std::abs(exact[i].y) > 0.99f) continue;      // the seam and the poles are not shared, skip them
        CHECK(simd::dot(exact[i], sphere.normals[i]) > 0.95f);
    }
}

TEST(obj_parser_reads_quads_and_all_index_forms) {
    const char* text =
        "# a unit quad and a triangle\n"
        "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
        "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
        "vn 0 0 1\n"
        "f 1/1/1 2/2/1 3/3/1 4/4/1\n"
        "f -4//1 -3//1 -2//1\n";
    auto mesh = MeshData::parseOBJ(text);
    CHECK(mesh.has_value());
    CHECK(mesh->indices.size() == 9);                    // the quad becomes two triangles
    CHECK(mesh->vertexCount() == 7);                     // 4 + 3: different texture coordinates, different vertices
    CHECK_NEAR(mesh->uvs[0].y, 1, 1e-6);                 // v is flipped: OBJ's origin is at the bottom
    CHECK_NEAR(mesh->normals[0].z, 1, 1e-6);
    CHECK(windingMatchesNormals(*mesh));
    CHECK(mesh->tangents.size() == mesh->vertexCount());
    CHECK_NEAR(mesh->tangents[0].x, 1, 1e-4);            // u grows along +x
    CHECK_NEAR(mesh->tangents[0].w, 1, 1e-6);
}

TEST(obj_parser_computes_missing_normals_and_rejects_empty_files) {
    auto mesh = MeshData::parseOBJ("v 0 0 0\nv 1 0 0\nv 0 0 -1\nf 1 2 3\n");
    CHECK(mesh.has_value());
    CHECK_NEAR(mesh->normals[0].y, 1, 1e-5);
    CHECK(!MeshData::parseOBJ("v 0 0 0\n# no faces\n").has_value());
}
