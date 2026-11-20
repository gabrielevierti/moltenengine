#include "Water/Ocean.hpp"

#include <algorithm>
#include <cmath>

namespace molten {

void fourierTransform(std::complex<float>* values, int size, bool inverse, int stride) {
    // Reorder so that each value is where the bits of its position, reversed, say.
    for (int i = 1, j = 0; i < size; i++) {
        int bit = size >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(values[i * stride], values[j * stride]);
    }
    // Then combine pairs, pairs of pairs, and so on, each time twice as long.
    for (int length = 2; length <= size; length <<= 1) {
        float angle = 2.0f * kPi / float(length) * (inverse ? 1.0f : -1.0f);
        std::complex<float> step(std::cos(angle), std::sin(angle));
        for (int start = 0; start < size; start += length) {
            std::complex<float> turn(1, 0);
            for (int k = 0; k < length / 2; k++) {
                std::complex<float> even = values[(start + k) * stride], odd = values[(start + k + length / 2) * stride] * turn;
                values[(start + k) * stride] = even + odd;
                values[(start + k + length / 2) * stride] = even - odd;
                turn *= step;
            }
        }
    }
}

namespace {

/// From waves to places, over the whole grid: every row, then every column.
void toPlaces(std::vector<std::complex<float>>& grid, int size) {
    for (int row = 0; row < size; row++) fourierTransform(grid.data() + row * size, size, true);
    for (int column = 0; column < size; column++) fourierTransform(grid.data() + column, size, true, size);
}

struct Dice {
    uint32_t state;
    float next() {                                              // 0 to 1, never 0
        state = state * 1664525u + 1013904223u;
        return (float(state >> 8) + 1.0f) / 16777217.0f;
    }
    /// From the bell curve: most near zero, few far from it.
    float normal() { return std::sqrt(-2.0f * std::log(next())) * std::cos(2.0f * kPi * next()); }
};

}  // namespace

Ocean::Ocean(const OceanSettings& settings) : settings_(settings) {
    size_ = 16;
    while (size_ < settings.resolution && size_ < 512) size_ *= 2;
    const int n = size_;
    const float patch = std::max(settings.patchSize, 1.0f), gravity = 9.81f;
    float wind = std::max(settings.windSpeed, 0.1f);
    Vec2 direction = simd::length(settings.windDirection) > 1e-5f ? simd::normalize(settings.windDirection) : Vec2{1, 0};
    float largest = wind * wind / gravity;                      // the longest wave this wind can raise
    float smallest = patch / float(n) * 0.5f;                   // shorter than the grid can show: left out

    start_.assign(size_t(n) * size_t(n), {0, 0});
    frequency_.assign(start_.size(), 0);
    Dice dice{settings.seed * 2654435761u + 12345u};
    double energy = 0;
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            // A wave for each point: how many crests fit in the patch along x and along z.
            float kx = 2.0f * kPi * float(i < n / 2 ? i : i - n) / patch, kz = 2.0f * kPi * float(j < n / 2 ? j : j - n) / patch;
            float k = std::sqrt(kx * kx + kz * kz);
            float a = dice.normal(), b = dice.normal();     // drawn for every wave, so that a seed always gives the same sea
            if (k < 1e-6f) continue;
            // Phillips' spectrum: how much of each wave a steady wind raises. Most along the wind, none against it.
            float along = (kx * direction.x + kz * direction.y) / k;
            float amount = std::exp(-1.0f / (k * largest * k * largest)) / (k * k * k * k) * along * along * std::exp(-k * k * smallest * smallest);
            if (along < 0) amount *= 0.07f;
            float amplitude = std::sqrt(amount * 0.5f);
            start_[size_t(j * n + i)] = std::complex<float>(a, b) * amplitude;
            frequency_[size_t(j * n + i)] = std::sqrt(gravity * k);
            energy += double(amount);
        }
    }
    // Scaled so that the waves are as high as that wind really makes them: by the sea
    // state tables, the highest third average about 0.21 V squared over g.
    float rmsNow = std::sqrt(float(energy) * 2.0f);
    float rmsWanted = 0.21f * wind * wind / gravity / 4.0f;
    scale_ = rmsNow > 1e-9f ? rmsWanted / rmsNow : 0.0f;
    rms_ = rmsWanted;
    for (std::complex<float>& wave : start_) wave *= scale_;

    height_.resize(start_.size());
    moveX_.resize(start_.size());
    moveZ_.resize(start_.size());
    slopeX_.resize(start_.size());
    slopeZ_.resize(start_.size());
    displacement_.assign(start_.size(), Vec3{0, 0, 0});
    normals_.assign(start_.size(), Vec3{0, 1, 0});
    foam_.assign(start_.size(), 0);
    update(0);
}

