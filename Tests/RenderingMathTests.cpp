#include "Test.hpp"

#include "Audio/AudioClip.hpp"
#include "Rendering/Canvas.hpp"
#include "Rendering/Cascades.hpp"
#include "Rendering/DebugDraw.hpp"
#include "Rendering/Particles.hpp"
#include "Rendering/ShaderSource.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>

using namespace molten;

// ---------------------------------------------------------------- shared GPU layouts

TEST(gpu_structs_have_the_sizes_the_shaders_expect) {
    // Sizes and offsets under the C layout rules, which the Metal Shading Language shares
    // for these types (a float3 is 16 bytes, aligned to 16). The Swift tests check the
    // same numbers. If a field is added to ShaderTypes.h, update them on purpose.
    CHECK(sizeof(gpu::PassUniforms) == 368);
    CHECK(sizeof(gpu::InstanceData) == 144);
    CHECK(sizeof(gpu::LightData) == 96);
    CHECK(sizeof(gpu::MaterialUniforms) == 208);
    CHECK(offsetof(gpu::MaterialUniforms, customShader) == 72);
    CHECK(offsetof(gpu::MaterialUniforms, custom) == 80);
    CHECK(offsetof(gpu::MaterialUniforms, water) == 64);
    CHECK(sizeof(gpu::CanvasVertex) == 32);
    CHECK(sizeof(gpu::LineVertex) == 32);
    CHECK(sizeof(gpu::ParticleInstance) == 32);
    CHECK(offsetof(gpu::PassUniforms, cameraPosition) == 320);
    CHECK(offsetof(gpu::PassUniforms, time) == 336);
    CHECK(offsetof(gpu::LightData, type) == 48);
    CHECK(offsetof(gpu::FrameUniforms, cascadeSplits) == 256);
    CHECK(sizeof(gpu::FrameUniforms) == 544);
    CHECK(offsetof(gpu::FrameUniforms, fogShaftSteps) == 528);
    CHECK(offsetof(gpu::FrameUniforms, volumetricClouds) == 532);
    CHECK(offsetof(gpu::FrameUniforms, fogHeightFalloff) == 508);
    CHECK(offsetof(gpu::FrameUniforms, tracesReflections) == 520);
    CHECK(offsetof(gpu::FrameUniforms, bounceIntensity) == 524);
    CHECK(sizeof(gpu::TracedSurface) == 32);
    CHECK(offsetof(gpu::FrameUniforms, lightCount) == 352);
    CHECK(offsetof(gpu::FrameUniforms, fogDensity) == 480);
    CHECK(offsetof(gpu::FrameUniforms, shadowRayCount) == 500);
    CHECK(offsetof(gpu::FrameUniforms, sunAngularRadius) == 504);
}

// ---------------------------------------------------------------- cascaded shadows

TEST(each_cascade_contains_its_slice_of_the_view) {
    Mat4 cameraWorld = simd::inverse(lookAt(Vec3{3, 5, 8}, Vec3{0, 0, 0}));
    float fovY = radians(60), aspect = 16.0f / 9.0f, nearPlane = 0.1f;
    CascadeSettings settings;
    Vec3 lightDirection = simd::normalize(Vec3{-0.4f, -1.0f, -0.3f});
    Cascades cascades = computeCascades(cameraWorld, fovY, aspect, nearPlane, lightDirection, settings);

    CHECK(cascades.count == 4);
    CHECK_NEAR(cascades.splitFar[3], settings.shadowDistance, 1e-3);
    float previousFar = nearPlane;
    for (int i = 0; i < cascades.count; i++) {
        CHECK(cascades.splitFar[i] > previousFar);
        // Every corner of the slice must land inside the cascade's clip volume.
        for (float distance : {previousFar, cascades.splitFar[i]}) {
            for (float sx : {-1.0f, 1.0f}) {
                for (float sy : {-1.0f, 1.0f}) {
                    Vec3 corner = transformPoint(cameraWorld, Vec3{sx * std::tan(fovY / 2) * aspect * distance, sy * std::tan(fovY / 2) * distance, -distance});
                    Vec3 clip = transformPoint(cascades.viewProjection[i], corner);
                    CHECK(std::abs(clip.x) <= 1.001f && std::abs(clip.y) <= 1.001f);
                    CHECK(clip.z >= 0.0f && clip.z <= 1.0f);
                }
            }
        }
        previousFar = cascades.splitFar[i];
    }

    // An object above the view, toward the light, is still inside the depth range,
    // so it can cast a shadow into the view.
    Vec3 above = Vec3{0, 0, 0} - lightDirection * 40.0f;
    Vec3 clip = transformPoint(cascades.viewProjection[0], above);
    CHECK(clip.z >= 0.0f && clip.z <= 1.0f);
}

