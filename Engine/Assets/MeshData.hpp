// MeshData.hpp
// A mesh on the CPU: positions, normals, tangents, texture coordinates and triangle
// indices. It knows nothing about Metal, so it can be built, edited and tested
// anywhere. Rendering/Mesh uploads it to the GPU.
//
// Triangles are counter-clockwise when seen from outside. Texture coordinates have
// their origin at the top left. A tangent points toward increasing u; its w is the
// sign that makes  cross(normal, tangent) * w  point toward the top of the texture.
#pragma once

#include "Math/Math.hpp"

#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace molten {

struct MeshData {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec4> tangents;
    std::vector<Vec2> uvs;
    std::vector<uint32_t> indices;

    /// Floats per vertex in interleaved(): position 3, normal 3, tangent 4, uv 2.
    static constexpr size_t kFloatsPerVertex = 12;

    size_t vertexCount() const { return positions.size(); }
    AABB bounds() const;

    /// The vertex buffer contents, in the layout the mesh shaders expect.
    std::vector<float> interleaved() const;

    /// Smooth normals from the triangles, weighted by triangle area.
    void computeNormals();

    /// Tangents from the texture coordinates. Needs normals.
    void computeTangents();

    // ---- primitives, centered on the origin

    /// A box. Normally every face shows the whole texture once. With `uvPerUnit` above zero
    /// the texture instead repeats that many times per world unit, so that boxes of
    /// different sizes can share one material without stretching it.
    static MeshData cube(Vec3 size = Vec3{1, 1, 1}, float uvPerUnit = 0.0f);
    /// A flat grid in the XZ plane, facing +Y. `uvScale` repeats the texture that many times.
    static MeshData plane(float width = 1, float depth = 1, int subdivisions = 1, float uvScale = 1);
    static MeshData sphere(float radius = 0.5f, int segments = 32, int rings = 16);
    /// A capped cylinder around the Y axis.
    static MeshData cylinder(float radius = 0.5f, float height = 1, int segments = 32);
    /// A cone around the Y axis, point up, with a flat base.
    static MeshData cone(float radius = 0.5f, float height = 1, int segments = 32);
    /// Terrain: a grid in the XZ plane whose height at (x, z) is given by a function.
    /// `uvScale` repeats the texture that many times across the whole grid.
    static MeshData heightfield(float width, float depth, int columns, int rows, const std::function<float(float, float)>& height,
                                float uvScale = 1);

    /// Parses Wavefront OBJ text: v, vt, vn and f records, polygons of any size,
    /// negative indices. Materials and groups are ignored. Returns nothing if the
    /// text contains no faces.
    static std::optional<MeshData> parseOBJ(std::string_view text);
};

}  // namespace molten
