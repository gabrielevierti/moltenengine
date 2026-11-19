// Math.hpp
// Vector, matrix and quaternion helpers on top of Apple's simd library, plus the
// small geometric types the engine needs: boxes, rays, planes, frustums, random numbers.
//
// Conventions:
//   - right-handed world, +Y up, a camera looks down its own -Z axis
//   - matrices are column-major and multiply column vectors: world = model * local
//   - clip-space depth runs from 0 (near) to 1 (far), as Metal expects
#pragma once

#include <simd/simd.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace molten {

using Vec2 = simd::float2;
using Vec3 = simd::float3;
using Vec4 = simd::float4;
using Mat3 = simd::float3x3;
using Mat4 = simd::float4x4;
using Quat = simd::quatf;

inline constexpr float kPi = 3.14159265358979323846f;

// ---------------------------------------------------------------- scalars

inline float radians(float degrees) { return degrees * (kPi / 180.0f); }
inline float degrees(float radians) { return radians * (180.0f / kPi); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float saturate(float x) { return std::min(std::max(x, 0.0f), 1.0f); }

inline float smoothstep(float edge0, float edge1, float x) {
    float t = saturate((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

/// Moves `current` toward `target` at a rate that does not depend on the frame rate.
/// `sharpness` is roughly "how many times per second the gap shrinks by a factor of e".
inline float damp(float current, float target, float sharpness, float dt) {
    return lerp(current, target, 1.0f - std::exp(-sharpness * dt));
}

inline float easeOutCubic(float t) { float u = 1.0f - saturate(t); return 1.0f - u * u * u; }
inline float easeInOutCubic(float t) {
    t = saturate(t);
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}
inline float easeOutBack(float t) {
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    float u = saturate(t) - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

// ---------------------------------------------------------------- vectors

inline Vec3 xyz(Vec4 v) { return Vec3{v.x, v.y, v.z}; }
inline Vec4 vec4(Vec3 v, float w) { return Vec4{v.x, v.y, v.z, w}; }
inline Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
inline Vec4 lerp(Vec4 a, Vec4 b, float t) { return a + (b - a) * t; }
inline Vec3 damp(Vec3 current, Vec3 target, float sharpness, float dt) {
    return lerp(current, target, 1.0f - std::exp(-sharpness * dt));
}

/// normalize() that returns `fallback` instead of NaN for a zero-length vector.
inline Vec2 safeNormalize(Vec2 v, Vec2 fallback) {
    float lengthSquared = simd::dot(v, v);
    return lengthSquared > 1e-12f ? v / std::sqrt(lengthSquared) : fallback;
}

inline Vec3 safeNormalize(Vec3 v, Vec3 fallback = Vec3{0, 1, 0}) {
    float lengthSquared = simd::length_squared(v);
    return lengthSquared > 1e-12f ? v / std::sqrt(lengthSquared) : fallback;
}

// ---------------------------------------------------------------- quaternions

inline Quat axisAngle(Vec3 axis, float angle) { return Quat(angle, axis); }
inline Vec3 rotate(Quat q, Vec3 v) { return simd_act(q, v); }

/// Yaw around Y, then pitch around X, then roll around Z: the order a first-person camera wants.
inline Quat eulerAngles(float pitch, float yaw, float roll = 0.0f) {
    return Quat(yaw, Vec3{0, 1, 0}) * Quat(pitch, Vec3{1, 0, 0}) * Quat(roll, Vec3{0, 0, 1});
}

/// The reverse of eulerAngles(): pitch, yaw and roll (as x, y, z) of a rotation. What an
/// editor shows, because three angles are easier to read than a quaternion.
inline Vec3 toEulerAngles(Quat q) {
    Mat3 m(q);
    // With R = yaw * pitch * roll, the third column is (sin y cos p, -sin p, cos y cos p)
    // and the second row starts with (cos p sin r, cos p cos r).
    float sinPitch = std::min(std::max(-m.columns[2].y, -1.0f), 1.0f);
    float pitch = std::asin(sinPitch);
    if (std::abs(sinPitch) > 0.9999f) {
        // Looking straight up or down: yaw and roll turn around the same axis. Call it all yaw.
        return Vec3{pitch, std::atan2(-m.columns[0].z, m.columns[0].x), 0.0f};
    }
    return Vec3{pitch, std::atan2(m.columns[2].x, m.columns[2].z), std::atan2(m.columns[0].y, m.columns[1].y)};
}

/// The rotation that turns -Z into `forward` while keeping +Y as close to `up` as possible.
inline Quat lookRotation(Vec3 forward, Vec3 up = Vec3{0, 1, 0}) {
    Vec3 f = safeNormalize(forward, Vec3{0, 0, -1});
    Vec3 right = simd::cross(f, up);
    if (simd::length_squared(right) < 1e-8f) {              // forward is parallel to up
        right = simd::cross(f, Vec3{1, 0, 0});
    }
    right = simd::normalize(right);
    Vec3 realUp = simd::cross(right, f);
    return Quat(Mat3(right, realUp, -f));
}

// ---------------------------------------------------------------- matrices

inline Mat4 identity() { return Mat4(1.0f); }

inline Mat4 translation(Vec3 t) {
    return Mat4(Vec4{1, 0, 0, 0}, Vec4{0, 1, 0, 0}, Vec4{0, 0, 1, 0}, Vec4{t.x, t.y, t.z, 1});
}

inline Mat4 scaling(Vec3 s) { return Mat4(Vec4{s.x, s.y, s.z, 1}); }
inline Mat4 rotation(Quat q) { return Mat4(q); }

/// translation * rotation * scale, written out so that no matrix product is needed.
inline Mat4 compose(Vec3 position, Quat orientation, Vec3 scale) {
    Mat3 r(orientation);
    return Mat4(vec4(r.columns[0] * scale.x, 0), vec4(r.columns[1] * scale.y, 0),
                vec4(r.columns[2] * scale.z, 0), vec4(position, 1));
}

/// Right-handed perspective projection with depth from 0 to 1. `fovY` is in radians.
inline Mat4 perspective(float fovY, float aspect, float nearZ, float farZ) {
    float y = 1.0f / std::tan(fovY * 0.5f);
    float x = y / aspect;
    float z = farZ / (nearZ - farZ);
    return Mat4(Vec4{x, 0, 0, 0}, Vec4{0, y, 0, 0}, Vec4{0, 0, z, -1}, Vec4{0, 0, z * nearZ, 0});
}

/// Right-handed orthographic projection with depth from 0 to 1.
inline Mat4 orthographic(float left, float right, float bottom, float top, float nearZ, float farZ) {
    return Mat4(Vec4{2.0f / (right - left), 0, 0, 0},
                Vec4{0, 2.0f / (top - bottom), 0, 0},
                Vec4{0, 0, 1.0f / (nearZ - farZ), 0},
                Vec4{(left + right) / (left - right), (top + bottom) / (bottom - top), nearZ / (nearZ - farZ), 1});
}

/// View matrix for a camera at `eye` looking at `target`.
inline Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up = Vec3{0, 1, 0}) {
    Vec3 f = simd::normalize(target - eye);
    Vec3 s = simd::normalize(simd::cross(f, up));
    Vec3 u = simd::cross(s, f);
    return Mat4(Vec4{s.x, u.x, -f.x, 0}, Vec4{s.y, u.y, -f.y, 0}, Vec4{s.z, u.z, -f.z, 0},
                Vec4{-simd::dot(s, eye), -simd::dot(u, eye), simd::dot(f, eye), 1});
}

/// The reverse of compose(): position, rotation and scale of a matrix made of those three.
/// Shear is lost, and a mirrored matrix comes back with a negative x scale.
inline void decompose(const Mat4& m, Vec3& position, Quat& orientation, Vec3& scale) {
    position = Vec3{m.columns[3].x, m.columns[3].y, m.columns[3].z};
    Vec3 x{m.columns[0].x, m.columns[0].y, m.columns[0].z};
    Vec3 y{m.columns[1].x, m.columns[1].y, m.columns[1].z};
    Vec3 z{m.columns[2].x, m.columns[2].y, m.columns[2].z};
    scale = Vec3{simd::length(x), simd::length(y), simd::length(z)};
    if (simd::dot(simd::cross(x, y), z) < 0) scale.x = -scale.x;
    if (std::abs(scale.x) < 1e-8f || scale.y < 1e-8f || scale.z < 1e-8f) {
        orientation = Quat{};
        return;
    }
    orientation = simd::normalize(Quat(Mat3(x / scale.x, y / scale.y, z / scale.z)));
}

inline Vec3 transformPoint(const Mat4& m, Vec3 p) {
    Vec4 r = m * vec4(p, 1);
    return xyz(r) / r.w;
}

inline Vec3 transformDirection(const Mat4& m, Vec3 d) { return xyz(m * vec4(d, 0)); }

/// The matrix that transforms normals correctly even under non-uniform scale.
inline Mat4 normalMatrix(const Mat4& model) { return simd::transpose(simd::inverse(model)); }

// ---------------------------------------------------------------- shapes

struct AABB {
    Vec3 min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec3 max{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};

    AABB() = default;
    AABB(Vec3 minimum, Vec3 maximum) : min(minimum), max(maximum) {}
    static AABB fromCenter(Vec3 center, Vec3 halfExtents) { return AABB(center - halfExtents, center + halfExtents); }

    bool isEmpty() const { return min.x > max.x; }
    Vec3 center() const { return (min + max) * 0.5f; }
    Vec3 halfExtents() const { return (max - min) * 0.5f; }
    void expand(Vec3 p) { min = simd::min(min, p); max = simd::max(max, p); }
    void expand(const AABB& other) { min = simd::min(min, other.min); max = simd::max(max, other.max); }
    bool contains(Vec3 p) const { return simd::all(p >= min) && simd::all(p <= max); }
    bool intersects(const AABB& other) const { return simd::all(min <= other.max) && simd::all(max >= other.min); }

    /// The box that encloses this one after `m` is applied (Arvo's method: no corner loop needed).
    AABB transformed(const Mat4& m) const {
        Vec3 c = transformPoint(m, center());
        Vec3 h = halfExtents();
        Vec3 e = simd::abs(xyz(m.columns[0])) * h.x + simd::abs(xyz(m.columns[1])) * h.y + simd::abs(xyz(m.columns[2])) * h.z;
        return AABB(c - e, c + e);
    }
};

struct Ray {
    Vec3 origin{0, 0, 0};
    Vec3 direction{0, 0, -1};      // must be normalized
    Vec3 at(float t) const { return origin + direction * t; }
};

/// All points p with dot(normal, p) + d = 0. The normal points to the "inside" half-space.
struct Plane {
    Vec3 normal{0, 1, 0};
    float d = 0;
    float signedDistance(Vec3 p) const { return simd::dot(normal, p) + d; }
};

/// The six planes of a view volume, extracted from a view-projection matrix.
struct Frustum {
    Plane planes[6];               // left, right, bottom, top, near, far

    Frustum() = default;
    explicit Frustum(const Mat4& viewProjection) {
        auto row = [&](int i) {
            return Vec4{viewProjection.columns[0][i], viewProjection.columns[1][i],
                        viewProjection.columns[2][i], viewProjection.columns[3][i]};
        };
        Vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
        Vec4 raw[6] = {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r2, r3 - r2};   // depth 0..1: the near plane is row 2 alone
        for (int i = 0; i < 6; i++) {
            float length = simd::length(xyz(raw[i]));
            planes[i] = Plane{xyz(raw[i]) / length, raw[i].w / length};
        }
    }

    /// False only when the box is certainly outside. A box near a corner may pass even
    /// though it is outside, which costs a draw call but never drops a visible object.
    /// With `testNearPlane` false, whatever is in front of the near plane counts as inside:
    /// shadow maps want that, because anything between the light and the view casts a shadow.
    bool intersects(const AABB& box, bool testNearPlane = true) const {
        for (int i = 0; i < 6; i++) {
            if (i == 4 && !testNearPlane) continue;
            const Plane& plane = planes[i];
            Vec3 farthest{plane.normal.x >= 0 ? box.max.x : box.min.x,
                          plane.normal.y >= 0 ? box.max.y : box.min.y,
                          plane.normal.z >= 0 ? box.max.z : box.min.z};
            if (plane.signedDistance(farthest) < 0) return false;
        }
        return true;
    }
};

/// Distance along the ray to a sphere, or nothing.
inline std::optional<float> intersectSphere(const Ray& ray, Vec3 center, float radius) {
    Vec3 oc = ray.origin - center;
    float b = simd::dot(oc, ray.direction);
    float c = simd::dot(oc, oc) - radius * radius;
    float h = b * b - c;
    if (h < 0) return std::nullopt;
    h = std::sqrt(h);
    float t = -b - h;
    if (t < 0) t = -b + h;                                  // the origin is inside the sphere
    if (t < 0) return std::nullopt;
    return t;
}

/// Distance along the ray to an axis-aligned box (slab method), or nothing.
inline std::optional<float> intersectAABB(const Ray& ray, const AABB& box) {
    float tMin = 0.0f, tMax = std::numeric_limits<float>::max();
    for (int axis = 0; axis < 3; axis++) {
        float origin = ray.origin[axis], direction = ray.direction[axis];
        if (std::abs(direction) < 1e-8f) {
            if (origin < box.min[axis] || origin > box.max[axis]) return std::nullopt;
            continue;
        }
        float t1 = (box.min[axis] - origin) / direction;
        float t2 = (box.max[axis] - origin) / direction;
        if (t1 > t2) std::swap(t1, t2);
        tMin = std::max(tMin, t1);
        tMax = std::min(tMax, t2);
        if (tMin > tMax) return std::nullopt;
    }
    return tMin;
}

/// Distance along the ray to a plane, or nothing if the ray is parallel or points away.
inline std::optional<float> intersectPlane(const Ray& ray, const Plane& plane) {
    float denominator = simd::dot(plane.normal, ray.direction);
    if (std::abs(denominator) < 1e-8f) return std::nullopt;
    float t = -plane.signedDistance(ray.origin) / denominator;
    if (t < 0) return std::nullopt;
    return t;
}

// ---------------------------------------------------------------- random numbers

// ---------------------------------------------------------------- noise

/// A repeatable random number in [0, 1) for a point of the integer grid. The same
/// point and seed always give the same number, on every machine.
inline float hashNoise(int x, int y, uint32_t seed = 0) {
    uint32_t h = uint32_t(x) * 0x8da6b343u + uint32_t(y) * 0xd8163841u + seed * 0xcb1ab31fu;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return float(h & 0xffffffu) / float(0x1000000);
}

/// Smooth noise in [0, 1): random values at the corners of a unit grid, blended in
/// between. Looks like soft blobs about one unit wide.
inline float valueNoise(Vec2 p, uint32_t seed = 0) {
    float fx = std::floor(p.x), fy = std::floor(p.y);
    int x = int(fx), y = int(fy);
    float tx = p.x - fx, ty = p.y - fy;
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    float top = lerp(hashNoise(x, y, seed), hashNoise(x + 1, y, seed), tx);
    float bottom = lerp(hashNoise(x, y + 1, seed), hashNoise(x + 1, y + 1, seed), tx);
    return lerp(top, bottom, ty);
}

/// Several layers of valueNoise, each twice as fine and half as strong as the one
/// before, scaled back to [0, 1). The usual recipe for terrain, clouds and dirt.
inline float fractalNoise(Vec2 p, int octaves = 4, uint32_t seed = 0) {
    float sum = 0, strength = 0.5f, total = 0;
    for (int i = 0; i < octaves; i++) {
        sum += strength * valueNoise(p, seed + uint32_t(i));
        total += strength;
        p = p * 2.0f;
        strength *= 0.5f;
    }
    return total > 0 ? sum / total : 0;
}

/// A small, fast, seedable generator (PCG32). The same seed gives the same sequence on
/// every machine and in both the C++ and the Swift engine, which keeps gameplay and
/// tests reproducible.
struct Random {
    uint64_t state = 0;
    uint64_t increment = 1442695040888963407ULL;

    explicit Random(uint64_t seed = 0x853c49e6748fea9bULL) {
        nextUInt();
        state += seed;
        nextUInt();
    }

    uint32_t nextUInt() {
        uint64_t old = state;
        state = old * 6364136223846793005ULL + increment;
        uint32_t xorshifted = uint32_t(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = uint32_t(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }

    /// Uniform in [0, 1).
    float next() { return float(nextUInt() >> 8) * (1.0f / 16777216.0f); }
    float range(float low, float high) { return low + (high - low) * next(); }
    /// Uniform integer in [low, high).
    int rangeInt(int low, int high) { return low + int(nextUInt() % uint32_t(std::max(high - low, 1))); }

    Vec3 onUnitSphere() {
        float z = range(-1.0f, 1.0f);
        float angle = range(0.0f, 2.0f * kPi);
        float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        return Vec3{r * std::cos(angle), r * std::sin(angle), z};
    }

    Vec3 insideUnitSphere() {
        Vec3 direction = onUnitSphere();        // two statements: C++ does not fix the order of operands
        return direction * std::cbrt(next());
    }

    /// A unit vector within `halfAngle` radians of `direction`.
    Vec3 inCone(Vec3 direction, float halfAngle) {
        float cosTheta = range(std::cos(halfAngle), 1.0f);
        float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
        float phi = range(0.0f, 2.0f * kPi);
        Vec3 w = safeNormalize(direction);
        Vec3 helper = std::abs(w.y) < 0.99f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        Vec3 u = simd::normalize(simd::cross(helper, w));
        Vec3 v = simd::cross(w, u);
        return u * (sinTheta * std::cos(phi)) + v * (sinTheta * std::sin(phi)) + w * cosTheta;
    }
};

}  // namespace molten
