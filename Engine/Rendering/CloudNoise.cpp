#include "Rendering/CloudNoise.hpp"

#include <algorithm>
#include <cmath>
#include <thread>

namespace molten {

namespace {

/// A well-mixed number for a cell of a grid.
uint32_t hashCell(int x, int y, int z, uint32_t seed) {
    uint32_t h = uint32_t(x) * 0x8da6b343u ^ uint32_t(y) * 0xd8163841u ^ uint32_t(z) * 0xcb1ab31fu ^ (seed * 0x9e3779b9u + 0x7f4a7c15u);
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

int wrapped(int value, int period) {
    int m = value % period;
    return m < 0 ? m + period : m;
}

float fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);        // Perlin's: no creases where cells meet
}

/// One of twelve directions for a corner of the grid, dotted with the way to the point.
float gradientDot(uint32_t hash, float x, float y, float z) {
    switch (hash % 12u) {
        case 0: return x + y;
        case 1: return -x + y;
        case 2: return x - y;
        case 3: return -x - y;
        case 4: return x + z;
        case 5: return -x + z;
        case 6: return x - z;
        case 7: return -x - z;
        case 8: return y + z;
        case 9: return -y + z;
        case 10: return y - z;
        default: return -y - z;
    }
}

float remap(float value, float fromLow, float fromHigh, float toLow, float toHigh) {
    return toLow + (value - fromLow) / (fromHigh - fromLow) * (toHigh - toLow);
}

float saturate(float value) {
    return std::min(std::max(value, 0.0f), 1.0f);
}

/// Lumps on lumps: three sizes, each twice as fine and less strong.
float worleyLayers(float x, float y, float z, int cellsAcross, int cellsUp, uint32_t seed) {
    float a = tileableWorley(x * float(cellsAcross), y * float(cellsAcross), z * float(cellsUp), cellsAcross, cellsAcross, cellsUp, seed);
    float b = tileableWorley(x * float(cellsAcross * 2), y * float(cellsAcross * 2), z * float(cellsUp * 2), cellsAcross * 2, cellsAcross * 2, cellsUp * 2, seed + 1);
    float c = tileableWorley(x * float(cellsAcross * 4), y * float(cellsAcross * 4), z * float(cellsUp * 4), cellsAcross * 4, cellsAcross * 4, cellsUp * 4, seed + 2);
    return a * 0.625f + b * 0.25f + c * 0.125f;
}

/// Runs `row(z)` for every z below `depth`, on several threads.
template <class F> void forEachSlice(int depth, int threads, F row) {
    int count = threads > 0 ? threads : int(std::thread::hardware_concurrency());
    count = std::clamp(count, 1, 16);
    count = std::min(count, depth);
    std::vector<std::thread> workers;
    for (int t = 1; t < count; t++) {
        workers.emplace_back([=] {
            for (int z = t; z < depth; z += count) row(z);
        });
    }
    for (int z = 0; z < depth; z += count) row(z);
    for (std::thread& worker : workers) worker.join();
}

uint8_t asByte(float value) {
    return uint8_t(std::lround(saturate(value) * 255.0f));
}

/// Layers of lumps come out between about 0.2 and 0.76, mostly near the middle: spread over 0 to 1.
float spreadLumps(float value) {
    return (value - 0.20f) / 0.56f;
}

}  // namespace

