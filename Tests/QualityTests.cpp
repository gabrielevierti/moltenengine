#include "Test.hpp"

#include "Rendering/Quality.hpp"
#include "Scene/SceneFile.hpp"

using namespace molten;

using Level = GameQuality::Level;
using Group = GameQuality::Group;

TEST(a_quality_level_is_every_group_at_that_level) {
    for (Level level : {Level::Low, Level::Medium, Level::High, Level::Ultra}) {
        GameQuality quality = GameQuality::preset(level);
        CHECK(quality.level == level);
        for (int group = 0; group < GameQuality::kGroupCount; group++) {
            CHECK(quality.isAt(Group(group), level));
            CHECK(quality.levelOf(Group(group)) == level);
            CHECK(!quality.summaryOf(Group(group)).empty());
        }
        quality.findLevel();
        CHECK(quality.level == level);                  // found again, and of two levels that fit a group, the one it was
    }
    CHECK(GameQuality{}.level == Level::High);
    CHECK(GameQuality::preset(Level::High).toText() == GameQuality{}.toText());
}

TEST(a_group_can_be_at_a_level_of_its_own) {
    GameQuality quality = GameQuality::preset(Level::High);
    quality.setGroup(Group::Shadows, Level::Low);
    CHECK(quality.level == Level::Custom);
    CHECK(quality.levelOf(Group::Shadows) == Level::Low);
    CHECK(quality.shadowResolution == 2048 && quality.shadowCascades == 2 && quality.shadowDistance == 60);
    // The rest is where it was.
    CHECK(quality.levelOf(Group::Resolution) == Level::High && quality.renderScale == 1.0f);
    CHECK(quality.levelOf(Group::RayTracing) == Level::High && quality.rayTracing == LightingQuality::High);
    // Back, and the whole is at High again.
    quality.setGroup(Group::Shadows, Level::High);
    CHECK(quality.level == Level::High);

    // A value of no level: the group is at none, and so is the whole.
    quality.cloudSteps = 57;
    quality.findLevel();
    CHECK(quality.levelOf(Group::Sky) == Level::Custom && quality.level == Level::Custom);
    for (Level level : {Level::Low, Level::Medium, Level::High, Level::Ultra}) CHECK(!quality.isAt(Group::Sky, level));
    // Where two levels have the same values a group is at both: edges as smooth at Medium as at High.
    CHECK(quality.isAt(Group::AntiAliasing, Level::Medium) && quality.isAt(Group::AntiAliasing, Level::High));
    CHECK(!quality.isAt(Group::AntiAliasing, Level::Low) && !quality.isAt(Group::AntiAliasing, Level::Ultra));
    // Every level asks more of the machine than the one under it.
    GameQuality low = GameQuality::preset(Level::Low), medium = GameQuality::preset(Level::Medium), high = GameQuality::preset(Level::High),
                ultra = GameQuality::preset(Level::Ultra);
    CHECK(low.renderScale < medium.renderScale && medium.renderScale < high.renderScale && high.renderScale <= ultra.renderScale);
    CHECK(low.shadowDistance < medium.shadowDistance && medium.shadowDistance < high.shadowDistance && high.shadowDistance < ultra.shadowDistance);
    CHECK(int(low.rayTracing) < int(medium.rayTracing) && int(medium.rayTracing) < int(high.rayTracing) && int(high.rayTracing) < int(ultra.rayTracing));
    CHECK(!low.rayTraced && medium.rayTraced && high.rayTraced && ultra.rayTraced);
    CHECK(low.cloudSteps < medium.cloudSteps && medium.cloudSteps < high.cloudSteps && high.cloudSteps < ultra.cloudSteps);
    CHECK(low.cloudResolution < medium.cloudResolution && medium.cloudResolution < high.cloudResolution && high.cloudResolution <= ultra.cloudResolution);
    CHECK(low.lightShafts < medium.lightShafts && medium.lightShafts < high.lightShafts && high.lightShafts < ultra.lightShafts);
    // Low is still a good picture: clouds with depth, the screen's ambient occlusion, light shafts.
    CHECK(low.volumetricClouds && low.ambientOcclusion && low.lightShafts > 0 && low.metalFX);
    // Every level keeps the frame rate by drawing fewer pixels while the GPU cannot: Ultra too, at 60.
    CHECK(low.dynamicResolution && ultra.dynamicResolution && ultra.targetFrameRate == 60 && ultra.renderScale == 1.0f);
    CHECK(low.detailDistance < medium.detailDistance && medium.detailDistance < high.detailDistance && high.detailDistance < ultra.detailDistance);
}

