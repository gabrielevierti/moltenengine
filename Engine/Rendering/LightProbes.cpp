// LightProbes.cpp
#include "Rendering/LightProbes.hpp"

#include "Core/TextFile.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>

namespace molten {

namespace {

const Vec3 kAxes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

/// The six sides of one probe, read toward `normal`: each side counts by the square of how
/// much the normal leans its way (an "ambient cube"). The same sum as probeLight's on the GPU.
Vec4 facing(const Vec4* sides, Vec3 normal) {
    Vec3 n2 = normal * normal;
    return sides[normal.x >= 0 ? 0 : 1] * n2.x + sides[normal.y >= 0 ? 2 : 3] * n2.y + sides[normal.z >= 0 ? 4 : 5] * n2.z;
}

float smooth01(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

/// How much of a grid holds at a place: 1 well inside, fading to 0 over `fade` meters to its edge.
float gridWeight(const ProbeGrid& grid, Vec3 place) {
    Vec3 low = place - grid.origin, high = grid.farCorner() - place;
    float inside = std::min({low.x, low.y, low.z, high.x, high.y, high.z});
    if (inside < 0) return 0.0f;
    return grid.fade > 0 ? smooth01(inside / grid.fade) : 1.0f;
}

/// A grid's light at a place inside it, blended between the eight probes round it.
Vec4 gridLight(const ProbeGrid& grid, Vec3 place, Vec3 normal) {
    Vec3 cell = (place - grid.origin) / grid.spacing;
    int x0 = std::clamp(int(std::floor(cell.x)), 0, std::max(grid.countX - 2, 0));
    int y0 = std::clamp(int(std::floor(cell.y)), 0, std::max(grid.countY - 2, 0));
    int z0 = std::clamp(int(std::floor(cell.z)), 0, std::max(grid.countZ - 2, 0));
    Vec3 t = simd::clamp(cell - Vec3{float(x0), float(y0), float(z0)}, Vec3{0, 0, 0}, Vec3{1, 1, 1});
    Vec4 sum{0, 0, 0, 0};
    for (int corner = 0; corner < 8; corner++) {
        int dx = corner & 1, dy = (corner >> 1) & 1, dz = (corner >> 2) & 1;
        int x = std::min(x0 + dx, grid.countX - 1), y = std::min(y0 + dy, grid.countY - 1), z = std::min(z0 + dz, grid.countZ - 1);
        float w = (dx ? t.x : 1 - t.x) * (dy ? t.y : 1 - t.y) * (dz ? t.z : 1 - t.z);
        sum += facing(&grid.cells[grid.indexOf(x, y, z) * 6], normal) * w;
    }
    return sum;
}

/// Directions spread evenly over the sphere (a Fibonacci spiral), turned by `turn` so that
/// neighbouring probes do not all look the same ways.
Vec3 sphereDirection(int i, int count, float turn) {
    float golden = 2.39996323f;
    float y = 1.0f - (float(i) + 0.5f) * 2.0f / float(count);
    float r = std::sqrt(std::max(0.0f, 1.0f - y * y));
    float phi = float(i) * golden + turn;
    return Vec3{std::cos(phi) * r, y, std::sin(phi) * r};
}

}  // namespace

// ---------------------------------------------------------------- reading the light

Vec4 BakedLighting::lightAt(Vec3 place, Vec3 normal) const {
    Vec4 open{0, 0, 0, 1};
    for (const ProbeGrid& grid : grids) {
        if (grid.cells.size() != grid.probeCount() * 6) continue;
        float w = gridWeight(grid, place);
        if (w <= 0) continue;
        Vec4 inside = gridLight(grid, place, normal);
        return open + (inside - open) * w;          // the first grid that holds the place
    }
    return open;
}

ProbeGrid gridFor(const AABB& box, float spacing) {
    ProbeGrid grid;
    spacing = std::max(spacing, 0.05f);
    Vec3 size = simd::max(box.max - box.min, Vec3{0, 0, 0});
    grid.countX = std::max(2, int(std::ceil(size.x / spacing)) + 1);
    grid.countY = std::max(2, int(std::ceil(size.y / spacing)) + 1);
    grid.countZ = std::max(2, int(std::ceil(size.z / spacing)) + 1);
    // As even as the box allows: the probes are spread across it, edge to edge, and spacing
    // is what that comes to along its longest side.
    float longest = std::max({size.x / float(grid.countX - 1), size.y / float(grid.countY - 1), size.z / float(grid.countZ - 1)});
    grid.spacing = std::max(longest, 0.05f);
    Vec3 used = Vec3{float(grid.countX - 1), float(grid.countY - 1), float(grid.countZ - 1)} * grid.spacing;
    grid.origin = box.center() - used * 0.5f;
    grid.fade = std::min(grid.spacing, 1.0f);
    return grid;
}

// ---------------------------------------------------------------- the file

std::string BakedLighting::toText() const {
    std::string out = "molten-lighting 1\n";
    char line[256];
    for (const ProbeGrid& grid : grids) {
        std::snprintf(line, sizeof line, "grid %d %d %d  %.9g %.9g %.9g  %.9g %.9g\n", grid.countX, grid.countY, grid.countZ, double(grid.origin.x),
                      double(grid.origin.y), double(grid.origin.z), double(grid.spacing), double(grid.fade));
        out += line;
        // One probe a line: its six sides, each the bounced light and the sky seen.
        for (size_t probe = 0; probe < grid.probeCount() && probe * 6 + 5 < grid.cells.size(); probe++) {
            out += "p";
            for (int side = 0; side < 6; side++) {
                const Vec4& c = grid.cells[probe * 6 + size_t(side)];
                std::snprintf(line, sizeof line, " %.4g %.4g %.4g %.3g", double(c.x), double(c.y), double(c.z), double(c.w));
                out += line;
            }
            out += "\n";
        }
    }
    return out;
}

std::optional<BakedLighting> BakedLighting::fromText(const std::string& contents, std::string* error) {
    auto fail = [&](const std::string& why) -> std::optional<BakedLighting> {
        if (error) *error = why;
        return std::nullopt;
    };
    if (std::string why = text::checkHeader(text::firstLine(contents), "molten-lighting"); !why.empty()) return fail("the lighting can't be read: " + why);
    BakedLighting baked;
    size_t start = contents.find('\n');
    while (start != std::string::npos && start < contents.size()) {
        size_t end = contents.find('\n', start + 1);
        std::string line = contents.substr(start + 1, end == std::string::npos ? std::string::npos : end - start - 1);
        start = end;
        if (line.empty() || line[0] == '#') continue;
        if (line.compare(0, 5, "grid ") == 0) {
            ProbeGrid grid;
            float ox = 0, oy = 0, oz = 0;
            if (std::sscanf(line.c_str(), "grid %d %d %d %f %f %f %f %f", &grid.countX, &grid.countY, &grid.countZ, &ox, &oy, &oz, &grid.spacing,
                            &grid.fade) < 7)
                return fail("a grid line says too little: " + line);
            grid.origin = Vec3{ox, oy, oz};
            if (grid.countX < 1 || grid.countY < 1 || grid.countZ < 1 || grid.probeCount() > 4'000'000 || !(grid.spacing > 0))
                return fail("a grid of no size, or too many probes");
            grid.cells.reserve(grid.probeCount() * 6);
            baked.grids.push_back(std::move(grid));
        } else if (line.compare(0, 2, "p ") == 0 && !baked.grids.empty()) {
            ProbeGrid& grid = baked.grids.back();
            const char* p = line.c_str() + 1;
            for (int side = 0; side < 6; side++) {
                Vec4 c{0, 0, 0, 1};
                for (int k = 0; k < 4; k++) {
                    char* next = nullptr;
                    c[k] = std::strtof(p, &next);
                    if (next == p) return fail("a probe line is cut short");
                    p = next;
                }
                grid.cells.push_back(c);
            }
        }
        // Anything else is from a newer engine of the same version: skipped.
    }
    for (const ProbeGrid& grid : baked.grids) {
        if (grid.cells.size() != grid.probeCount() * 6) return fail("a grid has the wrong number of probes");
    }
    return baked;
}

// ---------------------------------------------------------------- the tree of boxes

void BakeScene::build() {
    order_.resize(triangles_.size());
    for (uint32_t i = 0; i < order_.size(); i++) order_[i] = i;
    nodes_.clear();
    nodes_.reserve(triangles_.size() * 2 + 1);
    Node root;
    root.first = 0;
    root.count = uint32_t(triangles_.size());
    nodes_.push_back(root);
    if (!triangles_.empty()) split(0, 0);
}

void BakeScene::split(uint32_t index, int depth) {
    Node& node = nodes_[index];
    AABB box, centers;
    for (uint32_t i = node.first; i < node.first + node.count; i++) {
        const BakeTriangle& t = triangles_[order_[i]];
        for (Vec3 p : {t.a, t.b, t.c}) {
            box.min = simd::min(box.min, p);
            box.max = simd::max(box.max, p);
        }
        Vec3 c = (t.a + t.b + t.c) / 3.0f;
        centers.min = simd::min(centers.min, c);
        centers.max = simd::max(centers.max, c);
    }
    node.box = box;
    if (node.count <= 4 || depth > 48) return;
    // Halved across the longest side of where the triangles' middles are.
    Vec3 extent = centers.max - centers.min;
    int axis = extent.x > extent.y ? (extent.x > extent.z ? 0 : 2) : (extent.y > extent.z ? 1 : 2);
    if (extent[axis] <= 0) return;
    float middle = centers.min[axis] + extent[axis] * 0.5f;
    uint32_t* begin = order_.data() + node.first;
    uint32_t* end = begin + node.count;
    uint32_t* cut = std::partition(begin, end, [&](uint32_t t) {
        const BakeTriangle& tri = triangles_[t];
        return (tri.a[axis] + tri.b[axis] + tri.c[axis]) / 3.0f < middle;
    });
    uint32_t leftCount = uint32_t(cut - begin);
    if (leftCount == 0 || leftCount == node.count) return;
    // The two children are made side by side, so the parent keeps only where the right one is:
    // the left is the node before it (walk() relies on that).
    uint32_t first = node.first, count = node.count;
    uint32_t left = uint32_t(nodes_.size());
    nodes_.push_back(Node{AABB{}, first, leftCount});
    uint32_t right = uint32_t(nodes_.size());
    nodes_.push_back(Node{AABB{}, first + leftCount, count - leftCount});
    nodes_[index].count = 0;
    nodes_[index].first = right;
    split(left, depth + 1);
    split(right, depth + 1);
}

namespace {

bool hitsBox(const AABB& box, Vec3 origin, Vec3 inverse, float reach) {
    Vec3 t0 = (box.min - origin) * inverse, t1 = (box.max - origin) * inverse;
    Vec3 near = simd::min(t0, t1), far = simd::max(t0, t1);
    float enter = std::max({near.x, near.y, near.z, 0.0f});
    float leave = std::min({far.x, far.y, far.z, reach});
    return enter <= leave;
}

}  // namespace

template <typename Visit>
void BakeScene::walk(Vec3 origin, Vec3 direction, float reach, Visit&& visit) const {
    if (nodes_.empty() || triangles_.empty()) return;
    Vec3 inverse{1.0f / (std::fabs(direction.x) > 1e-12f ? direction.x : 1e-12f), 1.0f / (std::fabs(direction.y) > 1e-12f ? direction.y : 1e-12f),
                 1.0f / (std::fabs(direction.z) > 1e-12f ? direction.z : 1e-12f)};
    uint32_t stack[128];
    int top = 0;
    stack[top++] = 0;
    while (top > 0) {
        const Node& node = nodes_[stack[--top]];
        if (!hitsBox(node.box, origin, inverse, reach)) continue;
        if (node.count > 0) {
            for (uint32_t i = node.first; i < node.first + node.count; i++) {
                if (!visit(order_[i], reach)) return;
            }
        } else if (top < 126) {
            stack[top++] = node.first;          // right
            stack[top++] = node.first - 1;      // left, made just before it
        }
    }
}

std::optional<BakeScene::Hit> BakeScene::intersect(Vec3 origin, Vec3 direction, float reach) const {
    std::optional<Hit> best;
    walk(origin, direction, reach, [&](uint32_t index, float& limit) {
        const BakeTriangle& t = triangles_[index];
        // Möller and Trumbore.
        Vec3 e1 = t.b - t.a, e2 = t.c - t.a;
        Vec3 p = simd::cross(direction, e2);
        float det = simd::dot(e1, p);
        if (std::fabs(det) < 1e-12f) return true;
        float inv = 1.0f / det;
        Vec3 s = origin - t.a;
        float u = simd::dot(s, p) * inv;
        if (u < 0 || u > 1) return true;
        Vec3 q = simd::cross(s, e1);
        float v = simd::dot(direction, q) * inv;
        if (v < 0 || u + v > 1) return true;
        float d = simd::dot(e2, q) * inv;
        if (d <= 1e-4f || d >= limit) return true;
        limit = d;
        Hit hit;
        hit.distance = d;
        hit.triangle = index;
        Vec3 n = simd::normalize(simd::cross(e1, e2));
        // Counter-clockwise triangles face the way their winding says; a ray that meets the
        // other side of one that is not double sided is inside something.
        hit.isBackFace = det < 0 && !t.isDoubleSided;
        hit.normal = simd::dot(n, direction) > 0 ? -n : n;
        best = hit;
        return true;
    });
    return best;
}

bool BakeScene::isBlocked(Vec3 from, Vec3 to) const {
    Vec3 way = to - from;
    float length = simd::length(way);
    if (length <= 1e-4f) return false;
    Vec3 direction = way / length;
    bool blocked = false;
    walk(from, direction, length - 1e-3f, [&](uint32_t index, float& limit) {
        const BakeTriangle& t = triangles_[index];
        Vec3 e1 = t.b - t.a, e2 = t.c - t.a;
        Vec3 p = simd::cross(direction, e2);
        float det = simd::dot(e1, p);
        if (std::fabs(det) < 1e-12f) return true;
        float inv = 1.0f / det;
        Vec3 s = from - t.a;
        float u = simd::dot(s, p) * inv;
        if (u < 0 || u > 1) return true;
        Vec3 q = simd::cross(s, e1);
        float v = simd::dot(direction, q) * inv;
        if (v < 0 || u + v > 1) return true;
        float d = simd::dot(e2, q) * inv;
        if (d <= 1e-4f || d >= limit) return true;
        blocked = true;
        return false;
    });
    return blocked;
}

// ---------------------------------------------------------------- baking

namespace {

/// The light the lights give a point of a surface facing `normal` (what arrives, times the cosine).
Vec3 directLight(const BakeScene& scene, const std::vector<BakeLight>& lights, Vec3 place, Vec3 normal) {
    Vec3 sum{0, 0, 0};
    Vec3 lifted = place + normal * 0.01f;
    for (const BakeLight& light : lights) {
        Vec3 toLight;
        float falloff = 1.0f;
        Vec3 target;
        if (light.kind == BakeLight::Sun) {
            toLight = -simd::normalize(light.direction);
            target = lifted + toLight * 5000.0f;
        } else {
            Vec3 way = light.position - place;
            float distance = simd::length(way);
            if (distance >= light.range || distance < 1e-4f) continue;
            toLight = way / distance;
            float ratio = distance / light.range;
            float window = std::clamp(1.0f - ratio * ratio * ratio * ratio, 0.0f, 1.0f);
            falloff = window * window / std::max(distance * distance, 0.0001f);        // as Lighting.metal has it
            if (light.kind == BakeLight::Spot) {
                float cosAngle = simd::dot(-toLight, simd::normalize(light.direction));
                falloff *= smooth01((cosAngle - light.cosOuter) / std::max(light.cosInner - light.cosOuter, 1e-4f));
            }
            target = light.position;
        }
        float cosine = simd::dot(normal, toLight);
        if (cosine <= 0 || falloff <= 0) continue;
        if (scene.isBlocked(lifted, target)) continue;
        sum += light.radiance * (cosine * falloff);
    }
    return sum;
}

/// Probes inside walls see the backs of things: they are given the light of their neighbours
/// that are not, so that no darkness leaks out of them into the room beside.
void fillHidden(ProbeGrid& grid, std::vector<char>& hidden) {
    for (int pass = 0; pass < 8; pass++) {
        bool any = false;
        std::vector<char> next = hidden;
        for (int z = 0; z < grid.countZ; z++)
            for (int y = 0; y < grid.countY; y++)
                for (int x = 0; x < grid.countX; x++) {
                    size_t i = grid.indexOf(x, y, z);
                    if (!hidden[i]) continue;
                    Vec4 sum[6] = {};
                    int count = 0;
                    const int around[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                    for (const auto& d : around) {
                        int nx = x + d[0], ny = y + d[1], nz = z + d[2];
                        if (nx < 0 || ny < 0 || nz < 0 || nx >= grid.countX || ny >= grid.countY || nz >= grid.countZ) continue;
                        size_t n = grid.indexOf(nx, ny, nz);
                        if (hidden[n]) continue;
                        for (int s = 0; s < 6; s++) sum[s] += grid.cells[n * 6 + size_t(s)];
                        count++;
                    }
                    if (count == 0) continue;
                    for (int s = 0; s < 6; s++) grid.cells[i * 6 + size_t(s)] = sum[s] / float(count);
                    next[i] = 0;
                    any = true;
                }
        hidden = next;
        if (!any) break;
    }
}

}  // namespace

BakedLighting bakeLighting(const BakeScene& scene, const std::vector<BakeLight>& lights, const std::vector<ProbeVolumeDesc>& volumes,
                           const BakeSettings& settings, const std::function<bool(float)>& progress) {
    BakedLighting baked;
    std::vector<float> bounceOf;
    size_t total = 0;
    for (const ProbeVolumeDesc& volume : volumes) {
        if (volume.box.isEmpty()) continue;
        ProbeGrid grid = gridFor(volume.box, volume.spacing);
        grid.cells.assign(grid.probeCount() * 6, Vec4{0, 0, 0, 1});
        total += grid.probeCount();
        baked.grids.push_back(std::move(grid));
        bounceOf.push_back(volume.bounce);
    }
    if (baked.grids.empty()) return baked;

    const int rays = std::clamp(settings.raysPerProbe, 16, 8192);
    unsigned threadCount = settings.threads > 0 ? unsigned(settings.threads) : std::max(1u, std::thread::hardware_concurrency());
    std::atomic<bool> stopped{false};
    std::atomic<size_t> done{0};

    // Two rounds. The first finds how much sky each probe sees and the light the lights put
    // straight on what the rays hit. The second looks again, and what the rays hit is now
    // also lit by the sky it sees and by what was bounced to it in the first round (as the
    // probes nearest it say): light that has come twice off the walls into a room.
    for (int round = 0; round < 2 && !stopped; round++) {
        const BakedLighting before = baked;
        for (size_t g = 0; g < baked.grids.size() && !stopped; g++) {
            ProbeGrid& grid = baked.grids[g];
            const float bounce = bounceOf[g];
            std::vector<char> hidden(grid.probeCount(), 0);
            std::atomic<size_t> next{0};
            auto work = [&]() {
                std::vector<Vec4> sum(6), weight(6);
                for (;;) {
                    size_t probe = next.fetch_add(1);
                    if (probe >= grid.probeCount() || stopped) return;
                    int x = int(probe % size_t(grid.countX));
                    int y = int((probe / size_t(grid.countX)) % size_t(grid.countY));
                    int z = int(probe / (size_t(grid.countX) * size_t(grid.countY)));
                    Vec3 place = grid.placeOf(x, y, z);
                    std::fill(sum.begin(), sum.end(), Vec4{0, 0, 0, 0});
                    std::fill(weight.begin(), weight.end(), Vec4{0, 0, 0, 0});
                    int backs = 0;
                    float turn = float(probe) * 0.618034f * 2.0f * kPi;
                    for (int r = 0; r < rays; r++) {
                        Vec3 d = sphereDirection(r, rays, turn);
                        Vec4 seen{0, 0, 0, 1};      // escaped: the sky, and nothing bounced
                        if (std::optional<BakeScene::Hit> hit = scene.intersect(place, d, 1e6f)) {
                            if (hit->isBackFace) {
                                backs++;
                                seen = Vec4{0, 0, 0, 0};
                            } else {
                                const BakeTriangle& t = scene.triangle(hit->triangle);
                                Vec3 point = place + d * hit->distance;
                                Vec3 light = directLight(scene, lights, point, hit->normal) / kPi;
                                if (round > 0) {
                                    Vec4 around = before.lightAt(point + hit->normal * 0.05f, hit->normal);
                                    light += settings.skyLight * around.w + Vec3{around.x, around.y, around.z};
                                }
                                Vec3 out = t.emissive + t.albedo * light * bounce;
                                seen = Vec4{out.x, out.y, out.z, 0};
                            }
                        }
                        // Counted on each side by the cosine toward it: a cosine-weighted
                        // average, which is what the sky's light map holds for a normal.
                        for (int s = 0; s < 6; s++) {
                            float c = simd::dot(d, kAxes[s]);
                            if (c <= 0) continue;
                            sum[size_t(s)] += seen * c;
                            weight[size_t(s)] += c;
                        }
                    }
                    for (int s = 0; s < 6; s++) {
                        float w = weight[size_t(s)].x;
                        grid.cells[probe * 6 + size_t(s)] = w > 0 ? sum[size_t(s)] / w : Vec4{0, 0, 0, 1};
                    }
                    if (backs > rays / 4) hidden[probe] = 1;
                    size_t now = done.fetch_add(1) + 1;
                    if (progress && (now % 16 == 0 || now == total * 2)) {
                        if (!progress(float(now) / float(total * 2))) stopped = true;
                    }
                }
            };
            std::vector<std::thread> threads;
            for (unsigned i = 1; i < threadCount; i++) threads.emplace_back(work);
            work();
            for (std::thread& t : threads) t.join();
            fillHidden(grid, hidden);
        }
    }
    if (stopped) return BakedLighting{};
    if (progress) progress(1.0f);
    return baked;
}

}  // namespace molten
