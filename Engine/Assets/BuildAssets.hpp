// BuildAssets.hpp
// A project's Assets as a game takes them along when it is built: the same files, smaller.
// Pictures larger than the device needs (or than their import settings say) are made smaller
// once, here, instead of every time the game loads them; sounds kept as floating point or 24
// and 32 bits become 16-bit, and mono or another rate when their settings ask for it; the files
// the game never reads (Photoshop, Blender, Krita... files kept beside the art, .DS_Store) are
// left out. The project's own files are never changed.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace molten {

struct ShrinkOptions {
    int largestPicture = 0;             // pixels on the longer side; 0: as their settings say. 2048 suits a phone
    bool sixteenBitSounds = true;
    bool leavesOutSources = true;       // .psd, .blend, .kra, .xcf...
};

struct ShrinkReport {
    size_t bytesBefore = 0, bytesAfter = 0;
    int files = 0, pictures = 0, sounds = 0, leftOut = 0;     // copied; made smaller; made lighter; not taken
    std::vector<std::string> notes;     // what could not be read, and was copied as it is
    std::string summary() const;        // "Assets: 182 MB down to 61 MB (40 pictures smaller, 12 sounds lighter, 3 files left out)"
};

/// Copies `from` (a project's Assets folder) to `to` as described above. `to` is made if needed.
ShrinkReport copyAssetsSmaller(const std::string& from, const std::string& to, const ShrinkOptions& options = {});

/// The pixels of an RGBA picture brought down to fit `largest` on its longer side, each the
/// average of those it covers. Unchanged if it already fits.
std::vector<unsigned char> shrinkPixels(const std::vector<unsigned char>& pixels, int width, int height, int largest, int& newWidth, int& newHeight);

/// A WAV file's bytes made 16-bit (and mono, and resampled to `rate` when not 0). Empty if it is
/// not a WAV this understands (PCM 8, 16, 24 or 32 bits, or 32-bit float).
std::vector<unsigned char> lighterWav(const std::vector<unsigned char>& wav, bool toMono, int rate);

}  // namespace molten