TEST(the_quality_goes_through_its_file) {
    GameQuality quality = GameQuality::preset(Level::Medium);
    quality.setGroup(Group::RayTracing, Level::Ultra);
    quality.cloudResolution = 0.65f;
    quality.targetFrameRate = 45;
    quality.findLevel();
    std::optional<GameQuality> read = GameQuality::parse(quality.toText());
    CHECK(read.has_value());
    CHECK(read->toText() == quality.toText());
    CHECK(read->level == Level::Custom && read->rayTracing == LightingQuality::Ultra && read->cloudResolution == 0.65f && read->targetFrameRate == 45);
    CHECK(read->levelOf(Group::Shadows) == Level::Medium);
    // Not such a file.
    CHECK(!GameQuality::parse("level High\nrenderScale 1\n"));

    // A file of before the groups, at a level: that level as it is now, whatever values it carried
    // (they were that level's own of then: 60 meters of shadows, which now would hold a scene back).
    std::optional<GameQuality> old = GameQuality::parse("molten-quality 1\nlevel High\nantiAliasing 2\nrenderScale 1\nshadowDistance 60\nshadowResolution 2048\n");
    CHECK(old && old->level == Level::High && old->shadowDistance == GameQuality::preset(Level::High).shadowDistance);
    CHECK(old->rayTracing == LightingQuality::High && old->dynamicResolution);
    // And one that had values of its own keeps them, with what it does not say taken from High.
    std::optional<GameQuality> custom = GameQuality::parse("molten-quality 1\nlevel Custom\nantiAliasing 4\nshadowDistance 90\nvolumetricClouds 0\n");
    CHECK(custom && custom->level == Level::Custom && custom->antiAliasing == 4 && custom->shadowDistance == 90 && !custom->volumetricClouds);
    CHECK(custom->shadowResolution == 2048 && custom->rayTracing == LightingQuality::High);
    // A new file that names a level and one value: the level's, with that value.
    std::optional<GameQuality> partial = GameQuality::parse("molten-quality 2\nlevel Low\ncloudSteps 99\n");
    CHECK(partial && partial->antiAliasing == 1 && partial->cloudSteps == 99 && partial->shadowDistance == 60);
}