std::vector<uint8_t> halvedNoise(const std::vector<uint8_t>& pixels, int& width, int& height, int& depth) {
    const int oldWidth = std::max(width, 1), oldHeight = std::max(height, 1), oldDepth = std::max(depth, 1);
    if (pixels.size() < size_t(oldWidth) * size_t(oldHeight) * size_t(oldDepth) * 4) return {};
    width = std::max(oldWidth / 2, 1);
    height = std::max(oldHeight / 2, 1);
    depth = std::max(oldDepth / 2, 1);
    // Along an edge that is halved two points become one; along an edge already at one, one stays one.
    const int stepX = oldWidth > 1 ? 2 : 1, stepY = oldHeight > 1 ? 2 : 1, stepZ = oldDepth > 1 ? 2 : 1;
    const int count = stepX * stepY * stepZ;
    std::vector<uint8_t> out(size_t(width) * size_t(height) * size_t(depth) * 4);
    for (int z = 0; z < depth; z++) {
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int sums[4] = {0, 0, 0, 0};
                for (int dz = 0; dz < stepZ; dz++) {
                    for (int dy = 0; dy < stepY; dy++) {
                        for (int dx = 0; dx < stepX; dx++) {
                            const uint8_t* from = &pixels[((size_t(z * stepZ + dz) * size_t(oldHeight) + size_t(y * stepY + dy)) * size_t(oldWidth) + size_t(x * stepX + dx)) * 4];
                            for (int c = 0; c < 4; c++) sums[c] += from[c];
                        }
                    }
                }
                uint8_t* to = &out[((size_t(z) * size_t(height) + size_t(y)) * size_t(width) + size_t(x)) * 4];
                for (int c = 0; c < 4; c++) to[c] = uint8_t((sums[c] + count / 2) / count);
            }
        }
    }
    return out;
}

float tileablePerlin(float x, float y, float z, int periodX, int periodY, int periodZ, uint32_t seed) {
    int ix = int(std::floor(x)), iy = int(std::floor(y)), iz = int(std::floor(z));
    float fx = x - float(ix), fy = y - float(iy), fz = z - float(iz);
    float u = fade(fx), v = fade(fy), w = fade(fz);
    float corners[2][2][2];
    for (int k = 0; k < 2; k++) {
        for (int j = 0; j < 2; j++) {
            for (int i = 0; i < 2; i++) {
                uint32_t hash = hashCell(wrapped(ix + i, periodX), wrapped(iy + j, periodY), wrapped(iz + k, periodZ), seed);
                corners[k][j][i] = gradientDot(hash, fx - float(i), fy - float(j), fz - float(k));
            }
        }
    }
    auto mix = [](float a, float b, float t) { return a + (b - a) * t; };
    float low = mix(mix(corners[0][0][0], corners[0][0][1], u), mix(corners[0][1][0], corners[0][1][1], u), v);
    float high = mix(mix(corners[1][0][0], corners[1][0][1], u), mix(corners[1][1][0], corners[1][1][1], u), v);
    return mix(low, high, w);
}

float tileableWorley(float x, float y, float z, int periodX, int periodY, int periodZ, uint32_t seed) {
    int ix = int(std::floor(x)), iy = int(std::floor(y)), iz = int(std::floor(z));
    float nearest = 1e9f;
    for (int k = -1; k <= 1; k++) {
        for (int j = -1; j <= 1; j++) {
            for (int i = -1; i <= 1; i++) {
                int cx = ix + i, cy = iy + j, cz = iz + k;
                uint32_t hash = hashCell(wrapped(cx, periodX), wrapped(cy, periodY), wrapped(cz, periodZ), seed);
                // The cell's point: somewhere inside it.
                float px = float(cx) + float(hash & 1023u) / 1023.0f;
                float py = float(cy) + float((hash >> 10) & 1023u) / 1023.0f;
                float pz = float(cz) + float((hash >> 20) & 1023u) / 1023.0f;
                float dx = px - x, dy = py - y, dz = pz - z;
                nearest = std::min(nearest, dx * dx + dy * dy + dz * dz);
            }
        }
    }
    return 1.0f - saturate(std::sqrt(nearest));
}

