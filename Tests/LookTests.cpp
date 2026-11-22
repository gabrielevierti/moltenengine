// The look of a scene: grading, exposure, lens effects, the sky, decals, swaying.
#include "Test.hpp"

#include "Game/Component.hpp"
#include "Kit/Kit.hpp"
#include "Rendering/GPUTypes.hpp"
#include "Scene/SceneFile.hpp"

#include <stb_image.h>

#include <cmath>
#include <fstream>

using namespace molten;

TEST(the_look_of_a_scene_is_saved_with_it) {
    Scene scene;
    std::vector<NamedMaterial> materials;
    RenderSettings settings;
    settings.sky.moon = 0.4f;
    settings.sky.stars = 1.5f;
    settings.colorGrading = "Grading/Warm.png";
    settings.colorGradingAmount = 0.7f;
    settings.autoExposure = true;
    settings.exposureCompensation = -0.5f;
    settings.depthOfField = true;
    settings.focusDistance = 3;
    settings.focusRange = 1.5f;
    settings.focusBlur = 12;
    settings.motionBlur = true;
    settings.motionBlurAmount = 0.3f;
    auto grass = std::make_shared<Material>();
    grass->sway = 0.8f;
    materials.push_back(NamedMaterial{"Grass", grass});
    std::string text = writeScene(scene, materials, settings);
    Scene again;
    std::vector<NamedMaterial> read;
    RenderSettings back;
    CHECK(readScene(text, again, read, back, SceneAssets{}));
    CHECK(std::abs(back.sky.moon - 0.4f) < 1e-5f && std::abs(back.sky.stars - 1.5f) < 1e-5f);
    CHECK(back.colorGrading == "Grading/Warm.png" && std::abs(back.colorGradingAmount - 0.7f) < 1e-5f);
    CHECK(back.autoExposure && std::abs(back.exposureCompensation + 0.5f) < 1e-5f);
    CHECK(back.depthOfField && back.focusDistance == 3 && back.focusRange == 1.5f && back.focusBlur == 12);
    CHECK(back.motionBlur && std::abs(back.motionBlurAmount - 0.3f) < 1e-5f);
    CHECK(read.size() == 1 && std::abs(read[0].material->sway - 0.8f) < 1e-5f);
    // Off, they leave no lines.
    std::string plain = writeScene(scene, {}, RenderSettings{});
    CHECK(plain.find("depthOfField") == std::string::npos && plain.find("night") == std::string::npos);
    // What the GPU is given for them.
    CHECK(sizeof(gpu::EffectUniforms) == 176 && sizeof(gpu::DecalUniforms) == 160 && sizeof(gpu::PostUniforms) == 144);
}

TEST(sunlight_goes_gold_and_red_as_the_sun_sinks) {
    Vec3 noon = sunlightThroughAir(1.0f), afternoon = sunlightThroughAir(0.5f);
    CHECK(std::abs(noon.x - 1) < 1e-4f && std::abs(noon.z - 1) < 1e-4f);
    CHECK(afternoon.z > 0.7f && afternoon.x > afternoon.z);                  // a little warm
    Vec3 low = sunlightThroughAir(0.08f);                                   // about five degrees up: orange
    CHECK(low.x > 0.4f && low.y < low.x * 0.7f && low.z < low.y * 0.5f);
    Vec3 setting = sunlightThroughAir(0.0f);                                // on the horizon: dim red
    CHECK(setting.x < low.x && setting.y < setting.x * 0.2f && setting.z < 0.01f);
    CHECK(simd::length(sunlightThroughAir(-0.1f)) == 0);                    // set: none
    // Saved only when turned off.
    RenderSettings settings;
    settings.sky.sunsetColors = false;
    Scene scene;
    std::vector<NamedMaterial> read;
    RenderSettings back;
    CHECK(readScene(writeScene(scene, {}, settings), scene, read, back, SceneAssets{}) && !back.sky.sunsetColors);
}

TEST(a_decal_is_a_component_and_the_neutral_table_changes_nothing) {
    kit::registerKit();
    const ComponentType* type = ComponentRegistry::instance().find("Decal");
    CHECK(type && type->category == "Rendering" && type->findField("picture") && type->findField("darkens"));
    // The neutral table: red across each square, green down it, blue from square to square.
    int w = 0, h = 0, c = 0;
    unsigned char* pixels = stbi_load("Assets/Grading/Neutral 32.png", &w, &h, &c, 4);
    CHECK(pixels && w == 1024 && h == 32);
    if (!pixels) return;
    auto at = [&](int x, int y) { return pixels + (size_t(y) * size_t(w) + size_t(x)) * 4; };
    CHECK(at(0, 0)[0] == 0 && at(0, 0)[1] == 0 && at(0, 0)[2] == 0);
    CHECK(at(31, 0)[0] == 255 && at(0, 31)[1] == 255 && at(32 * 31, 0)[2] == 255);
    CHECK(at(32 * 16 + 8, 20)[0] == uint8_t(std::lround(8 * 255.0 / 31)) && at(32 * 16 + 8, 20)[1] == uint8_t(std::lround(20 * 255.0 / 31)) &&
          at(32 * 16 + 8, 20)[2] == uint8_t(std::lround(16 * 255.0 / 31)));
    stbi_image_free(pixels);
}
