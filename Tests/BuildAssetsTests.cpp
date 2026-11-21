// Assets made smaller when a game is built.
#include "Test.hpp"

#include "Assets/BuildAssets.hpp"
#include "Core/TextFile.hpp"

#include <stb_image.h>
#include <stb_image_write.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace molten;

namespace {
std::vector<unsigned char> floatWav(int channels, int rate, int frames) {
    std::vector<unsigned char> out;
    auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) out.push_back((unsigned char)(v >> (8 * i))); };
    auto put16 = [&](uint16_t v) { out.push_back((unsigned char)v); out.push_back((unsigned char)(v >> 8)); };
    uint32_t bytes = uint32_t(frames * channels * 4);
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    put32(36 + bytes);
    out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put32(16);
    put16(3);
    put16(uint16_t(channels));
    put32(uint32_t(rate));
    put32(uint32_t(rate * channels * 4));
    put16(uint16_t(channels * 4));
    put16(32);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    put32(bytes);
    for (int f = 0; f < frames; f++) {
        for (int c = 0; c < channels; c++) {
            float value = 0.5f * std::sin(float(f) * 0.05f) * (c == 0 ? 1.0f : -1.0f);
            unsigned char raw[4];
            std::memcpy(raw, &value, 4);
            out.insert(out.end(), raw, raw + 4);
        }
    }
    return out;
}
int16_t sampleOf(const std::vector<unsigned char>& wav, size_t index) { return int16_t(wav[44 + index * 2] | wav[45 + index * 2] << 8); }
}  // namespace

TEST(pictures_are_made_smaller_and_sounds_lighter) {
    // A picture: half red, half see-through; brought down it keeps its colors without dark edges.
    std::vector<unsigned char> pixels(64 * 32 * 4);
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 64; x++) {
            unsigned char* p = &pixels[size_t(y * 64 + x) * 4];
            p[0] = 255, p[1] = 0, p[2] = 0, p[3] = x < 32 ? 255 : 0;
        }
    int w = 0, h = 0;
    std::vector<unsigned char> small = shrinkPixels(pixels, 64, 32, 16, w, h);
    CHECK(w == 16 && h == 8 && small.size() == 16 * 8 * 4);
    CHECK(small[0] == 255 && small[3] == 255 && small[(15) * 4 + 3] == 0);
    // Where solid meets clear, the color stays red: the clear pixels' black does not bleed in.
    for (int x = 0; x < 16; x++) CHECK(small[size_t(x) * 4 + 3] == 0 || small[size_t(x) * 4] == 255);
    shrinkPixels(pixels, 64, 32, 128, w, h);
    CHECK(w == 64 && h == 32);

    // A stereo sound in floats, made 16-bit mono at half its rate.
    std::vector<unsigned char> wav = floatWav(2, 44100, 4410);
    std::vector<unsigned char> light = lighterWav(wav, false, 0);
    CHECK(light.size() == 44 + 4410 * 2 * 2);
    CHECK(std::abs(sampleOf(light, 2 * 100) - int(std::lround(0.5 * std::sin(5.0) * 32767))) <= 2);
    std::vector<unsigned char> mono = lighterWav(wav, true, 22050);
    CHECK(mono.size() == 44 + 2205 * 2);
    CHECK(std::abs(sampleOf(mono, 100)) <= 2);              // its two sides were opposite: they cancel
    CHECK(lighterWav(std::vector<unsigned char>(10, 0), false, 0).empty());
}

TEST(a_project_assets_folder_is_copied_smaller) {
    namespace fs = std::filesystem;
    std::string from = "/tmp/molten-shrink/Assets", to = "/tmp/molten-shrink/Out";
    fs::remove_all("/tmp/molten-shrink");
    fs::create_directories(from + "/Textures");
    fs::create_directories(from + "/Audio");
    std::vector<unsigned char> big(512 * 256 * 4, 200);
    stbi_write_png((from + "/Textures/wall.png").c_str(), 512, 256, 4, big.data(), 512 * 4);
    stbi_write_png((from + "/Textures/icon.png").c_str(), 64, 64, 4, big.data(), 64 * 4);
    stbi_write_png((from + "/Textures/hud.png").c_str(), 300, 300, 4, big.data(), 300 * 4);
    text::writeFile(from + "/Textures/hud.png.meta", "molten-meta 1\nguid 1\nimport maxSize 100\n");
    std::vector<unsigned char> wav = floatWav(1, 44100, 44100);
    std::ofstream(from + "/Audio/wind.wav", std::ios::binary).write(reinterpret_cast<const char*>(wav.data()), std::streamsize(wav.size()));
    text::writeFile(from + "/Textures/wall.psd", "the painter's own file");
    text::writeFile(from + "/.DS_Store", "x");
    text::writeFile(from + "/Main.scene", "molten-scene 1\n");

    ShrinkOptions options;
    options.largestPicture = 256;
    ShrinkReport report = copyAssetsSmaller(from, to, options);
    int w, h, c;
    CHECK(stbi_info((to + "/Textures/wall.png").c_str(), &w, &h, &c) && w == 256 && h == 128);
    CHECK(stbi_info((to + "/Textures/icon.png").c_str(), &w, &h, &c) && w == 64);           // small already: as it was
    CHECK(stbi_info((to + "/Textures/hud.png").c_str(), &w, &h, &c) && w == 100);           // its own settings: smaller still
    CHECK(fs::exists(to + "/Textures/hud.png.meta") && fs::exists(to + "/Main.scene"));
    CHECK(!fs::exists(to + "/Textures/wall.psd") && !fs::exists(to + "/.DS_Store"));
    CHECK(fs::file_size(to + "/Audio/wind.wav") == 44 + 44100 * 2);
    CHECK(report.pictures == 2 && report.sounds == 1 && report.leftOut == 2 && report.bytesAfter < report.bytesBefore);
    CHECK(report.summary().find("2 pictures smaller") != std::string::npos);
    // The project's own files are as they were.
    CHECK(stbi_info((from + "/Textures/wall.png").c_str(), &w, &h, &c) && w == 512 && fs::exists(from + "/Textures/wall.psd"));
}
