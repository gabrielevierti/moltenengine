// SoftRender.cpp
#include "SoftRender.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace molten::editor {

namespace {

const float kPi = 3.14159265f;

float toLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
float toScreen(float c) {
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

/// Bright light folded into what a screen can show, the way the renderer's filmic curve does.
Vec3 toneMapped(Vec3 c) {
    auto curve = [](float x) {
        x = std::max(x, 0.0f);
        return std::clamp((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f), 0.0f, 1.0f);
    };
    return Vec3{curve(c.x), curve(c.y), curve(c.z)};
}

Vec3 mix(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
float smooth(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

/// The sky in a direction. `blur` from 0 (as it is) to 1 (as a rough surface sees it: all of it at once).
Vec3 skyIn(Vec3 direction, float blur, Vec3 zenith, Vec3 horizon, Vec3 ground) {
    float width = 0.04f + 0.9f * blur;
    float up = smooth(-width, width, direction.y);
    Vec3 above = mix(horizon, zenith, std::pow(std::clamp(direction.y, 0.0f, 1.0f), 0.5f + blur));
    Vec3 below = mix(horizon * 0.5f, ground, std::pow(std::clamp(-direction.y, 0.0f, 1.0f), 0.4f + blur));
    return mix(below, above, up);
}

/// How much of the light reaches the picture: the scene is a sunny day, the picture a screen.
const float kExposure = 0.5f;

void put(SoftImage& image, int x, int y, Vec3 linear, float opacity) {
    Vec3 shown = toneMapped(linear * kExposure);
    uint8_t* pixel = &image.rgba[size_t(y * image.width + x) * 4];
    pixel[0] = uint8_t(toScreen(shown.x) * 255.0f + 0.5f);
    pixel[1] = uint8_t(toScreen(shown.y) * 255.0f + 0.5f);
    pixel[2] = uint8_t(toScreen(shown.z) * 255.0f + 0.5f);
    pixel[3] = uint8_t(std::clamp(opacity, 0.0f, 1.0f) * 255.0f + 0.5f);
}

/// How much of the light a surface mirrors, summed over a rough surface's facets, for
/// light arriving from all around (the fit used by renderers that cannot afford the sum).
Vec3 mirroredShare(Vec3 f0, float roughness, float facing) {
    Vec4 c0{-1, -0.0275f, -0.572f, 0.022f}, c1{1, 0.0425f, 1.04f, -0.04f};
    Vec4 r = c0 * roughness + c1;
    float a004 = std::min(r.x * r.x, std::exp2(-9.28f * facing)) * r.x + r.y;
    float scale = -1.04f * a004 + r.z, bias = 1.04f * a004 + r.w;
    return f0 * scale + Vec3{bias, bias, bias};
}

}  // namespace

Vec4 SoftImage::sample(float u, float v) const {
    if (isEmpty()) return Vec4{1, 1, 1, 1};
    u -= std::floor(u);
    v -= std::floor(v);
    float x = u * float(width) - 0.5f, y = v * float(height) - 0.5f;
    int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    float fx = x - float(x0), fy = y - float(y0);
    auto at = [&](int px, int py) {
        px = ((px % width) + width) % width;
        py = ((py % height) + height) % height;
        const uint8_t* p = &rgba[size_t(py * width + px) * 4];
        return Vec4{float(p[0]) / 255.0f, float(p[1]) / 255.0f, float(p[2]) / 255.0f, float(p[3]) / 255.0f};
    };
    Vec4 top = at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx;
    Vec4 bottom = at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx;
    return top * (1 - fy) + bottom * fy;
}

SoftImage renderBall(const BallLook& look, int size) {
    SoftImage image;
    image.width = image.height = std::max(size, 4);
    image.rgba.assign(size_t(image.width * image.height) * 4, 0);

    const Vec3 zenith{0.16f, 0.32f, 0.72f}, horizon{0.78f, 0.84f, 0.92f}, ground{0.11f, 0.1f, 0.09f};
    const Vec3 toSun = simd::normalize(Vec3{-0.5f, 0.72f, 0.5f});
    const Vec3 sun = Vec3{1.0f, 0.95f, 0.86f} * 3.2f;
    const Vec3 toEye{0, 0, 1};
    const float radius = float(image.width) * 0.5f - 1.0f, middle = float(image.width) * 0.5f;

    for (int y = 0; y < image.height; y++) {
        for (int x = 0; x < image.width; x++) {
            float px = (float(x) + 0.5f - middle) / radius, py = (middle - (float(y) + 0.5f)) / radius;
            float fromMiddle = std::sqrt(px * px + py * py);
            float coverage = std::clamp((1.0f - fromMiddle) * radius + 0.5f, 0.0f, 1.0f);      // a soft edge, one pixel wide
            if (coverage <= 0) continue;
            if (fromMiddle > 0.999f) {
                px *= 0.999f / fromMiddle;
                py *= 0.999f / fromMiddle;
            }
            Vec3 normal{px, py, std::sqrt(std::max(1.0f - px * px - py * py, 0.0f))};

            // Where on the ball: around it and from pole to pole, twice around so a picture is not stretched.
            float u = (std::atan2(normal.x, normal.z) / (2 * kPi) + 0.5f) * 2.0f * look.uvScale.x;
            float v = (std::acos(std::clamp(normal.y, -1.0f, 1.0f)) / kPi) * look.uvScale.y;

            Vec3 base{look.baseColor.x, look.baseColor.y, look.baseColor.z};
            float opacity = look.baseColor.w;
            if (look.baseColorPicture && !look.baseColorPicture->isEmpty()) {
                Vec4 texel = look.baseColorPicture->sample(u, v);
                base = base * Vec3{toLinear(texel.x), toLinear(texel.y), toLinear(texel.z)};
                opacity *= texel.w;
            }
            float metal = std::clamp(look.metallic, 0.0f, 1.0f), rough = std::clamp(look.roughness, 0.04f, 1.0f);
            if (look.metallicRoughnessPicture && !look.metallicRoughnessPicture->isEmpty()) {
                Vec4 texel = look.metallicRoughnessPicture->sample(u, v);
                rough = std::clamp(rough * texel.y, 0.04f, 1.0f);
                metal *= texel.z;
            }
            Vec3 glow = look.emissive;
            if (look.emissivePicture && !look.emissivePicture->isEmpty()) {
                Vec4 texel = look.emissivePicture->sample(u, v);
                glow = glow * Vec3{toLinear(texel.x), toLinear(texel.y), toLinear(texel.z)};
            }
            if (look.normalPicture && !look.normalPicture->isEmpty()) {
                // The surface leans as its normal map says: across is along the ball's equator, up toward its pole.
                Vec4 texel = look.normalPicture->sample(u, v);
                Vec3 across = simd::normalize(simd::cross(Vec3{0, 1, 0}, normal) + Vec3{1e-4f, 0, 0});
                Vec3 upward = simd::cross(normal, across);
                normal = simd::normalize(across * (texel.x * 2 - 1) + upward * (texel.y * 2 - 1) + normal * std::max(texel.z * 2 - 1, 0.05f));
            }
            if (look.isWater) {
                metal = 0;
                rough = 0.06f;
            }

            Vec3 color;
            if (look.isUnlit) {
                color = base + glow;
            } else {
                float facing = std::max(simd::dot(normal, toEye), 1e-3f);
                Vec3 f0 = mix(Vec3{0.04f, 0.04f, 0.04f}, base, metal);
                if (look.isWater) f0 = Vec3{0.02f, 0.02f, 0.02f};
                Vec3 matte = base * (1 - metal);

                // The sun: what a surface scatters, and what its facets mirror.
                float lit = std::max(simd::dot(normal, toSun), 0.0f);
                Vec3 half = simd::normalize(toSun + toEye);
                float nh = std::max(simd::dot(normal, half), 0.0f), vh = std::max(simd::dot(toEye, half), 0.0f);
                float a = rough * rough, a2 = a * a;
                float d = a2 / (kPi * std::pow(nh * nh * (a2 - 1) + 1, 2.0f));
                float k = (rough + 1) * (rough + 1) / 8.0f;
                float g = (lit / (lit * (1 - k) + k)) * (facing / (facing * (1 - k) + k));
                Vec3 fresnel = f0 + (Vec3{1, 1, 1} - f0) * std::pow(1 - vh, 5.0f);
                Vec3 shine = fresnel * (d * g / std::max(4 * lit * facing, 1e-3f));
                color = (matte / kPi + shine) * sun * lit;

                // The sky: all of it for what is scattered, the part it mirrors for the rest.
                Vec3 mirrored = normal * (2 * simd::dot(normal, toEye)) - toEye;
                color += matte * skyIn(normal, 1.0f, zenith, horizon, ground);
                color += skyIn(mirrored, rough, zenith, horizon, ground) * mirroredShare(f0, rough, facing) * 1.15f;
                color += glow;
                if (look.isWater) {
                    // Water is clear from above and a mirror from the side.
                    float edge = std::pow(1 - facing, 3.0f);
                    opacity = std::clamp(opacity + (1 - opacity) * edge, 0.0f, 1.0f);
                }
            }
            if (look.isSeenThrough || look.isWater) {
                // Something behind it, to see through to: a checkered card.
                bool isLight = (int(std::floor(float(x) * 8.0f / float(image.width))) + int(std::floor(float(y) * 8.0f / float(image.height)))) % 2 == 0;
                Vec3 card = isLight ? Vec3{0.32f, 0.32f, 0.34f} : Vec3{0.1f, 0.1f, 0.11f};
                color = color * opacity + card * (1 - opacity);
            }
            put(image, x, y, color, coverage);
        }
    }
    return image;
}

void lookAtThings(SoftView& view, const std::vector<SoftThing>& things, const Vec3* around) {
    if (things.empty()) return;
    auto middleOf = [&](int axis) {
        std::vector<float> values;
        for (const SoftThing& thing : things) values.push_back(thing.center[axis]);
        std::nth_element(values.begin(), values.begin() + long(values.size() / 2), values.end());
        return values[values.size() / 2];
    };
    Vec3 center{middleOf(0), middleOf(1), middleOf(2)};
    std::vector<float> away;
    for (const SoftThing& thing : things) away.push_back(simd::length(thing.center - center) + std::min(thing.reach, 30.0f));
    std::sort(away.begin(), away.end());
    float radius = std::max(away[size_t(float(away.size() - 1) * 0.8f)], 1.5f);
    // A small scene is shown whole. A wide world is shown where its camera is.
    if (around && radius > 60.0f) {
        std::vector<float> away;
        for (const SoftThing& thing : things) away.push_back(simd::length(thing.center - *around));
        std::sort(away.begin(), away.end());
        float radius = std::clamp(away[size_t(float(away.size() - 1) * 0.35f)], 5.0f, 45.0f);
        // What is looked at is on the ground near the camera, not the camera itself, which floats.
        Vec3 lowest = *around;
        for (const SoftThing& thing : things) {
            if (simd::length(thing.center - *around) <= radius) lowest.y = std::min(lowest.y, thing.center.y);
        }
        view.target = Vec3{around->x, (lowest.y + around->y) * 0.5f, around->z};
        view.eye = view.target + simd::normalize(Vec3{0.62f, 0.42f, 0.78f}) * (radius * 2.2f);
        view.fovY = 0.75f;
        view.fogDistance = radius * 30.0f;
        return;
    }
    view.target = center;
    view.eye = center + simd::normalize(Vec3{0.62f, 0.5f, 0.78f}) * (radius * 2.5f);
    view.fovY = 0.75f;
    view.fogDistance = radius * 14.0f;
}

SoftImage renderTriangles(const std::vector<SoftTriangle>& triangles, const SoftView& view, int width, int height) {
    SoftImage image;
    image.width = std::max(width, 4);
    image.height = std::max(height, 4);
    image.rgba.assign(size_t(image.width * image.height) * 4, 255);
    std::vector<float> depth(size_t(image.width * image.height), std::numeric_limits<float>::max());
    std::vector<int> owner(size_t(image.width * image.height), -1);      // which triangle is seen at each pixel

    Vec3 forward = simd::normalize(view.target - view.eye);
    Vec3 right = simd::normalize(simd::cross(forward, Vec3{0, 1, 0}) + Vec3{1e-6f, 0, 0});
    Vec3 up = simd::cross(right, forward);
    float focal = 1.0f / std::tan(view.fovY * 0.5f);
    float aspect = float(image.width) / float(image.height);
    Vec3 toSun = simd::normalize(view.sunDirection * -1.0f);
    Vec3 sun = view.sunColor * view.sunIntensity;

    // The sky behind everything.
    for (int y = 0; y < image.height; y++) {
        for (int x = 0; x < image.width; x++) {
            float sx = (2 * (float(x) + 0.5f) / float(image.width) - 1) * aspect / focal, sy = (1 - 2 * (float(y) + 0.5f) / float(image.height)) / focal;
            Vec3 direction = simd::normalize(forward + right * sx + up * sy);
            put(image, x, y, skyIn(direction, 0.15f, view.skyZenith, view.skyHorizon, view.skyGround), 1.0f);
        }
    }

    const float nearPlane = 0.05f;
    struct Point {
        float x, y, z;          // across and up in the eye's own space, and how far ahead
    };
    auto seen = [&](Vec3 p) {
        Vec3 d = p - view.eye;
        return Point{simd::dot(d, right), simd::dot(d, up), simd::dot(d, forward)};
    };
    // In a small scene the sun's light is looked for from each pixel: things cast shadows.
    // In a large one that is too many questions for a thumbnail, and too small to see.
    const bool castsShadows = triangles.size() <= 3000;
    auto isInShadow = [&](Vec3 from, int self) {
        for (size_t i = 0; i < triangles.size(); i++) {
            if (int(i) == self) continue;
            const SoftTriangle& t = triangles[i];
            Vec3 e1 = t.b - t.a, e2 = t.c - t.a, p = simd::cross(toSun, e2);
            float det = simd::dot(e1, p);
            if (std::abs(det) < 1e-9f) continue;
            Vec3 s = from - t.a;
            float u = simd::dot(s, p) / det;
            if (u < 0 || u > 1) continue;
            Vec3 q = simd::cross(s, e1);
            float v = simd::dot(toSun, q) / det;
            if (v < 0 || u + v > 1) continue;
            if (simd::dot(e2, q) / det > 1e-3f) return true;
        }
        return false;
    };
    struct Look {
        Vec3 normal, color;
        float glow;
    };
    std::vector<Look> looks(triangles.size());
    for (size_t index = 0; index < triangles.size(); index++) {
        const SoftTriangle& triangle = triangles[index];
        Vec3 normal = simd::cross(triangle.b - triangle.a, triangle.c - triangle.a);
        float area = simd::length(normal);
        if (area < 1e-12f) continue;
        normal = normal / area;
        Vec3 middleOf = (triangle.a + triangle.b + triangle.c) / 3.0f;
        if (simd::dot(normal, view.eye - middleOf) < 0) normal = normal * -1.0f;         // both sides are shown: a thumbnail has no use for holes
        looks[index] = Look{normal, triangle.color, triangle.glow};

        // Cut by the plane just ahead of the eye: a floor that reaches behind it must still be drawn.
        Point corners[3] = {seen(triangle.a), seen(triangle.b), seen(triangle.c)};
        Point cut[4];
        int count = 0;
        for (int i = 0; i < 3; i++) {
            const Point& from = corners[i];
            const Point& to = corners[(i + 1) % 3];
            bool fromAhead = from.z >= nearPlane, toAhead = to.z >= nearPlane;
            if (fromAhead) cut[count++] = from;
            if (fromAhead != toAhead) {
                float t = (nearPlane - from.z) / (to.z - from.z);
                cut[count++] = Point{from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t, nearPlane};
            }
        }
        for (int piece = 1; piece + 1 < count; piece++) {
            const Point* p[3] = {&cut[0], &cut[piece], &cut[piece + 1]};
            float sx[3], sy[3], inverse[3];
            for (int i = 0; i < 3; i++) {
                inverse[i] = 1.0f / p[i]->z;
                sx[i] = (p[i]->x * focal / aspect * inverse[i] * 0.5f + 0.5f) * float(image.width);
                sy[i] = (0.5f - p[i]->y * focal * inverse[i] * 0.5f) * float(image.height);
            }
            float whole = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
            if (std::abs(whole) < 1e-6f) continue;
            int minX = std::max(int(std::floor(std::min({sx[0], sx[1], sx[2]}))), 0), maxX = std::min(int(std::ceil(std::max({sx[0], sx[1], sx[2]}))), image.width - 1);
            int minY = std::max(int(std::floor(std::min({sy[0], sy[1], sy[2]}))), 0), maxY = std::min(int(std::ceil(std::max({sy[0], sy[1], sy[2]}))), image.height - 1);
            for (int y = minY; y <= maxY; y++) {
                for (int x = minX; x <= maxX; x++) {
                    float qx = float(x) + 0.5f, qy = float(y) + 0.5f;
                    float w0 = ((sx[1] - qx) * (sy[2] - qy) - (sx[2] - qx) * (sy[1] - qy)) / whole;
                    float w1 = ((sx[2] - qx) * (sy[0] - qy) - (sx[0] - qx) * (sy[2] - qy)) / whole;
                    float w2 = 1 - w0 - w1;
                    if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                    float z = 1.0f / (w0 * inverse[0] + w1 * inverse[1] + w2 * inverse[2]);
                    float& nearest = depth[size_t(y * image.width + x)];
                    if (z >= nearest) continue;
                    nearest = z;
                    owner[size_t(y * image.width + x)] = int(index);
                }
            }
        }
    }
    // What is seen at each pixel, lit.
    for (int y = 0; y < image.height; y++) {
        for (int x = 0; x < image.width; x++) {
            int index = owner[size_t(y * image.width + x)];
            if (index < 0) continue;
            const Look& look = looks[size_t(index)];
            float z = depth[size_t(y * image.width + x)];
            Vec3 color = look.color;
            if (look.glow < 1) {
                float lit = std::max(simd::dot(look.normal, toSun), 0.0f);
                if (lit > 0 && castsShadows) {
                    float sx = (2 * (float(x) + 0.5f) / float(image.width) - 1) * aspect / focal, sy = (1 - 2 * (float(y) + 0.5f) / float(image.height)) / focal;
                    Vec3 place = view.eye + (forward + right * sx + up * sy) * z;
                    if (isInShadow(place + look.normal * 1e-3f, index)) lit = 0;
                }
                // The sky's light from all around: less colored than the sky looks, since the bright
                // horizon and the ground under it give most of it.
                Vec3 around = skyIn(look.normal, 1.0f, view.skyZenith, view.skyHorizon, view.skyGround);
                float grey = (around.x + around.y + around.z) / 3.0f;
                around = mix(around, Vec3{grey, grey, grey}, 0.6f) * 1.4f;
                Vec3 shaded = look.color * (sun * (lit / kPi) + around);
                color = mix(shaded, look.color, look.glow);
            }
            if (view.fogDistance > 0) color = mix(view.skyHorizon, color, std::exp2(-z / view.fogDistance));
            put(image, x, y, color, 1.0f);
        }
    }
    return image;
}

}  // namespace molten::editor