TEST(cascades_move_in_whole_texels) {
    // Shadows shimmer if the shadow map slides by a fraction of a texel as the camera moves.
    CascadeSettings settings;
    Vec3 lightDirection = simd::normalize(Vec3{0.3f, -1.0f, 0.2f});
    float fovY = radians(60);
    Vec3 worldPoint{1.234f, 0.0f, -2.345f};
    Cascades base = computeCascades(simd::inverse(lookAt(Vec3{0, 2, 5}, Vec3{0, 0, 0})), fovY, 1.5f, 0.1f, lightDirection, settings);
    for (float shift : {0.013f, 0.11f, 0.37f}) {
        Vec3 offset{shift, 0, shift * 0.5f};
        Cascades moved = computeCascades(simd::inverse(lookAt(Vec3{0, 2, 5} + offset, offset)), fovY, 1.5f, 0.1f, lightDirection, settings);
        for (int i = 0; i < base.count; i++) {
            CHECK_NEAR(base.radius[i], moved.radius[i], 1e-5);
            // The same world point must land on shadow-map positions a whole number of texels apart.
            Vec3 a = transformPoint(base.viewProjection[i], worldPoint), b = transformPoint(moved.viewProjection[i], worldPoint);
            float texelsX = (a.x - b.x) * 0.5f * float(settings.resolution);
            float texelsY = (a.y - b.y) * 0.5f * float(settings.resolution);
            CHECK_NEAR(texelsX, std::round(texelsX), 0.02);
            CHECK_NEAR(texelsY, std::round(texelsY), 0.02);
        }
    }
}

// ---------------------------------------------------------------- particles

TEST(particles_reach_a_steady_count_and_respect_the_cap) {
    Scene scene;
    Entity e = scene.create();
    scene.add(e, Transform{.position = {1, 2, 3}});
    ParticleEmitter emitter;
    emitter.rate = 100;
    emitter.lifetimeMin = emitter.lifetimeMax = 0.5f;
    scene.add(e, emitter);
    scene.updateTransforms();
    for (int i = 0; i < 240; i++) updateParticles(scene, 1.0f / 120.0f);
    size_t alive = scene.get<ParticleEmitter>(e)->particles.size();
    CHECK(alive >= 48 && alive <= 52);                   // rate times lifetime

    scene.get<ParticleEmitter>(e)->maxParticles = 20;
    scene.get<ParticleEmitter>(e)->burst(1000);
    updateParticles(scene, 1.0f / 120.0f);
    CHECK(scene.get<ParticleEmitter>(e)->particles.size() <= 52);
    scene.get<ParticleEmitter>(e)->isEmitting = false;
    for (int i = 0; i < 120; i++) updateParticles(scene, 1.0f / 120.0f);
    CHECK(scene.get<ParticleEmitter>(e)->particles.empty());
}

TEST(a_burst_starts_at_the_emitter_and_flies_along_its_direction) {
    Scene scene;
    Entity e = scene.create();
    scene.add(e, Transform{.position = {5, 0, 0}, .rotation = axisAngle(Vec3{0, 0, 1}, radians(-90))});   // local +Y becomes world +X
    ParticleEmitter emitter;
    emitter.rate = 0;
    emitter.spread = radians(10);
    emitter.gravity = Vec3{0, 0, 0};
    emitter.burst(50);
    scene.add(e, emitter);
    scene.updateTransforms();
    updateParticles(scene, 0.0f);
    const auto& particles = scene.get<ParticleEmitter>(e)->particles;
    CHECK(particles.size() == 50);
    for (const Particle& particle : particles) {
        CHECK_NEAR(simd::distance(particle.position, Vec3{5, 0, 0}), 0, 1e-4);
        CHECK(simd::normalize(particle.velocity).x > std::cos(radians(10.5f)));
    }
}

