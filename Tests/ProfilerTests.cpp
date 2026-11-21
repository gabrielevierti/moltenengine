// The profiler: sections of a frame in the order and nesting they ran in, a history of frames.
#include "Test.hpp"

#include "Core/World.hpp"

#include <thread>

using namespace molten;

TEST(the_profiler_keeps_each_frames_sections_in_order) {
    Profiler profiler;
    profiler.beginFrame();
    { Profiler::Scope off(profiler, "not kept"); }
    profiler.endFrame();
    CHECK(profiler.frames().empty());                       // off: nothing kept, nothing costs

    profiler.isEnabled = true;
    for (int frame = 0; frame < Profiler::kHistory + 20; frame++) {
        profiler.beginFrame();
        {
            Profiler::Scope update(profiler, "Update");
            Profiler::Scope inner(profiler, "Racer");
            if (frame == 0) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        { Profiler::Scope physics(profiler, std::string("Physics")); }
        profiler.setGpuTime(3.5f);
        profiler.endFrame();
    }
    std::vector<const Profiler::Frame*> frames = profiler.frames();
    CHECK(frames.size() == size_t(Profiler::kHistory));     // the oldest ones went
    CHECK(frames.front()->number + Profiler::kHistory - 1 == frames.back()->number);
    const Profiler::Frame* last = profiler.lastFrame();
    CHECK(last == frames.back() && last->gpuMs == 3.5f);
    CHECK(last->sections.size() == 3);
    CHECK(last->sections[0].name == "Update" && last->sections[0].depth == 0);
    CHECK(last->sections[1].name == "Racer" && last->sections[1].depth == 1);
    CHECK(last->sections[2].name == "Physics" && last->sections[2].depth == 0);
    CHECK(last->sections[0].ms >= last->sections[1].ms && last->cpuMs >= last->sections[0].ms);
}

TEST(a_world_profiles_its_frame_by_part_and_by_component) {
    World world;
    world.profiler.isEnabled = true;
    world.advance(1.0 / 60.0);
    const Profiler::Frame* frame = world.profiler.lastFrame();
    CHECK(frame != nullptr);
    if (!frame) return;
    bool hasUpdate = false, hasParticles = false;
    for (const Profiler::Section& section : frame->sections) {
        hasUpdate = hasUpdate || section.name == "Update";
        hasParticles = hasParticles || section.name == "Particles";
    }
    CHECK(hasUpdate && hasParticles);
}
