#include "Test.hpp"

#include "Rendering/Quality.hpp"
#include "Scene/SceneFile.hpp"

using namespace molten;

using Level = GameQuality::Level;
using Group = GameQuality::Group;

TEST(a_new_project_has_four_levels_each_asking_more_than_the_one_before) {
    QualityLevels all = QualityLevels::standard();
    CHECK(all.names() == std::vector<std::string>({"Low", "Medium", "High", "Ultra"}));
    CHECK(all.start == "High" && all.starting().name == "High");
    for (const GameQuality& level : all.levels) {
        for (int group = 0; group < GameQuality::kGroupCount; group++) CHECK(!level.summaryOf(Group(group)).empty());
    }
    const GameQuality &low = *all.find("Low"), &medium = *all.find("Medium"), &high = *all.find("High"), &ultra = *all.find("Ultra");
    CHECK(low.renderScale < medium.renderScale && medium.renderScale < high.renderScale && high.renderScale <= ultra.renderScale);
    CHECK(low.shadowReach < medium.shadowReach && medium.shadowReach < high.shadowReach && high.shadowReach < ultra.shadowReach);
    CHECK(!low.rayTraced && medium.rayTraced && high.rayTraced && ultra.rayTraced);
    CHECK(medium.shadowRays < high.shadowRays && high.shadowRays < ultra.shadowRays);
    CHECK(low.cloudSteps < medium.cloudSteps && medium.cloudSteps < high.cloudSteps && high.cloudSteps < ultra.cloudSteps);
    CHECK(low.lightShafts < medium.lightShafts && medium.lightShafts < high.lightShafts && high.lightShafts < ultra.lightShafts);
    CHECK(low.detailDistance < medium.detailDistance && medium.detailDistance < high.detailDistance && high.detailDistance < ultra.detailDistance);
    // Low is still a good picture: smooth edges, clouds with depth at a size that keeps them soft,
    // the screen's ambient occlusion, light shafts, MetalFX for the smaller picture.
    CHECK(low.antiAliasing == 2 && low.volumetricClouds && low.cloudResolution >= 0.4f && low.ambientOcclusion && low.lightShafts > 0 && low.metalFX);
    // Every level keeps the frame rate by drawing fewer pixels while the GPU cannot.
    for (const GameQuality& level : all.levels) CHECK(level.dynamicResolution && level.targetFrameRate == 60);
    CHECK(GameQuality::preset(Level::High).toLines() == GameQuality{}.toLines());
}

TEST(levels_are_the_games_own_to_name_add_and_take_away) {
    QualityLevels all = QualityLevels::standard();
    // A level of its own, made from High, with values of its own.
    GameQuality handheld = *all.find("High");
    handheld.name = all.freeName("High");
    CHECK(handheld.name == "High 2");
    handheld.name = "Handheld";
    handheld.renderScale = 0.7f;
    handheld.targetFrameRate = 40;
    handheld.tracedReflections = false;
    handheld.cloudResolution = 0.65f;
    all.levels.insert(all.levels.begin() + 1, handheld);
    all.levels.erase(all.levels.begin() + all.indexOf("Ultra"));
    all.start = "Handheld";
    std::string text = all.toText();
    CHECK(text.rfind("molten-quality 3\n", 0) == 0);
    std::optional<QualityLevels> read = QualityLevels::parse(text);
    CHECK(read.has_value());
    CHECK(read && read->names() == std::vector<std::string>({"Low", "Handheld", "Medium", "High"}));
    CHECK(read && read->start == "Handheld" && read->starting().renderScale == 0.7f && read->starting().targetFrameRate == 40);
    CHECK(read && !read->find("Handheld")->tracedReflections && read->find("Handheld")->cloudResolution == 0.65f);
    CHECK(read && read->toText() == text);
    // Names with spaces and quotes come back whole.
    all.levels[0].name = "Very \"Low\" Power";
    std::optional<QualityLevels> quoted = QualityLevels::parse(all.toText());
    CHECK(quoted && quoted->levels[0].name == "Very \"Low\" Power");
    // A start that names no level: the first.
    std::optional<QualityLevels> lost = QualityLevels::parse("molten-quality 3\nstart \"Gone\"\nlevel \"A\"\n  renderScale 0.5\nlevel \"B\"\n");
    CHECK(lost && lost->start == "A" && lost->starting().renderScale == 0.5f);
    // A level the file does not finish (a newer engine's value it does not know is skipped).
    std::optional<QualityLevels> newer = QualityLevels::parse("molten-quality 3\nlevel \"Low\"\n  cloudSteps 99\n  hologramDetail 3\n");
    CHECK(newer && newer->levels.size() == 1 && newer->levels[0].cloudSteps == 99 && newer->levels[0].antiAliasing == 2);
    // Not such a file, or a newer one.
    CHECK(!QualityLevels::parse("level High\nrenderScale 1\n"));
    CHECK(!QualityLevels::parse("molten-quality 4\nlevel \"A\"\n"));
}