// ---------------------------------------------------------------- sound synthesis

TEST(synthesized_clips_have_the_right_length_level_and_pitch) {
    SynthParams params;
    params.frequency = 441;
    params.duration = 0.5f;
    params.volume = 0.4f;
    AudioClip clip = synthesize(params, 44100);
    CHECK(clip.samples.size() == 22050);
    CHECK_NEAR(clip.duration(), 0.5, 1e-4);
    float peak = 0;
    int risingZeroCrossings = 0;
    for (size_t i = 1; i < clip.samples.size(); i++) {
        peak = std::max(peak, std::abs(clip.samples[i]));
        if (clip.samples[i - 1] < 0 && clip.samples[i] >= 0) risingZeroCrossings++;
    }
    CHECK(peak <= 0.4f + 1e-4f && peak > 0.38f);
    CHECK(std::abs(risingZeroCrossings - 220) <= 2);     // 441 Hz for half a second
    CHECK_NEAR(clip.samples.front(), 0, 1e-3);           // the envelope removes clicks at both ends
    CHECK_NEAR(clip.samples.back(), 0, 1e-2);
}

TEST(noise_is_repeatable_and_sequences_concatenate) {
    SynthParams noise;
    noise.waveform = Waveform::Noise;
    noise.seed = 9;
    CHECK(synthesize(noise).samples == synthesize(noise).samples);
    SynthParams note;
    note.duration = 0.1f;
    CHECK(synthesizeSequence({note, note, note}).samples.size() == 3 * synthesize(note).samples.size());
}

// ---------------------------------------------------------------- shader source

TEST(shader_source_resolves_includes_once_and_keeps_line_numbers) {
    namespace fs = std::filesystem;
    fs::path root = fs::temp_directory_path() / "lm_shader_test";
    fs::remove_all(root);
    fs::create_directories(root / "Shared");
    std::ofstream(root / "Shared" / "Types.h") << "struct Shared { float x; };\n";
    std::ofstream(root / "Common.metal") << "#include \"Shared/Types.h\"\nfloat one() { return 1.0; }\n";
    std::ofstream(root / "A.metal") << "#include \"Common.metal\"\nfloat a() { return one(); }\n";
    std::ofstream(root / "B.metal") << "#include \"Common.metal\"\n#include \"Missing.metal\"\nfloat b() { return one(); }\n";
    std::ofstream(root / "notes.txt") << "not a shader\n";

    ShaderSource source = ShaderSource::load(root.string());
    auto count = [&](const std::string& needle) {
        size_t n = 0;
        for (size_t at = source.text.find(needle); at != std::string::npos; at = source.text.find(needle, at + 1)) n++;
        return n;
    };
    CHECK(count("struct Shared") == 1);                  // included by two files, present once
    CHECK(count("float one()") == 1);
    CHECK(count("float a()") == 1 && count("float b()") == 1);
    CHECK(count("not a shader") == 0);
    CHECK(count("#line 2 \"A.metal\"") == 1);            // after the include, line numbers continue
    CHECK(source.files.size() == 4);
    CHECK(source.error.find("B.metal:2") != std::string::npos);
    CHECK(source.text.find("struct Shared") < source.text.find("float one()"));
    fs::remove_all(root);

    CHECK(!ShaderSource::load((root / "does-not-exist").string()).error.empty());
}

// ---------------------------------------------------------------- text and 2D

namespace {
const char* kTestFont =
    "info 40 50 8 256 256\n"
    "65 0 0 30 40 -2 4 24\n"        // A
    "66 32 0 28 40 0 4 22\n"        // B
    "32 0 0 0 0 0 0 10\n"           // space
    "63 64 0 20 40 0 4 18\n"        // ?
    "232 96 0 22 40 0 4 20\n";      // e with a grave accent
}

TEST(utf8_decoding) {
    size_t i = 0;
    std::string text = "A\xC3\xA8\xE2\x82\xAC\xF0\x9F\x99\x82\xFFZ";       // A, e-grave, euro sign, an emoji, an invalid byte, Z
    CHECK(decodeUTF8(text, i) == 'A');
    CHECK(decodeUTF8(text, i) == 0xE8);
    CHECK(decodeUTF8(text, i) == 0x20AC);
    CHECK(decodeUTF8(text, i) == 0x1F642);
    CHECK(decodeUTF8(text, i) == 0xFFFD);
    CHECK(decodeUTF8(text, i) == 'Z');
    CHECK(i == text.size());
}