void Ocean::update(float time) {
    const int n = size_;
    const float patch = std::max(settings_.patchSize, 1.0f);
    const std::complex<float> i1(0, 1);
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            size_t at = size_t(j * n + i), opposite = size_t(((n - j) % n) * n + (n - i) % n);
            float kx = 2.0f * kPi * float(i < n / 2 ? i : i - n) / patch, kz = 2.0f * kPi * float(j < n / 2 ? j : j - n) / patch;
            float k = std::sqrt(kx * kx + kz * kz);
            // Each wave turns at its own speed; adding the one going the other way keeps the surface real.
            float phase = frequency_[at] * time;
            std::complex<float> turn(std::cos(phase), std::sin(phase));
            std::complex<float> h = start_[at] * turn + std::conj(start_[opposite]) * std::conj(turn);
            height_[at] = h;
            slopeX_[at] = i1 * kx * h;
            slopeZ_[at] = i1 * kz * h;
            moveX_[at] = k > 1e-6f ? -i1 * (kx / k) * h : std::complex<float>(0, 0);
            moveZ_[at] = k > 1e-6f ? -i1 * (kz / k) * h : std::complex<float>(0, 0);
        }
    }
    toPlaces(height_, n);
    toPlaces(slopeX_, n);
    toPlaces(slopeZ_, n);
    toPlaces(moveX_, n);
    toPlaces(moveZ_, n);

    const float up = settings_.heightScale, chop = settings_.choppiness * settings_.heightScale;
    for (size_t at = 0; at < displacement_.size(); at++) {
        displacement_[at] = Vec3{moveX_[at].real() * chop, height_[at].real() * up, moveZ_[at].real() * chop};
        normals_[at] = simd::normalize(Vec3{-slopeX_[at].real() * up, 1.0f, -slopeZ_[at].real() * up});
    }
    // Where the sideways movement squeezes the surface until it folds, the wave breaks.
    const float cell = patch / float(n);
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            Vec3 right = displacement(i + 1, j), left = displacement(i - 1, j), front = displacement(i, j + 1), back = displacement(i, j - 1);
            float xx = 1.0f + (right.x - left.x) / (2 * cell), zz = 1.0f + (front.z - back.z) / (2 * cell);
            float xz = (front.x - back.x) / (2 * cell), zx = (right.z - left.z) / (2 * cell);
            float area = xx * zz - xz * zx;                     // 1 undisturbed, 0 folded flat
            float fresh = std::clamp((0.55f - area) / 0.5f, 0.0f, 1.0f);
            // Foam appears at once and fades slowly.
            foam_[index(i, j)] = std::max(fresh, foam_[index(i, j)] * 0.97f);
        }
    }
}

template <typename T>
T Ocean::sample(const std::vector<T>& field, float x, float z) const {
    float cell = std::max(settings_.patchSize, 1.0f) / float(size_);
    float fx = x / cell, fz = z / cell;
    float flooredX = std::floor(fx), flooredZ = std::floor(fz);
    int i = int(flooredX), j = int(flooredZ);
    float tx = fx - flooredX, tz = fz - flooredZ;
    T a = field[index(i, j)] * (1 - tx) + field[index(i + 1, j)] * tx;
    T b = field[index(i, j + 1)] * (1 - tx) + field[index(i + 1, j + 1)] * tx;
    return a * (1 - tz) + b * tz;
}

Vec3 Ocean::displacementAt(float x, float z) const { return sample(displacement_, x, z); }
Vec3 Ocean::normalAt(float x, float z) const { return simd::normalize(sample(normals_, x, z)); }
float Ocean::foamAt(float x, float z) const { return sample(foam_, x, z); }

float Ocean::heightAt(float x, float z) const {
    // The point that is above (x, z) now started somewhere else: step back to it.
    float fromX = x, fromZ = z;
    for (int pass = 0; pass < 3; pass++) {
        Vec3 moved = displacementAt(fromX, fromZ);
        fromX = x - moved.x;
        fromZ = z - moved.z;
    }
    return displacementAt(fromX, fromZ).y;
}

}  // namespace molten
