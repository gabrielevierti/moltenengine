// CloudNoise.hpp
// The two blocks of noise clouds are carved out of.
//
// A cloud is not a shape with an edge: it is more or less water in the air, everywhere.
// Walking a line of sight through the sky and asking, a hundred times over, how much cloud
// there is at each step is what makes them look like volumes; but the asking must be cheap,
// and the answer must have the look of clouds, which are billows on billows. So the answer
// is worked out once, here, into two small blocks of numbers that repeat in every direction
// without a seam, and the GPU only reads them:
//
//   shape    128 x 128 x 32, four values a point. The first is the body of the clouds:
//            smooth swells (Perlin's noise) with round lumps pushed into them (Worley's
//            noise: how far the nearest of a scatter of points is, which makes cells like
//            the heads of a cauliflower). The other three are those lumps alone, each twice
//            as fine as the one before: they round off the body's edges.
//   detail   32 x 32 x 32, three values a point: lumps finer still, which eat wisps out of
//            the edges that the eye is near enough to see.
//
// It takes a fraction of a second on several threads, and nothing but arithmetic: the
// renderer does it away from the main thread when the engine starts, and tests can look at
// what it makes.
#pragma once

#include <cstdint>
#include <vector>

namespace molten {

struct CloudNoise {
    int shapeWidth = 0, shapeHeight = 0, shapeDepth = 0;       // across, across, up
    int detailSize = 0;                                         // the same along each edge
    std::vector<uint8_t> shape;                                 // four bytes a point, x fastest, then y (the other "across"), then z (up)
    std::vector<uint8_t> detail;                                // four bytes a point; the fourth is unused

    bool isEmpty() const { return shape.empty() || detail.empty(); }
    /// A value of the shape block, 0 to 1, with its coordinates wrapped: channel 0 the body, 1 to 3 the lumps.
    float shapeAt(int x, int y, int z, int channel) const;
    float detailAt(int x, int y, int z, int channel) const;
};

/// Makes both blocks. `threads` 0 uses what the machine has. The same numbers every time.
CloudNoise makeCloudNoise(int shapeWidth = 128, int shapeDepth = 32, int detailSize = 32, int threads = 0);

/// A block of four-byte points at half the size along each edge (an edge of one stays one):
/// each value the mean of those it replaces. The far clouds are read from such smaller
/// copies, which is what keeps them from sparkling. The sizes are changed to the new ones.
std::vector<uint8_t> halvedNoise(const std::vector<uint8_t>& pixels, int& width, int& height, int& depth);

/// Smooth noise that repeats every `period` cells along each axis: -1 to 1 or so.
float tileablePerlin(float x, float y, float z, int periodX, int periodY, int periodZ, uint32_t seed = 0);
/// Round lumps that repeat the same way: 1 at each of a scatter of points (one a cell), 0 a
/// cell's width away from the nearest.
float tileableWorley(float x, float y, float z, int periodX, int periodY, int periodZ, uint32_t seed = 0);

}  // namespace molten