TEST(font_metrics_parse_and_measure) {
    auto font = FontMetrics::parse(kTestFont);
    CHECK(font.has_value());
    CHECK(font->glyphs.size() == 5);
    CHECK_NEAR(font->glyph('B')->advance, 22, 1e-6);
    CHECK(font->glyph(0x4E2D) == font->glyph('?'));      // unknown characters fall back to '?'
    Vec2 size = font->measure("AB A", 20);               // half the baked size
    CHECK_NEAR(size.x, (24 + 22 + 10 + 24) * 0.5, 1e-4);
    CHECK_NEAR(size.y, 25, 1e-4);
    CHECK_NEAR(font->measure("A\nAB", 40).x, 46, 1e-4);
    CHECK_NEAR(font->measure("A\nAB", 40).y, 100, 1e-4);
    CHECK_NEAR(font->measure("\xC3\xA8", 40).x, 20, 1e-4);
    CHECK(!FontMetrics::parse("garbage").has_value());
}

TEST(canvas_batches_by_texture_and_lays_out_text) {
    auto font = FontMetrics::parse(kTestFont);
    const Texture* atlas = reinterpret_cast<const Texture*>(uintptr_t(0x1000));     // only compared, never used
    const Texture* sprite = reinterpret_cast<const Texture*>(uintptr_t(0x2000));
    Canvas canvas;
    canvas.setFont(&*font, atlas);
    canvas.begin(Vec2{800, 600});

    canvas.rect(Vec2{10, 20}, Vec2{100, 50}, rgb(0xff0000));
    canvas.rect(Vec2{10, 80}, Vec2{100, 50}, rgb(0x00ff00));
    CHECK(canvas.batches().size() == 1);                 // two solid rectangles share a draw call
    CHECK(canvas.vertices().size() == 12);
    CHECK_NEAR(canvas.vertices()[0].position.x, 10, 1e-6);
    CHECK_NEAR(canvas.vertices()[2].position.y, 70, 1e-6);
    CHECK_NEAR(canvas.vertices()[0].color.x, 1, 1e-6);

    canvas.image(sprite, Vec2{0, 0}, Vec2{32, 32});
    canvas.text("A B", Vec2{400, 100}, 40, rgb(0xffffff), TextAlign::Center);
    canvas.circle(Vec2{50, 50}, 10, rgb(0xffffff), 8);
    CHECK(canvas.batches().size() == 4);
    CHECK(canvas.batches()[1].texture == sprite && !canvas.batches()[1].isText);
    CHECK(canvas.batches()[2].texture == atlas && canvas.batches()[2].isText);
    CHECK(canvas.batches()[2].vertexCount == 12);        // the space draws nothing
    CHECK(canvas.batches()[3].vertexCount == 66);        // six triangles inside, and two per side for the soft edge

    // Centered: the line is 24 + 10 + 22 = 56 wide, so the first glyph starts at 400 - 28 + offsetX.
    const gpu::CanvasVertex& first = canvas.vertices()[canvas.batches()[2].firstVertex];
    CHECK_NEAR(first.position.x, 400 - 28 - 2, 1e-4);
    CHECK_NEAR(first.position.y, 104, 1e-4);
    CHECK_NEAR(first.uv.x, 0, 1e-6);
    uint32_t total = 0;
    for (const Canvas::Batch& batch : canvas.batches()) {
        CHECK(batch.firstVertex == total);
        total += batch.vertexCount;
    }
    CHECK(total == canvas.vertices().size());

    canvas.begin(Vec2{800, 600});
    CHECK(canvas.vertices().empty() && canvas.batches().empty());
}