TEST(quality_files_of_before_become_the_four_levels) {
    // Version 2: one level of values. It becomes that level of the four, which the game starts at.
    std::optional<QualityLevels> two = QualityLevels::parse("molten-quality 2\nlevel Medium\nantiAliasing 4\nrayTracing Ultra\ncloudSteps 40\n");
    CHECK(two && two->names().size() == 4 && two->start == "Medium");
    const GameQuality* medium = two ? two->find("Medium") : nullptr;
    CHECK(medium && medium->antiAliasing == 4 && medium->cloudSteps == 40 && medium->shadowRays == 12 && medium->occlusionRays == 3);
    CHECK(two && two->find("Low")->toLines() == GameQuality::preset(Level::Low).toLines());
    // "Custom" was no level: the values become High's.
    std::optional<QualityLevels> custom = QualityLevels::parse("molten-quality 2\nlevel Custom\nvolumetricClouds 0\nshadowDistance 90\n");
    CHECK(custom && custom->start == "High" && !custom->find("High")->volumetricClouds && std::abs(custom->find("High")->shadowReach - 0.6f) < 1e-4f);
    // Version 1: a level, as that level is now, whatever values it carried.
    std::optional<QualityLevels> one = QualityLevels::parse("molten-quality 1\nlevel Low\nshadowDistance 60\n");
    CHECK(one && one->start == "Low" && one->find("Low")->toLines() == GameQuality::preset(Level::Low).toLines());
}