TEST(the_quality_limits_what_a_scene_asks_and_decides_its_ray_tracing) {
    // A scene that asks for everything.
    RenderSettings scene;
    scene.cascades.shadowDistance = 110;
    scene.sky.cloudsHaveDepth = true;
    scene.fogShaftSteps = 24;
    applyLightingQuality(scene, LightingQuality::Ultra);

    RenderSettings atLow = scene;
    GameQuality::preset(Level::Low).applyTo(atLow);
    CHECK(!atLow.rayTracedShadows && !atLow.rayTracedOcclusion && !atLow.rayTracedReflections && !atLow.rayTracedBounce);
    CHECK(atLow.cascades.shadowDistance == 60 && atLow.cascades.resolution == 2048 && atLow.cascades.count == 2);
    CHECK(atLow.sky.cloudsHaveDepth && atLow.sky.cloudSteps == 24 && atLow.fogShaftSteps == 8 && atLow.ambientOcclusionEnabled);
    CHECK(atLow.sampleCount == 1 && atLow.renderScale == 0.8f && atLow.dynamicResolution);

    RenderSettings atMedium = scene;
    GameQuality::preset(Level::Medium).applyTo(atMedium);
    CHECK(lightingQualityOf(atMedium) == LightingQuality::Medium);
    CHECK(atMedium.cascades.shadowDistance == 100 && atMedium.sky.cloudsHaveDepth && atMedium.sky.cloudSteps == 32 && atMedium.fogShaftSteps == 12);

    RenderSettings atHigh = scene;
    GameQuality::preset(Level::High).applyTo(atHigh);
    CHECK(lightingQualityOf(atHigh) == LightingQuality::High);
    CHECK(atHigh.cascades.shadowDistance == 110);           // as far as the scene asks: High would reach farther
    CHECK(atHigh.sky.cloudSteps == 48 && atHigh.sky.cloudResolution == 0.5f && atHigh.fogShaftSteps == 16);

    RenderSettings atUltra = scene;
    GameQuality::preset(Level::Ultra).applyTo(atUltra);
    CHECK(lightingQualityOf(atUltra) == LightingQuality::Ultra);
    CHECK(atUltra.sampleCount == 4 && atUltra.cascades.resolution == 4096 && atUltra.fogShaftSteps == 24);
    CHECK(atUltra.sky.cloudSteps == 64 && atUltra.sky.cloudResolution == 0.5f && atUltra.shadowRayCount == 12 && atUltra.occlusionRayCount == 3);

    // A scene that asks for little gets no more than it asks, at any level; but ray tracing is
    // the project's alone, whatever the scene's file says of it.
    RenderSettings plain;
    plain.sky.cloudsHaveDepth = false;
    plain.fogShaftSteps = 0;
    plain.bloomEnabled = false;
    plain.cascades.shadowDistance = 25;
    RenderSettings plainAtUltra = plain;
    GameQuality::preset(Level::Ultra).applyTo(plainAtUltra);
    CHECK(!plainAtUltra.sky.cloudsHaveDepth && plainAtUltra.fogShaftSteps == 0 && !plainAtUltra.bloomEnabled);
    CHECK(lightingQualityOf(plainAtUltra) == LightingQuality::Ultra);
    // On at a level no whole level has it at (Low is off at Low): all of it traced still, with the
    // fewest rays; kept and written so.
    GameQuality chosen = GameQuality::preset(Level::High);
    chosen.setRayTracing(LightingQuality::Low);
    chosen.findLevel();
    CHECK(chosen.levelOf(Group::RayTracing) == Level::Custom && chosen.level == Level::Custom);
    std::optional<GameQuality> read = GameQuality::parse(chosen.toText());
    CHECK(read && read->rayTraced && read->rayTracing == LightingQuality::Low && read->levelOf(Group::RayTracing) == Level::Custom);
    RenderSettings drawn;
    drawn.bounceIntensity = 0.2f;
    chosen.applyTo(drawn);
    CHECK(drawn.rayTracedShadows && drawn.rayTracedOcclusion && drawn.rayTracedBounce && drawn.rayTracedReflections && drawn.bounceIntensity == 1.0f);
    CHECK(lightingQualityOf(drawn) == LightingQuality::Low && drawn.shadowRayCount == 4 && drawn.occlusionRayCount == 1);
    // Off: nothing traced, and the ray tracing group is where Low has it.
    chosen.rayTraced = false;
    chosen.findLevel();
    CHECK(chosen.isAt(Group::RayTracing, Level::Low));
    RenderSettings plainDrawn;
    chosen.applyTo(plainDrawn);
    CHECK(!plainDrawn.rayTracedShadows && !plainDrawn.rayTracedOcclusion && !plainDrawn.rayTracedReflections && !plainDrawn.rayTracedBounce);
    // A file of before, that chose its values one by one: off if the sun's shadows were.
    std::optional<GameQuality> before = GameQuality::parse("molten-quality 2\nlevel Custom\nrayTracing Ultra\ntracedShadows 0\nshadowRays 9\n");
    CHECK(before && !before->rayTraced && before->rayTracing == LightingQuality::Ultra);
    CHECK(plainAtUltra.cascades.shadowDistance == 25);

    // A scene's own smaller picture (a game's menu sets it) and the level's multiply; its own dynamic resolution is kept.
    RenderSettings small;
    small.renderScale = 0.7f;
    small.dynamicResolution = true;
    small.targetFrameRate = 30;
    small.lowestDynamicScale = 0.8f;
    GameQuality::preset(Level::Medium).applyTo(small);
    CHECK_NEAR(small.renderScale, 0.63, 1e-5);
    CHECK(small.dynamicResolution && small.targetFrameRate == 30 && small.lowestDynamicScale == 0.8f);
    // A level limits what a scene asks to its own rays: every level traces all of it.
    RenderSettings custom = scene;
    limitLightingQuality(custom, LightingQuality::Medium);
    CHECK(custom.rayTracedShadows && custom.shadowRayCount == 6 && custom.rayTracedOcclusion && custom.occlusionRayCount == 1);
    CHECK(custom.rayTracedReflections && custom.reflectionMaxRoughness == 0.25f && custom.rayTracedBounce);
    RenderSettings untouched = scene;
    limitLightingQuality(untouched, LightingQuality::Ultra);
    CHECK(lightingQualityOf(untouched) == LightingQuality::Ultra);
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
    CHECK(isOn && target == 60 && lowest == 0.5f);
    isOn = false;
    GameQuality::preset(Level::Ultra).dynamicResolutionFor(isOn, target, lowest);
    CHECK(isOn && target == 60 && lowest == 0.65f);
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
