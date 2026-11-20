// Ocean.hpp
// A patch of sea that repeats, made the way oceans are made for films: the surface is
// the sum of thousands of waves of every length and direction, as many of each as wind
// of a given speed raises (a "spectrum"), each travelling at the speed water waves of its
// length travel. Adding them up one by one would be hopeless; a Fourier transform adds
// them all at once, for every point of a grid.
//
// Besides rising and falling, water under a wave moves toward the crest, which is what
// makes crests sharp and troughs wide: the grid's points are moved sideways too
// ("choppiness"). Where that folds the surface over itself, waves break: foam.
#pragma once

#include "Math/Math.hpp"

#include <complex>
#include <vector>

namespace molten {

/// The Fourier transform of `size` values (a power of two), in place; `inverse` goes from waves to places.
void fourierTransform(std::complex<float>* values, int size, bool inverse, int stride = 1);

struct OceanSettings {
    int resolution = 64;                // points along each side of the patch: a power of two
    float patchSize = 80.0f;            // meters before it repeats
    float windSpeed = 9.0f;             // meters per second: 3 ripples a pond, 9 is a fresh breeze, 20 a gale
    Vec2 windDirection{1.0f, 0.3f};     // along x and z
    float choppiness = 1.0f;            // how sharp the crests are; 0 only rises and falls
    float heightScale = 1.0f;           // times what that wind really raises
    uint32_t seed = 7;                  // another sea of the same kind
};

class Ocean {
public:
    explicit Ocean(const OceanSettings& settings = {});
    const OceanSettings& settings() const { return settings_; }

    /// Works out the surface as it is at a time, in seconds.
    void update(float time);

    int resolution() const { return size_; }
    /// What a point of the grid does: x and z are how far it is moved sideways, y how far up.
    Vec3 displacement(int i, int j) const { return displacement_[index(i, j)]; }
    Vec3 normal(int i, int j) const { return normals_[index(i, j)]; }
    /// 0 clear water to 1 white.
    float foam(int i, int j) const { return foam_[index(i, j)]; }

    /// The same, anywhere, between the grid's points; the patch repeats without a seam.
    Vec3 displacementAt(float x, float z) const;
    Vec3 normalAt(float x, float z) const;
    float foamAt(float x, float z) const;
    /// How high the water stands above a place. The sideways movement of the surface is
    /// undone first, so that this is the height at (x, z) and not of the point that started there.
    float heightAt(float x, float z) const;
    /// The typical height of its waves: the average of the highest third, trough to crest.
    float significantHeight() const { return 4.0f * rms_ * settings_.heightScale; }

private:
    size_t index(int i, int j) const { return size_t((j & (size_t(size_) - 1)) * size_t(size_) + (i & (size_t(size_) - 1))); }
    template <typename T>
    T sample(const std::vector<T>& field, float x, float z) const;

    OceanSettings settings_;
    int size_ = 64;
    float rms_ = 0, scale_ = 1;
    std::vector<std::complex<float>> start_;        // each wave as it is at time zero
    std::vector<float> frequency_;                  // how fast each turns
    std::vector<std::complex<float>> height_, moveX_, moveZ_, slopeX_, slopeZ_;
    std::vector<Vec3> displacement_, normals_;
    std::vector<float> foam_;
};

}  // namespace molten