TEST(the_quality_limits_what_a_scene_asks_and_decides_its_ray_tracing) {
    // A scene that asks for everything.
    RenderSettings scene;
    scene.cascades.shadowDistance = 110;
    scene.sky.cloudsHaveDepth = true;
    scene.fogShaftSteps = 24;
    scene.motionBlur = true;
    applyLightingQuality(scene, LightingQuality::Ultra);

    RenderSettings atLow = scene;
    GameQuality::preset(Level::Low).applyTo(atLow);
    CHECK(!atLow.rayTracedShadows && !atLow.rayTracedOcclusion && !atLow.rayTracedReflections && !atLow.rayTracedBounce);
    CHECK(std::abs(atLow.cascades.shadowDistance - 66.0f) < 1e-3f && atLow.cascades.resolution == 2048 && atLow.cascades.count == 2);
    CHECK(atLow.sky.cloudsHaveDepth && atLow.sky.cloudSteps == 24 && atLow.fogShaftSteps == 8 && atLow.ambientOcclusionEnabled);
    CHECK(atLow.sampleCount == 2 && atLow.renderScale == 0.85f && atLow.dynamicResolution && !atLow.motionBlur);

    RenderSettings atHigh = scene;
    GameQuality::preset(Level::High).applyTo(atHigh);
    CHECK(lightingQualityOf(atHigh) == LightingQuality::High && atHigh.motionBlur);
    CHECK(atHigh.cascades.shadowDistance == 110);           // as far as the scene asks
    CHECK(atHigh.sky.cloudSteps == 48 && atHigh.sky.cloudResolution == 0.5f && atHigh.fogShaftSteps == 16);

    RenderSettings atUltra = scene;
    GameQuality::preset(Level::Ultra).applyTo(atUltra);
    CHECK(lightingQualityOf(atUltra) == LightingQuality::Ultra);
    CHECK(atUltra.sampleCount == 4 && atUltra.cascades.resolution == 4096 && atUltra.fogShaftSteps == 24);
    CHECK(atUltra.sky.cloudSteps == 64 && atUltra.shadowRayCount == 12 && atUltra.occlusionRayCount == 3);

    // A scene that asks for little gets no more than it asks, at any level.
    RenderSettings plain;
    plain.sky.cloudsHaveDepth = false;
    plain.fogShaftSteps = 0;
    plain.bloomEnabled = false;
    plain.cascades.shadowDistance = 25;
    GameQuality::preset(Level::Ultra).applyTo(plain);
    CHECK(!plain.sky.cloudsHaveDepth && plain.fogShaftSteps == 0 && !plain.bloomEnabled && plain.cascades.shadowDistance == 37.5f);

    // Each part of ray tracing on its own: reflections off, the rest traced.
    GameQuality mine = GameQuality::preset(Level::High);
    mine.tracedReflections = false;
    mine.occlusionRays = 5;
    RenderSettings drawn;
    mine.applyTo(drawn);
    CHECK(drawn.rayTracedShadows && drawn.rayTracedOcclusion && drawn.rayTracedBounce && !drawn.rayTracedReflections && drawn.occlusionRayCount == 5);
    mine.rayTraced = false;                 // off: none of it, whatever the parts say
    RenderSettings off;
    mine.applyTo(off);
    CHECK(!off.rayTracedShadows && !off.rayTracedOcclusion && !off.rayTracedReflections && !off.rayTracedBounce);
    // Lights in the air and the baked probes are the level's to allow.
    mine.lightsInAir = false;
    mine.lightProbes = false;
    mine.applyTo(off);
    CHECK(!off.volumetricLights && !off.usesLightProbes);
    // The clouds are never walked into a picture smaller than 35% of the screen.
    mine.cloudResolution = 0.1f;
    mine.applyTo(off);
    CHECK(off.sky.cloudResolution == 0.35f);

    // A scene's own smaller picture (a game's menu sets it) and the level's multiply; its own dynamic resolution is kept.
    RenderSettings small;
    small.renderScale = 0.7f;
    small.dynamicResolution = true;
    small.targetFrameRate = 30;
    small.lowestDynamicScale = 0.8f;
    GameQuality::preset(Level::Medium).applyTo(small);
    CHECK_NEAR(small.renderScale, 0.63, 1e-5);
    CHECK(small.dynamicResolution && small.targetFrameRate == 30 && small.lowestDynamicScale == 0.8f);
    // A group copied from another level, and compared with it.
    GameQuality copied = GameQuality::preset(Level::High);
    copied.copyGroup(Group::Shadows, GameQuality::preset(Level::Low));
    CHECK(copied.sameGroup(Group::Shadows, GameQuality::preset(Level::Low)) && copied.shadowCascades == 2);
    CHECK(copied.sameGroup(Group::Sky, GameQuality::preset(Level::High)) && !copied.sameGroup(Group::Sky, GameQuality::preset(Level::Low)));
}

