// ImportSettings.cpp
#include "Assets/ImportSettings.hpp"

#include "Audio/AudioEdit.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <sstream>

namespace molten {

bool ImportSettings::isDefault() const { return toText().empty(); }

std::string ImportSettings::toText() const {
    ImportSettings usual;
    std::ostringstream out;
    if (use != usual.use) out << "import use " << (use == Use::Color ? "color" : "data") << "\n";
    if (mipmaps != usual.mipmaps) out << "import mipmaps " << (mipmaps ? 1 : 0) << "\n";
    if (maxSize != usual.maxSize) out << "import maxSize " << maxSize << "\n";
    if (isPixelated) out << "import filter pixels\n";
    if (flipsVertically) out << "import flipVertically 1\n";
    if (scale != usual.scale) out << "import scale " << scale << "\n";
    if (zIsUp) out << "import up z\n";
    if (flipsFaces) out << "import flipFaces 1\n";
    if (smoothsNormals) out << "import smoothNormals 1\n";
    if (isCentered) out << "import center 1\n";
    if (isMono) out << "import mono 1\n";
    if (sampleRate != usual.sampleRate) out << "import sampleRate " << sampleRate << "\n";
    if (isNormalized) out << "import normalize 1\n";
    if (volume != usual.volume) out << "import volume " << volume << "\n";
    return out.str();
}

ImportSettings ImportSettings::fromText(const std::string& metaText) {
    ImportSettings settings;
    std::istringstream lines(metaText);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream words(line);
        std::string first, key, value;
        if (!(words >> first >> key >> value) || first != "import") continue;
        float number = std::strtof(value.c_str(), nullptr);
        bool isOn = value != "0";
        if (key == "use") settings.use = value == "data" ? Use::Data : value == "color" ? Use::Color : Use::AsAsked;
        else if (key == "mipmaps") settings.mipmaps = isOn;
        else if (key == "maxSize") settings.maxSize = std::max(int(number), 0);
        else if (key == "filter") settings.isPixelated = value == "pixels";
        else if (key == "flipVertically") settings.flipsVertically = isOn;
        else if (key == "scale") settings.scale = number > 0 ? number : 1.0f;
        else if (key == "up") settings.zIsUp = value == "z";
        else if (key == "flipFaces") settings.flipsFaces = isOn;
        else if (key == "smoothNormals") settings.smoothsNormals = isOn;
        else if (key == "center") settings.isCentered = isOn;
        else if (key == "mono") settings.isMono = isOn;
        else if (key == "sampleRate") settings.sampleRate = std::max(int(number), 0);
        else if (key == "normalize") settings.isNormalized = isOn;
        else if (key == "volume") settings.volume = std::max(number, 0.0f);
    }
    return settings;
}

ImportSettings ImportSettings::load(const std::string& assetFile) {
    std::ifstream file(assetFile + ".meta", std::ios::binary);
    if (!file) return {};
    std::stringstream text;
    text << file.rdbuf();
    return fromText(text.str());
}

bool ImportSettings::save(const std::string& assetFile) const {
    std::string kept;
    {
        std::ifstream file(assetFile + ".meta", std::ios::binary);
        std::string line;
        while (file && std::getline(file, line)) {
            if (line.rfind("import ", 0) != 0 && !line.empty()) kept += line + "\n";
        }
    }
    if (kept.empty()) kept = "molten-meta 1\n";
    std::ofstream out(assetFile + ".meta", std::ios::binary);
    out << kept << toText();
    return bool(out);
}

