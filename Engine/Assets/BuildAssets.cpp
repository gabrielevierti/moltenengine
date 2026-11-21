#include "Assets/BuildAssets.hpp"

#include "Assets/ImportSettings.hpp"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>

namespace fs = std::filesystem;

namespace molten {

namespace {

std::string lowerExtension(const fs::path& path) {
    std::string extension = path.extension().string();
    for (char& c : extension) c = char(std::tolower(static_cast<unsigned char>(c)));
    return extension;
}

std::vector<unsigned char> readBytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

bool writeBytes(const fs::path& path, const std::vector<unsigned char>& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return bool(file);
}

uint32_t u32(const unsigned char* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t u16(const unsigned char* p) { return uint16_t(p[0] | p[1] << 8); }
void put32(std::vector<unsigned char>& out, uint32_t v) { for (int i = 0; i < 4; i++) out.push_back((unsigned char)(v >> (8 * i))); }
void put16(std::vector<unsigned char>& out, uint16_t v) { out.push_back((unsigned char)v); out.push_back((unsigned char)(v >> 8)); }

std::string megabytes(size_t bytes) {
    char text[32];
    if (bytes >= 1024 * 1024) std::snprintf(text, sizeof(text), "%.1f MB", double(bytes) / (1024.0 * 1024.0));
    else std::snprintf(text, sizeof(text), "%.0f KB", double(bytes) / 1024.0);
    return text;
}

}  // namespace

std::string ShrinkReport::summary() const {
    std::string text = "Assets: " + megabytes(bytesBefore) + " down to " + megabytes(bytesAfter);
    std::vector<std::string> parts;
    if (pictures) parts.push_back(std::to_string(pictures) + (pictures == 1 ? " picture" : " pictures") + " smaller");
    if (sounds) parts.push_back(std::to_string(sounds) + (sounds == 1 ? " sound" : " sounds") + " lighter");
    if (leftOut) parts.push_back(std::to_string(leftOut) + (leftOut == 1 ? " file" : " files") + " left out");
    if (!parts.empty()) {
        text += " (";
        for (size_t i = 0; i < parts.size(); i++) text += (i ? ", " : "") + parts[i];
        text += ")";
    }
    return text;
}

std::vector<unsigned char> shrinkPixels(const std::vector<unsigned char>& pixels, int width, int height, int largest, int& newWidth, int& newHeight) {
    newWidth = width;
    newHeight = height;
    if (largest <= 0 || std::max(width, height) <= largest || width <= 0 || height <= 0) return pixels;
    float scale = float(largest) / float(std::max(width, height));
    newWidth = std::max(1, int(std::lround(float(width) * scale)));
    newHeight = std::max(1, int(std::lround(float(height) * scale)));
    std::vector<unsigned char> out(size_t(newWidth) * size_t(newHeight) * 4);
    double stepX = double(width) / newWidth, stepY = double(height) / newHeight;
    for (int y = 0; y < newHeight; y++) {
        int y0 = int(y * stepY), y1 = std::max(y0 + 1, std::min(height, int(std::ceil((y + 1) * stepY))));
        for (int x = 0; x < newWidth; x++) {
            int x0 = int(x * stepX), x1 = std::max(x0 + 1, std::min(width, int(std::ceil((x + 1) * stepX))));
            // The average of the pixels it covers, weighted by how see-through they are so that the
            // colors of invisible pixels do not bleed into the edges.
            double sum[4] = {0, 0, 0, 0};
            double alphaSum = 0;
            int count = 0;
            for (int sy = y0; sy < y1; sy++) {
                for (int sx = x0; sx < x1; sx++) {
                    const unsigned char* p = &pixels[(size_t(sy) * size_t(width) + size_t(sx)) * 4];
                    double a = p[3] / 255.0;
                    for (int c = 0; c < 3; c++) sum[c] += p[c] * a;
                    sum[3] += p[3];
                    alphaSum += a;
                    count++;
                }
            }
            unsigned char* q = &out[(size_t(y) * size_t(newWidth) + size_t(x)) * 4];
            for (int c = 0; c < 3; c++) q[c] = (unsigned char)std::lround(alphaSum > 0 ? sum[c] / alphaSum : 0);
            q[3] = (unsigned char)std::lround(sum[3] / std::max(count, 1));
        }
    }
    return out;
}

std::vector<unsigned char> lighterWav(const std::vector<unsigned char>& wav, bool toMono, int rate) {
    if (wav.size() < 44 || std::memcmp(wav.data(), "RIFF", 4) != 0 || std::memcmp(wav.data() + 8, "WAVE", 4) != 0) return {};
    int format = 0, channels = 0, sampleRate = 0, bits = 0;
    const unsigned char* data = nullptr;
    size_t dataSize = 0;
    for (size_t at = 12; at + 8 <= wav.size();) {
        uint32_t size = u32(&wav[at + 4]);
        const unsigned char* body = &wav[at + 8];
        if (at + 8 + size > wav.size()) size = uint32_t(wav.size() - at - 8);
        if (std::memcmp(&wav[at], "fmt ", 4) == 0 && size >= 16) {
            format = u16(body);
            channels = u16(body + 2);
            sampleRate = int(u32(body + 4));
            bits = u16(body + 14);
            if (format == 0xFFFE && size >= 26) format = u16(body + 24);       // WAVE_FORMAT_EXTENSIBLE: its sub-format
        } else if (std::memcmp(&wav[at], "data", 4) == 0) {
            data = body;
            dataSize = size;
        }
        at += 8 + size + (size & 1);
    }
    bool isFloat = format == 3 && bits == 32;
    bool isInteger = format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
    if (!data || channels < 1 || sampleRate <= 0 || (!isFloat && !isInteger)) return {};
    size_t bytesPerSample = size_t(bits / 8);
    size_t frames = dataSize / (bytesPerSample * size_t(channels));
    // Each sample as a number from -1 to 1.
    auto sampleAt = [&](size_t frame, int channel) -> float {
        const unsigned char* p = data + (frame * size_t(channels) + size_t(channel)) * bytesPerSample;
        if (isFloat) {
            float value;
            std::memcpy(&value, p, 4);
            return value;
        }
        switch (bits) {
            case 8: return (float(p[0]) - 128.0f) / 128.0f;
            case 16: return float(int16_t(u16(p))) / 32768.0f;
            case 24: return float(int32_t(uint32_t(p[0]) << 8 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 24) >> 8) / 8388608.0f;
            default: return float(int32_t(u32(p))) / 2147483648.0f;
        }
    };
    int outChannels = toMono ? 1 : channels;
    int outRate = rate > 0 ? rate : sampleRate;
    size_t outFrames = rate > 0 ? size_t(double(frames) * double(outRate) / double(sampleRate)) : frames;
    std::vector<unsigned char> out;
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    put32(out, uint32_t(36 + outFrames * size_t(outChannels) * 2));
    out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put32(out, 16);
    put16(out, 1);
    put16(out, uint16_t(outChannels));
    put32(out, uint32_t(outRate));
    put32(out, uint32_t(outRate * outChannels * 2));
    put16(out, uint16_t(outChannels * 2));
    put16(out, 16);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    put32(out, uint32_t(outFrames * size_t(outChannels) * 2));
    out.reserve(out.size() + outFrames * size_t(outChannels) * 2);
    for (size_t frame = 0; frame < outFrames; frame++) {
        // Between two frames of the original, when the rate changes.
        double source = rate > 0 ? double(frame) * double(sampleRate) / double(outRate) : double(frame);
        size_t a = std::min(size_t(source), frames - 1), b = std::min(a + 1, frames - 1);
        float t = float(source - double(a));
        for (int c = 0; c < outChannels; c++) {
            float value = 0;
            if (toMono) {
                for (int k = 0; k < channels; k++) value += sampleAt(a, k) + (sampleAt(b, k) - sampleAt(a, k)) * t;
                value /= float(channels);
            } else {
                value = sampleAt(a, c) + (sampleAt(b, c) - sampleAt(a, c)) * t;
            }
            put16(out, uint16_t(int16_t(std::lround(std::clamp(value, -1.0f, 1.0f) * 32767.0f))));
        }
    }
    return out;
}

ShrinkReport copyAssetsSmaller(const std::string& from, const std::string& to, const ShrinkOptions& options) {
    ShrinkReport report;
    static const std::set<std::string> kSources = {".psd", ".psb", ".blend", ".blend1", ".kra", ".xcf", ".afphoto", ".afdesign", ".ai",
                                                   ".sketch", ".fig", ".aseprite", ".ase", ".max", ".ma", ".mb", ".c4d", ".spp", ".sbs", ".zpr", ".ztl"};
    std::error_code code;
    fs::create_directories(to, code);
    if (!fs::is_directory(from, code)) return report;
    for (auto it = fs::recursive_directory_iterator(from, code); it != fs::recursive_directory_iterator(); it.increment(code)) {
        if (code) break;
        const fs::path& path = it->path();
        fs::path relative = fs::relative(path, from, code);
        fs::path target = fs::path(to) / relative;
        if (it->is_directory(code)) {
            fs::create_directories(target, code);
            continue;
        }
        if (!it->is_regular_file(code)) continue;
        size_t size = size_t(it->file_size(code));
        report.bytesBefore += size;
        std::string name = path.filename().string();
        std::string extension = lowerExtension(path);
        if (options.leavesOutSources && (kSources.count(extension) || name == ".DS_Store" || name == "Thumbs.db" || name == "desktop.ini")) {
            report.leftOut++;
            continue;
        }
        fs::create_directories(target.parent_path(), code);
        bool done = false;
        if (extension == ".png" || extension == ".jpg" || extension == ".jpeg") {
            // Its own settings may ask for smaller still.
            ImportSettings settings = ImportSettings::load(path.string());
            int largest = options.largestPicture;
            if (settings.maxSize > 0) largest = largest > 0 ? std::min(largest, settings.maxSize) : settings.maxSize;
            int width = 0, height = 0, channels = 0;
            if (largest > 0 && stbi_info(path.string().c_str(), &width, &height, &channels) && std::max(width, height) > largest) {
                unsigned char* loaded = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
                if (loaded) {
                    std::vector<unsigned char> pixels(loaded, loaded + size_t(width) * size_t(height) * 4);
                    stbi_image_free(loaded);
                    int w = 0, h = 0;
                    std::vector<unsigned char> smaller = shrinkPixels(pixels, width, height, largest, w, h);
                    bool written = extension == ".png" ? stbi_write_png(target.string().c_str(), w, h, 4, smaller.data(), w * 4) != 0
                                                       : stbi_write_jpg(target.string().c_str(), w, h, 4, smaller.data(), 90) != 0;
                    if (written) {
                        report.pictures++;
                        done = true;
                    }
                } else {
                    report.notes.push_back(relative.string() + ": could not be read; copied as it is");
                }
            }
        } else if (extension == ".wav" && options.sixteenBitSounds) {
            ImportSettings settings = ImportSettings::load(path.string());
            std::vector<unsigned char> bytes = readBytes(path);
            // Worth it when it is not 16-bit already, or its settings ask for mono or another rate.
            bool isSixteen = bytes.size() >= 36 && u16(&bytes[34]) == 16 && u16(&bytes[20]) == 1;
            if (!isSixteen || settings.isMono || settings.sampleRate > 0) {
                std::vector<unsigned char> lighter = lighterWav(bytes, settings.isMono, settings.sampleRate);
                if (!lighter.empty() && lighter.size() < bytes.size() && writeBytes(target, lighter)) {
                    report.sounds++;
                    done = true;
                } else if (lighter.empty()) {
                    report.notes.push_back(relative.string() + ": a kind of WAV not made lighter; copied as it is");
                }
            }
        }
        if (!done) fs::copy_file(path, target, fs::copy_options::overwrite_existing, code);
        report.files++;
        report.bytesAfter += size_t(fs::file_size(target, code));
    }
    return report;
}

}  // namespace molten
