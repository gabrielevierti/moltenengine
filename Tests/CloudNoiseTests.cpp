#include "Test.hpp"

#include "Rendering/CloudNoise.hpp"

#include <algorithm>
#include <cmath>

using namespace molten;

namespace {

/// The values of one channel of the shape block, in order of size.
std::vector<float> sortedChannel(const CloudNoise& noise, int channel) {
    std::vector<float> values;
    values.reserve(size_t(noise.shapeWidth) * noise.shapeHeight * noise.shapeDepth);
    for (int z = 0; z < noise.shapeDepth; z++) {
        for (int y = 0; y < noise.shapeHeight; y++) {
            for (int x = 0; x < noise.shapeWidth; x++) values.push_back(noise.shapeAt(x, y, z, channel));
        }
    }
    std::sort(values.begin(), values.end());
    return values;
}

}  // namespace

TEST(cloud_noise_has_the_sizes_asked_for) {
    CloudNoise noise = makeCloudNoise(32, 8, 8, 2);
    CHECK(!noise.isEmpty());
    CHECK(noise.shapeWidth == 32 && noise.shapeHeight == 32 && noise.shapeDepth == 8);
    CHECK(noise.detailSize == 8);
    CHECK(noise.shape.size() == size_t(32 * 32 * 8 * 4));
    CHECK(noise.detail.size() == size_t(8 * 8 * 8 * 4));
}

TEST(cloud_noise_is_the_same_every_time_and_on_any_number_of_threads) {
    CloudNoise once = makeCloudNoise(32, 8, 8, 1);
    CloudNoise again = makeCloudNoise(32, 8, 8, 3);
    CHECK(once.shape == again.shape);
    CHECK(once.detail == again.detail);
}

TEST(cloud_noise_uses_the_whole_range_around_the_middle) {
    // A sky's coverage is a threshold on these numbers: bunched together they give all
    // cloud or none, and a threshold off the middle gives a slider that does nothing for
    // half its travel.
    CloudNoise noise = makeCloudNoise(64, 16, 16, 2);
    for (int channel = 0; channel < 4; channel++) {
        std::vector<float> values = sortedChannel(noise, channel);
        float median = values[values.size() / 2];
        float low = values[values.size() / 10], high = values[values.size() * 9 / 10];
        CHECK(median > 0.3f && median < 0.7f);
        CHECK(high - low > 0.25f);                              // not bunched
        CHECK(values.front() < 0.2f && values.back() > 0.8f);  // the ends are reached
    }
}

TEST(cloud_noise_repeats_without_a_seam) {
    // Reading past an edge gives the other side's values (the GPU's sampler does the same),
    // and across the edge the values change no faster than they do anywhere else.
    CloudNoise noise = makeCloudNoise(64, 16, 16, 2);
    CHECK_NEAR(noise.shapeAt(64, 5, 3, 0), noise.shapeAt(0, 5, 3, 0), 1e-6);
    CHECK_NEAR(noise.shapeAt(-1, 5, 3, 0), noise.shapeAt(63, 5, 3, 0), 1e-6);
    CHECK_NEAR(noise.shapeAt(7, 64, 16, 1), noise.shapeAt(7, 0, 0, 1), 1e-6);
    CHECK_NEAR(noise.detailAt(16, -1, 17, 2), noise.detailAt(0, 15, 1, 2), 1e-6);

    double inside = 0, acrossX = 0, acrossY = 0, acrossZ = 0;
    int insideCount = 0;
    for (int z = 0; z < noise.shapeDepth; z++) {
        for (int y = 0; y < noise.shapeHeight; y++) {
            for (int x = 8; x < 40; x++) {
                inside += std::abs(noise.shapeAt(x + 1, y, z, 0) - noise.shapeAt(x, y, z, 0));
                insideCount++;
            }
            acrossX += std::abs(noise.shapeAt(0, y, z, 0) - noise.shapeAt(63, y, z, 0));
        }
    }
    for (int z = 0; z < noise.shapeDepth; z++) {
        for (int x = 0; x < noise.shapeWidth; x++) acrossY += std::abs(noise.shapeAt(x, 0, z, 0) - noise.shapeAt(x, 63, z, 0));
    }
    for (int y = 0; y < noise.shapeHeight; y++) {
        for (int x = 0; x < noise.shapeWidth; x++) acrossZ += std::abs(noise.shapeAt(x, y, 0, 0) - noise.shapeAt(x, y, 15, 0));
    }
    inside /= insideCount;
    acrossX /= double(noise.shapeHeight) * noise.shapeDepth;
    acrossY /= double(noise.shapeWidth) * noise.shapeDepth;
    acrossZ /= double(noise.shapeWidth) * noise.shapeHeight;
    CHECK(acrossX < inside * 1.6);
    CHECK(acrossY < inside * 1.6);
    // Up, the block is a quarter as tall as it is wide with as many cells: its values change faster there.
    CHECK(acrossZ < inside * 6.0);
}