TEST(canvas_shapes_fade_out_over_one_pixel) {
    Canvas canvas;
    canvas.begin(Vec2{800, 600}, 2.0f);                 // a Retina display: one pixel is half a point
    CHECK_NEAR(canvas.pixelSize(), 0.5f, 1e-6f);
    canvas.roundedRect(Vec2{100, 100}, Vec2{200, 80}, 12, Vec4{1, 0, 0, 0.8f});
    CHECK(canvas.batches().size() == 1);
    CHECK(canvas.vertices().size() % 3 == 0);
    int solid = 0, clear = 0;
    for (const gpu::CanvasVertex& v : canvas.vertices()) {
        // Nothing is drawn more than half a pixel outside the rectangle asked for.
        CHECK(v.position.x >= 100 - 0.26f && v.position.x <= 300 + 0.26f);
        CHECK(v.position.y >= 100 - 0.26f && v.position.y <= 180 + 0.26f);
        if (v.color.w == 0.0f) clear++; else { solid++; CHECK_NEAR(v.color.w, 0.8f, 1e-6f); }
    }
    CHECK(solid > 0 && clear > 0);

    // A circle touches its radius on all four sides.
    canvas.begin(Vec2{800, 600});
    canvas.circle(Vec2{50, 50}, 20, Vec4{1, 1, 1, 1});
    float minX = 1e9f, maxX = -1e9f;
    for (const gpu::CanvasVertex& v : canvas.vertices()) { minX = std::min(minX, v.position.x); maxX = std::max(maxX, v.position.x); }
    CHECK_NEAR(minX, 29.5f, 0.2f);
    CHECK_NEAR(maxX, 70.5f, 0.2f);
}

TEST(a_line_thinner_than_a_pixel_is_drawn_fainter) {
    Canvas canvas;
    canvas.begin(Vec2{800, 600});
    canvas.line(Vec2{10, 10}, Vec2{110, 10}, 0.25f, Vec4{1, 1, 1, 1});
    CHECK(!canvas.vertices().empty());
    float strongest = 0, top = 1e9f, bottom = -1e9f;
    for (const gpu::CanvasVertex& v : canvas.vertices()) {
        strongest = std::max(strongest, v.color.w);
        top = std::min(top, v.position.y);
        bottom = std::max(bottom, v.position.y);
    }
    CHECK_NEAR(strongest, 0.25f, 1e-5f);
    CHECK_NEAR(bottom - top, 2.0f, 1e-4f);              // one pixel, fading on both sides
}

TEST(canvas_clips_nest_and_split_draw_calls) {
    Canvas canvas;
    canvas.begin(Vec2{800, 600});
    canvas.rect(Vec2{0, 0}, Vec2{10, 10}, Vec4{1, 1, 1, 1});
    canvas.pushClip(Vec2{100, 100}, Vec2{200, 200});
    canvas.rect(Vec2{0, 0}, Vec2{10, 10}, Vec4{1, 1, 1, 1});
    canvas.pushClip(Vec2{250, 50}, Vec2{200, 100});     // sticks out of the first one
    canvas.rect(Vec2{0, 0}, Vec2{10, 10}, Vec4{1, 1, 1, 1});
    canvas.pushClip(Vec2{0, 0}, Vec2{50, 50});          // entirely outside: nothing can be drawn
    canvas.rect(Vec2{0, 0}, Vec2{10, 10}, Vec4{1, 1, 1, 1});
    canvas.circle(Vec2{5, 5}, 4, Vec4{1, 1, 1, 1});
    canvas.popClip();
    canvas.popClip();
    canvas.popClip();
    canvas.rect(Vec2{0, 0}, Vec2{10, 10}, Vec4{1, 1, 1, 1});
    canvas.popClip();                                   // one too many: harmless

    CHECK(canvas.batches().size() == 4);
    CHECK(!canvas.batches()[0].isClipped && canvas.batches()[1].isClipped && !canvas.batches()[3].isClipped);
    const Canvas::Batch& nested = canvas.batches()[2];
    CHECK(simd::all(nested.clipMin == Vec2{250, 100}) && simd::all(nested.clipMax == Vec2{300, 150}));
    CHECK(canvas.vertices().size() == 4 * 6);
}

TEST(debug_draw_builds_line_lists) {
    DebugDraw debug;
    debug.box(AABB(Vec3{-1, -1, -1}, Vec3{1, 1, 1}), Vec4{1, 1, 1, 1});
    CHECK(debug.vertices().size() == 24);                // twelve edges
    debug.sphere(Vec3{0, 0, 0}, 1, Vec4{1, 1, 1, 1}, 16);
    CHECK(debug.vertices().size() == 24 + 3 * 16 * 2);
    debug.clear();
    CHECK(debug.vertices().empty());
}