TEST(lighting_levels_saved_when_they_sent_more_rays_come_back_as_the_same_level) {
    // What "Ultra" was: four rays toward the sun and four around, from every pixel.
    RenderSettings old;
    old.rayTracedShadows = true, old.shadowRayCount = 4;
    old.rayTracedOcclusion = true, old.occlusionRayCount = 4;
    old.rayTracedReflections = true, old.reflectionMaxRoughness = 0.5f;
    old.rayTracedBounce = true;
    CHECK(lightingQualityOf(old) == LightingQuality::Custom);
    CHECK(bringLightingQualityUpToDate(old));
    CHECK(lightingQualityOf(old) == LightingQuality::Ultra);
    CHECK(!bringLightingQualityUpToDate(old));              // already as it is now

    RenderSettings high;
    high.rayTracedShadows = true, high.shadowRayCount = 2;
    high.rayTracedOcclusion = true, high.occlusionRayCount = 2;
    high.rayTracedReflections = true, high.reflectionMaxRoughness = 0.25f;
    CHECK(bringLightingQualityUpToDate(high) && lightingQualityOf(high) == LightingQuality::High);

    RenderSettings medium;
    medium.rayTracedShadows = true, medium.shadowRayCount = 1;
    CHECK(bringLightingQualityUpToDate(medium) && lightingQualityOf(medium) == LightingQuality::Medium);

    // Values somebody set by hand are left alone; so is a scene without ray tracing.
    RenderSettings byHand;
    byHand.rayTracedShadows = true, byHand.shadowRayCount = 9;
    CHECK(!bringLightingQualityUpToDate(byHand) && byHand.shadowRayCount == 9);
    RenderSettings none;
    none.shadowRayCount = 4, none.occlusionRayCount = 4;
    CHECK(!bringLightingQualityUpToDate(none) && !none.rayTracedShadows);

    // And it happens as a scene is read.
    RenderSettings saved;
    saved.rayTracedShadows = true, saved.shadowRayCount = 4;
    saved.rayTracedOcclusion = true, saved.occlusionRayCount = 4;
    saved.rayTracedReflections = true, saved.reflectionMaxRoughness = 0.5f;
    saved.rayTracedBounce = true;
    Scene empty, read;
    std::vector<NamedMaterial> none2;
    RenderSettings back;
    CHECK(readScene(writeScene(empty, none2, saved), read, none2, back, SceneAssets{}));
    CHECK(lightingQualityOf(back) == LightingQuality::Ultra && back.occlusionRayCount == 3 && back.shadowRayCount == 12);
}

TEST(dynamic_resolution_goes_to_the_size_that_fits_and_does_not_swing) {
    // 60 frames a second: 16.7 ms a frame. At full size the GPU takes 30: far behind.
    float scale = nextDynamicScale(1.0f, 30.0f, 60.0f, 0.5f);
    CHECK(scale < 0.75f && scale >= 0.65f);                 // at once to about sqrt(14.2 / 30) = 0.69, not a twentieth at a time
    // There the frame takes what its pixels are of the first one's: inside the band, and the scale stays.
    float took = 30.0f * scale * scale;
    CHECK(took < 16.7f * 0.92f);
    CHECK(nextDynamicScale(scale, took, 60.0f, 0.5f) == scale);
    // Plenty of room: up, a step at a time.
    CHECK_NEAR(nextDynamicScale(0.7f, 6.0f, 60.0f, 0.5f), 0.75, 1e-5);
    CHECK(nextDynamicScale(1.0f, 3.0f, 60.0f, 0.5f) == 1.0f);
    // Hopelessly behind: to the floor and no lower; and always at least a step down.
    CHECK(nextDynamicScale(1.0f, 200.0f, 60.0f, 0.5f) == 0.5f);
    CHECK(nextDynamicScale(0.5f, 200.0f, 60.0f, 0.5f) == 0.5f);
    CHECK_NEAR(nextDynamicScale(1.0f, 15.6f, 60.0f, 0.5f), 0.95, 1e-5);
    // No time measured yet: as it is.
    CHECK(nextDynamicScale(0.8f, 0.0f, 60.0f, 0.5f) == 0.8f);
    // From any start, with a GPU whose time goes with the pixels, it settles and stays settled.
    for (float full : {12.0f, 20.0f, 33.0f, 50.0f, 80.0f}) {
        float at = 1.0f;
        int changes = 0;
        for (int step = 0; step < 40; step++) {
            float next = nextDynamicScale(at, full * at * at, 60.0f, 0.5f);
            if (next != at && step >= 12) changes++;
            at = next;
        }
        CHECK(changes == 0);
        CHECK(at >= 0.5f && at <= 1.0f);
        // And where it settled the frame fits, unless it is at the floor.
        CHECK(at == 0.5f || full * at * at <= 16.7f * 0.93f);
    }
    // The level decides for a scene that does not ask; one that asks keeps what it says.
    bool isOn = false;
    float target = 0, lowest = 0;
    GameQuality::preset(Level::High).dynamicResolutionFor(isOn, target, lowest);
    CHECK(isOn && target == 60 && lowest == 0.6f);
    isOn = false;
    GameQuality::preset(Level::Ultra).dynamicResolutionFor(isOn, target, lowest);
    CHECK(isOn && target == 60 && lowest == 0.7f);
    isOn = true, target = 30, lowest = 0.9f;
    GameQuality::preset(Level::Low).dynamicResolutionFor(isOn, target, lowest);
    CHECK(isOn && target == 30 && lowest == 0.9f);
}