CloudNoise makeCloudNoise(int shapeWidth, int shapeDepth, int detailSize, int threads) {
    CloudNoise noise;
    noise.shapeWidth = noise.shapeHeight = std::max(shapeWidth, 8);
    noise.shapeDepth = std::max(shapeDepth, 4);
    noise.detailSize = std::max(detailSize, 4);
    noise.shape.resize(size_t(noise.shapeWidth) * size_t(noise.shapeHeight) * size_t(noise.shapeDepth) * 4);
    noise.detail.resize(size_t(noise.detailSize) * size_t(noise.detailSize) * size_t(noise.detailSize) * 4);

    // The block is as many cells high as it is across, for the same size of cell: the layer of
    // clouds is thin beside its width, and the numbers are not spent on height nobody sees.
    const int across = noise.shapeWidth, up = noise.shapeDepth;
    const int upCells = std::max(1, 4 * up / across);          // for 4 cells across
    forEachSlice(up, threads, [&noise, across, up, upCells](int z) {
        for (int y = 0; y < across; y++) {
            for (int x = 0; x < across; x++) {
                float u = (float(x) + 0.5f) / float(across), v = (float(y) + 0.5f) / float(across), w = (float(z) + 0.5f) / float(up);
                // Swells: four sizes of smooth noise, brought to 0..1.
                float swell = 0.0f, strength = 1.0f, total = 0.0f;
                int cells = 4, cellsUp = upCells;
                for (int octave = 0; octave < 4; octave++) {
                    swell += strength * tileablePerlin(u * float(cells), v * float(cells), w * float(cellsUp), cells, cells, cellsUp, 11u + uint32_t(octave));
                    total += strength;
                    strength *= 0.5f;
                    cells *= 2;
                    cellsUp *= 2;
                }
                swell = saturate(swell / total * 0.75f + 0.5f);
                // Lumps, three sizes of them.
                float lumps0 = worleyLayers(u, v, w, 4, upCells, 101u);
                float lumps1 = worleyLayers(u, v, w, 8, upCells * 2, 211u);
                float lumps2 = worleyLayers(u, v, w, 16, upCells * 4, 307u);
                // The body: the swells, with the lumps pushed into them from below.
                float body = saturate(remap(swell, lumps0 - 1.0f, 1.0f, 0.0f, 1.0f));
                // Each spread over the whole of 0 to 1, half of it above the middle: what reads
                // them can then take "the densest third" to mean a third of the sky.
                uint8_t* texel = &noise.shape[(size_t(z) * size_t(across) + size_t(y)) * size_t(across) * 4 + size_t(x) * 4];
                texel[0] = asByte((body - 0.47f) / 0.40f);
                texel[1] = asByte(spreadLumps(lumps0));
                texel[2] = asByte(spreadLumps(lumps1));
                texel[3] = asByte(spreadLumps(lumps2));
            }
        }
    });

    const int size = noise.detailSize;
    forEachSlice(size, threads, [&noise, size](int z) {
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                float u = (float(x) + 0.5f) / float(size), v = (float(y) + 0.5f) / float(size), w = (float(z) + 0.5f) / float(size);
                uint8_t* texel = &noise.detail[(size_t(z) * size_t(size) + size_t(y)) * size_t(size) * 4 + size_t(x) * 4];
                texel[0] = asByte(spreadLumps(worleyLayers(u, v, w, 2, 2, 401u)));
                texel[1] = asByte(spreadLumps(worleyLayers(u, v, w, 4, 4, 503u)));
                texel[2] = asByte(spreadLumps(worleyLayers(u, v, w, 8, 8, 601u)));
                texel[3] = 255;
            }
        }
    });
    return noise;
}

float CloudNoise::shapeAt(int x, int y, int z, int channel) const {
    if (shape.empty()) return 0.0f;
    x = wrapped(x, shapeWidth), y = wrapped(y, shapeHeight), z = wrapped(z, shapeDepth);
    return float(shape[(size_t(z) * size_t(shapeHeight) + size_t(y)) * size_t(shapeWidth) * 4 + size_t(x) * 4 + size_t(channel)]) / 255.0f;
}

float CloudNoise::detailAt(int x, int y, int z, int channel) const {
    if (detail.empty()) return 0.0f;
    x = wrapped(x, detailSize), y = wrapped(y, detailSize), z = wrapped(z, detailSize);
    return float(detail[(size_t(z) * size_t(detailSize) + size_t(y)) * size_t(detailSize) * 4 + size_t(x) * 4 + size_t(channel)]) / 255.0f;
}

}  // namespace molten
