// ImportSettings.hpp
// How a file becomes what the game uses: a picture read as colors or as data, made
// smaller, drawn smooth or in sharp pixels; a model scaled, stood up, its faces turned;
// a sound made mono, slower to load or lighter, louder.
//
// The settings of a file live in its `.meta`, beside its identifier, as lines that begin
// with `import`. Only what differs from the usual is written:
//
//     molten-meta 1
//     guid 3f2a9c0e5b7d41a8b6c2d9e0f1a2b3c4
//     import maxSize 512
//     import filter pixels
//
// The file itself is never changed: the settings are applied each time it is loaded.
#pragma once

#include "Assets/MeshData.hpp"
#include "Audio/AudioClip.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace molten {

struct ImportSettings {
    // ---- pictures
    enum class Use { AsAsked, Color, Data };
    Use use = Use::AsAsked;             // Color: sRGB, what a painter made. Data: numbers (a normal map, a mask), not to be converted
    bool mipmaps = true;                // smaller copies for when it is far away: off for interface art and pixel art
    int maxSize = 0;                    // its longer side is brought down to this many pixels; 0 leaves it
    bool isPixelated = false;           // drawn in sharp pixels instead of smoothed
    bool flipsVertically = false;

    // ---- models
    float scale = 1.0f;                 // 0.01 for a model made in centimeters
    bool zIsUp = false;                 // made in a program where Z points up: laid back so that Y does
    bool flipsFaces = false;            // inside out: turns every triangle around
    bool smoothsNormals = false;        // works the normals out again, shared by the corners that meet
    bool isCentered = false;            // moved so that the middle of its box is its origin

    // ---- sounds
    bool isMono = false;                // two channels made one: halves the memory, right for sounds placed in the world
    int sampleRate = 0;                 // resampled to this many samples a second; 0 leaves it
    bool isNormalized = false;          // as loud as it can be without distorting
    float volume = 1.0f;

    bool isDefault() const;

    /// The `import` lines, one setting each, for what differs from the usual.
    std::string toText() const;
    /// Reads `import` lines out of a .meta file's text; other lines are ignored.
    static ImportSettings fromText(const std::string& metaText);

    /// The settings of an asset: read from `<assetFile>.meta`. The usual ones if there is none.
    static ImportSettings load(const std::string& assetFile);
    /// Writes them into `<assetFile>.meta`, keeping its other lines (the guid).
    bool save(const std::string& assetFile) const;

    void apply(MeshData& mesh) const;
    void apply(AudioClip& clip) const;
    /// Pixels four bytes each, row by row from the top: flipped and made smaller as asked.
    void apply(std::vector<uint8_t>& pixels, int& width, int& height) const;
};

}  // namespace molten