TEST(tileable_noise_repeats_with_its_period) {
    for (int i = 0; i < 20; i++) {
        float x = 0.37f + float(i) * 0.61f, y = 1.9f - float(i) * 0.23f, z = 0.11f + float(i) * 0.37f;
        CHECK_NEAR(tileablePerlin(x, y, z, 4, 4, 2), tileablePerlin(x + 4, y - 4, z + 2, 4, 4, 2), 1e-4);
        CHECK_NEAR(tileableWorley(x, y, z, 4, 4, 2), tileableWorley(x - 4, y + 4, z - 2, 4, 4, 2), 1e-4);
        float lumps = tileableWorley(x, y, z, 4, 4, 2);
        CHECK(lumps >= 0.0f && lumps <= 1.0f);
        CHECK(std::abs(tileablePerlin(x, y, z, 4, 4, 2)) <= 1.5f);
    }
    // Smooth: a small step moves the value a little.
    CHECK_NEAR(tileablePerlin(1.300f, 2.2f, 0.7f, 8, 8, 8), tileablePerlin(1.301f, 2.2f, 0.7f, 8, 8, 8), 0.01);
    // Another seed, other noise.
    double difference = 0;
    for (int i = 0; i < 50; i++) {
        float x = float(i) * 0.173f;
        difference += std::abs(tileablePerlin(x, 0.4f, 0.9f, 8, 8, 8, 1) - tileablePerlin(x, 0.4f, 0.9f, 8, 8, 8, 2));
    }
    CHECK(difference > 1.0);
}

TEST(a_block_of_noise_is_halved_into_the_mean_of_its_points) {
    // 4 x 2 x 2, one value a channel that says where it is.
    int width = 4, height = 2, depth = 2;
    std::vector<uint8_t> pixels(size_t(width * height * depth * 4));
    for (int z = 0; z < depth; z++) {
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                uint8_t* p = &pixels[size_t(((z * height + y) * width + x) * 4)];
                p[0] = uint8_t(x * 40), p[1] = uint8_t(y * 100), p[2] = uint8_t(z * 200), p[3] = 255;
            }
        }
    }
    std::vector<uint8_t> half = halvedNoise(pixels, width, height, depth);
    CHECK(width == 2 && height == 1 && depth == 1);
    CHECK(half.size() == size_t(2 * 4));
    CHECK(half[0] == 20 && half[4] == 100);         // x: the mean of 0 and 40, of 80 and 120
    CHECK(half[1] == 50 && half[2] == 100 && half[3] == 255);

    // Down to one point: the mean of all, and an edge of one stays one.
    std::vector<uint8_t> one = halvedNoise(half, width, height, depth);
    CHECK(width == 1 && height == 1 && depth == 1);
    CHECK(one.size() == 4 && one[0] == 60);
    std::vector<uint8_t> still = halvedNoise(one, width, height, depth);
    CHECK(width == 1 && still == one);
    // Fewer points than the sizes say: nothing, instead of reading past the end.
    int w = 8, h = 8, d = 8;
    CHECK(halvedNoise(one, w, h, d).empty());

    // The whole chain of the real block keeps its middle: no level darker or lighter than the first.
    CloudNoise noise = makeCloudNoise(32, 8, 8, 2);
    int sw = noise.shapeWidth, sh = noise.shapeHeight, sd = noise.shapeDepth;
    auto mean = [](const std::vector<uint8_t>& block) {
        double sum = 0;
        for (size_t i = 0; i < block.size(); i += 4) sum += block[i];
        return sum / double(block.size() / 4);
    };
    double first = mean(noise.shape);
    std::vector<uint8_t> level = noise.shape;
    int levels = 1;
    while (sw > 1 || sh > 1 || sd > 1) {
        level = halvedNoise(level, sw, sh, sd);
        CHECK_NEAR(mean(level), first, 2.0);
        levels++;
    }
    CHECK(levels == 6);                              // 32, 16, 8, 4, 2, 1
}