void ImportSettings::apply(MeshData& mesh) const {
    if (zIsUp) {
        // Z up, Y into the screen becomes Y up, Z toward the viewer: a quarter turn about X.
        auto turn = [](Vec3 v) { return Vec3{v.x, v.z, -v.y}; };
        for (Vec3& p : mesh.positions) p = turn(p);
        for (Vec3& n : mesh.normals) n = turn(n);
        for (Vec4& t : mesh.tangents) {
            Vec3 turned = turn(Vec3{t.x, t.y, t.z});
            t = Vec4{turned.x, turned.y, turned.z, t.w};
        }
    }
    if (scale != 1.0f) {
        for (Vec3& p : mesh.positions) p = p * scale;
    }
    if (isCentered && !mesh.positions.empty()) {
        Vec3 middle = mesh.bounds().center();
        for (Vec3& p : mesh.positions) p = p - middle;
    }
    if (flipsFaces) {
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) std::swap(mesh.indices[i + 1], mesh.indices[i + 2]);
        for (Vec3& n : mesh.normals) n = n * -1.0f;
    }
    if (smoothsNormals && mesh.normals.size() == mesh.positions.size()) {
        // Each corner's normal is the sum of the faces that meet at that place, larger faces
        // counting for more. Corners at the same place are found by their rounded position.
        std::vector<size_t> order(mesh.positions.size());
        for (size_t i = 0; i < order.size(); i++) order[i] = i;
        auto key = [&](size_t i) {
            const Vec3& p = mesh.positions[i];
            return std::array<long, 3>{std::lround(p.x * 10000.0f), std::lround(p.y * 10000.0f), std::lround(p.z * 10000.0f)};
        };
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return key(a) < key(b); });
        std::vector<size_t> place(mesh.positions.size());       // which group of corners each is in
        size_t groups = 0;
        for (size_t i = 0; i < order.size(); i++) {
            if (i > 0 && key(order[i]) != key(order[i - 1])) groups++;
            place[order[i]] = groups;
        }
        std::vector<Vec3> sums(groups + 1, Vec3{0, 0, 0});
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            uint32_t a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
            Vec3 face = simd::cross(mesh.positions[b] - mesh.positions[a], mesh.positions[c] - mesh.positions[a]);
            for (uint32_t corner : {a, b, c}) sums[place[corner]] += face;
        }
        for (size_t i = 0; i < mesh.normals.size(); i++) {
            Vec3 sum = sums[place[i]];
            if (simd::length(sum) > 1e-12f) mesh.normals[i] = simd::normalize(sum);
        }
    }
}

void ImportSettings::apply(AudioClip& clip) const {
    if (isMono) audio::toMono(clip);
    if (sampleRate > 0 && sampleRate != clip.sampleRate && clip.sampleRate > 0) {
        // Read through at another pace, and said to be at the new rate: the same sound, with fewer (or more) samples.
        audio::changeSpeed(clip, float(clip.sampleRate) / float(sampleRate));
        clip.sampleRate = sampleRate;
    }
    float whole = clip.duration() + 1.0f;
    if (isNormalized) audio::normalize(clip, 0, whole);
    if (volume != 1.0f) audio::gain(clip, 0, whole, volume);
}

void ImportSettings::apply(std::vector<uint8_t>& pixels, int& width, int& height) const {
    if (width <= 0 || height <= 0 || pixels.size() < size_t(width) * size_t(height) * 4) return;
    if (flipsVertically) {
        size_t row = size_t(width) * 4;
        for (int y = 0; y < height / 2; y++) {
            std::swap_ranges(pixels.begin() + long(size_t(y) * row), pixels.begin() + long(size_t(y + 1) * row), pixels.begin() + long(size_t(height - 1 - y) * row));
        }
    }
    // Halved, each new pixel the average of four, until its longer side fits.
    while (maxSize > 0 && std::max(width, height) > maxSize && width > 1 && height > 1) {
        int halfWidth = width / 2, halfHeight = height / 2;
        std::vector<uint8_t> smaller(size_t(halfWidth) * size_t(halfHeight) * 4);
        for (int y = 0; y < halfHeight; y++) {
            for (int x = 0; x < halfWidth; x++) {
                for (int c = 0; c < 4; c++) {
                    auto at = [&](int px, int py) { return int(pixels[(size_t(py) * size_t(width) + size_t(px)) * 4 + size_t(c)]); };
                    smaller[(size_t(y) * size_t(halfWidth) + size_t(x)) * 4 + size_t(c)] =
                        uint8_t((at(x * 2, y * 2) + at(x * 2 + 1, y * 2) + at(x * 2, y * 2 + 1) + at(x * 2 + 1, y * 2 + 1) + 2) / 4);
                }
            }
        }
        pixels = std::move(smaller);
        width = halfWidth;
        height = halfHeight;
    }
}

}  // namespace molten
