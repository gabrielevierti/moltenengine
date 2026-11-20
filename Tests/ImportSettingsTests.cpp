// ImportSettingsTests.cpp
// How a file becomes what the game uses: settings kept in its .meta, applied when it loads.
#include "Test.hpp"

#include "Assets/AssetDatabase.hpp"
#include "Assets/ImportSettings.hpp"

#include <filesystem>
#include <fstream>

using namespace molten;

TEST(import_settings_are_kept_in_the_meta_beside_the_guid) {
    namespace fs = std::filesystem;
    fs::path folder = fs::temp_directory_path() / "molten-import-test";
    fs::remove_all(folder);
    fs::create_directories(folder);
    std::string file = (folder / "wall.png").string();
    std::ofstream(file) << "not really a picture";
    std::ofstream(file + ".meta") << "molten-meta 1\nguid abc123\n";

    CHECK(ImportSettings::load(file).isDefault());
    ImportSettings settings;
    settings.maxSize = 512;
    settings.isPixelated = true;
    settings.use = ImportSettings::Use::Data;
    settings.scale = 0.01f;
    settings.isMono = true;
    CHECK(settings.save(file));
    ImportSettings back = ImportSettings::load(file);
    CHECK(back.maxSize == 512 && back.isPixelated && back.use == ImportSettings::Use::Data && back.isMono);
    CHECK_NEAR(back.scale, 0.01f, 1e-7f);
    CHECK(back.mipmaps && !back.zIsUp);
    // The identifier is still there, and the asset database keeps the settings when it reads the folder.
    std::ifstream meta(file + ".meta");
    std::string text((std::istreambuf_iterator<char>(meta)), std::istreambuf_iterator<char>());
    CHECK(text.find("guid abc123") != std::string::npos);
    AssetDatabase database;
    database.open(folder.string());
    database.refresh();
    CHECK(ImportSettings::load(file).maxSize == 512);
    // Back to the usual: nothing is written about it.
    CHECK(ImportSettings{}.save(file));
    CHECK(ImportSettings::load(file).isDefault());
    fs::remove_all(folder);
}

TEST(import_settings_shape_a_model) {
    MeshData cube = MeshData::cube();
    ImportSettings settings;
    settings.scale = 2.0f;
    settings.apply(cube);
    CHECK_NEAR(cube.bounds().halfExtents().x, 1.0f, 1e-5f);

    // Z up: what pointed along Z now points along Y.
    MeshData tall = MeshData::cube(Vec3{1, 1, 4});
    ImportSettings up;
    up.zIsUp = true;
    up.apply(tall);
    CHECK_NEAR(tall.bounds().halfExtents().y, 2.0f, 1e-5f);
    CHECK_NEAR(tall.bounds().halfExtents().z, 0.5f, 1e-5f);

    // Centered: a model that sits off to one side comes to the middle.
    MeshData off = MeshData::cube();
    for (Vec3& p : off.positions) p += Vec3{5, 0, 0};
    ImportSettings centered;
    centered.isCentered = true;
    centered.apply(off);
    CHECK_NEAR(off.bounds().center().x, 0.0f, 1e-5f);

    // Smooth normals: a cube's corners, which had one normal per face, now share one that points out along the diagonal.
    MeshData smooth = MeshData::cube();
    ImportSettings smoothing;
    smoothing.smoothsNormals = true;
    smoothing.apply(smooth);
    Vec3 outward = simd::normalize(smooth.positions[0]);
    CHECK(simd::dot(smooth.normals[0], outward) > 0.85f);       // near it: a face cut into two triangles counts once or twice at a corner

    // Faces turned: every triangle the other way around.
    MeshData turned = MeshData::cube();
    uint32_t second = turned.indices[1];
    ImportSettings flipping;
    flipping.flipsFaces = true;
    flipping.apply(turned);
    CHECK(turned.indices[2] == second);
}

TEST(import_settings_shape_a_sound_and_a_picture) {
    AudioClip clip;
    clip.sampleRate = 44100;
    clip.channels = 2;
    for (int i = 0; i < 44100; i++) {
        float sample = 0.25f * std::sin(float(i) * 0.05f);
        clip.samples.push_back(sample);
        clip.samples.push_back(sample);
    }
    ImportSettings settings;
    settings.isMono = true;
    settings.sampleRate = 22050;
    settings.isNormalized = true;
    settings.apply(clip);
    CHECK(clip.channels == 1 && clip.sampleRate == 22050);
    CHECK_NEAR(clip.duration(), 1.0f, 0.01f);
    float loudest = 0;
    for (float sample : clip.samples) loudest = std::max(loudest, std::abs(sample));
    CHECK(loudest > 0.9f && loudest <= 1.0f);

    // A picture 8 by 4, its top row red and the rest blue: made 4 by 2 and turned upside down.
    int width = 8, height = 4;
    std::vector<uint8_t> pixels(size_t(width * height) * 4, 0);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t* p = &pixels[size_t(y * width + x) * 4];
            p[y == 0 ? 0 : 2] = 255;
            p[3] = 255;
        }
    }
    ImportSettings smaller;
    smaller.maxSize = 4;
    smaller.flipsVertically = true;
    smaller.apply(pixels, width, height);
    CHECK(width == 4 && height == 2);
    CHECK(pixels.size() == 32);
    CHECK(pixels[2] == 255 && pixels[0] == 0);                       // the top is blue now
    CHECK(pixels[size_t(1 * 4) * 4 + 0] > 100);                      // and the bottom has the red in it
}