#include "Rendering/Environment.hpp"

TEST(a_sky_that_barely_changed_need_not_be_captured_again_at_once) {
    gpu::FrameUniforms then{};
    then.sunDirection = {0.3f, -0.8f, 0.5f};
    then.skySunDirection = then.sunDirection;
    then.sunColor = {4.0f, 3.6f, 3.0f};
    then.skyZenithColor = {0.07f, 0.16f, 0.42f};
    then.skyHorizonColor = {0.86f, 0.56f, 0.34f};
    then.skyGroundColor = {0.13f, 0.12f, 0.12f};
    then.skyIntensity = 1;
    then.cloudCoverage = 0.35f;
    then.cloudScale = 1;
    gpu::FrameUniforms now = then;
    CHECK(skyBarelyChanged(now, then));
    // The sun a frame of a slow day farther on.
    now.sunDirection = {0.305f, -0.8f, 0.497f};
    now.skySunDirection = now.sunDirection;
    now.sunColor = {4.0f, 3.58f, 2.97f};
    CHECK(skyBarelyChanged(now, then));
    // The sun somewhere else: at once.
    now.sunDirection = {0.6f, -0.6f, 0.5f};
    CHECK(!skyBarelyChanged(now, then));
    now = then;
    now.skyZenithColor = {0.3f, 0.1f, 0.1f};                 // the sky painted another color
    CHECK(!skyBarelyChanged(now, then));
    now = then;
    now.cloudCoverage = 0.6f;                               // clouds rolled in
    CHECK(!skyBarelyChanged(now, then));
    now = then;
    now.volumetricClouds = 48;                              // clouds given depth
    CHECK(!skyBarelyChanged(now, then));
    now = then;
    now.skyIntensity = 2;
    CHECK(!skyBarelyChanged(now, then));
}

TEST(the_quality_says_how_many_point_and_spot_lights_cast_shadows) {
    GameQuality low = GameQuality::preset(GameQuality::Level::Low), ultra = GameQuality::preset(GameQuality::Level::Ultra);
    CHECK(low.shadowedLights < ultra.shadowedLights && low.lightShadowResolution < ultra.lightShadowResolution);
    GameQuality mine = GameQuality::preset(GameQuality::Level::High);
    mine.shadowedLights = 3;
    mine.lightShadowResolution = 1024;
    QualityLevels all;
    all.levels.push_back(mine);
    std::optional<QualityLevels> read = QualityLevels::parse(all.toText());
    const GameQuality* back = read ? read->find("High") : nullptr;
    CHECK(back && back->shadowedLights == 3 && back->lightShadowResolution == 1024);
    RenderSettings settings;
    back->applyTo(settings);
    CHECK(settings.shadowedLights == 3 && settings.lightShadowResolution == 1024);
}
