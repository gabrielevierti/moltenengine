// Profiler.hpp
// Where a frame's time goes. Parts of the engine (and a game's own code) mark sections of the
// frame with a scope; the profiler keeps the last few hundred frames, each with its sections in
// the order and nesting they ran in, so a slow frame can be looked at after it has passed.
//
//   {
//       Profiler::Scope scope(world.profiler, "Pathfinding");
//       ...                                   // timed until the end of the block
//   }
//
// It costs a clock reading at each end of a section, and nothing when `isEnabled` is off.
#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace molten {

class Profiler {
public:
    bool isEnabled = false;
    static constexpr int kHistory = 300;        // frames kept: five seconds at 60

    struct Section {
        std::string name;
        int depth = 0;                          // 0 for the frame's own parts, 1 inside one of them...
        float ms = 0;
    };
    struct Frame {
        uint64_t number = 0;
        float cpuMs = 0;                        // from the start of the frame to its end, on the CPU
        float gpuMs = 0;                        // what the GPU took for a recent frame (it finishes a frame or two later)
        int drawCalls = 0, triangles = 0, visibleObjects = 0;
        std::vector<Section> sections;
    };

    void beginFrame();
    void endFrame();
    /// A section by hand; prefer Scope, which cannot be left open.
    void begin(const std::string& name);
    void end();

    class Scope {
    public:
        Scope(Profiler& profiler, const char* name) : profiler_(profiler.isEnabled && profiler.isInFrame_ ? &profiler : nullptr) {
            if (profiler_) profiler_->begin(name);
        }
        Scope(Profiler& profiler, const std::string& name) : profiler_(profiler.isEnabled && profiler.isInFrame_ ? &profiler : nullptr) {
            if (profiler_) profiler_->begin(name);
        }
        ~Scope() {
            if (profiler_) profiler_->end();
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        Profiler* profiler_;
    };

    /// The frames kept, oldest first.
    std::vector<const Frame*> frames() const;
    /// The last finished frame, or nullptr.
    const Frame* lastFrame() const;
    /// Filled in by whoever knows them, for the frame being made.
    void setGpuTime(float ms) { gpuMs_ = ms; }
    void setDrawStatistics(int drawCalls, int triangles, int visibleObjects);

    /// Seconds since some moment: for a section measured elsewhere.
    static double now();

private:
    std::vector<Frame> ring_;
    size_t nextSlot_ = 0;
    uint64_t frameNumber_ = 0;
    Frame current_;
    bool isInFrame_ = false;
    double frameStart_ = 0;
    struct Open {
        size_t index;
        double start;
    };
    std::vector<Open> open_;
    float gpuMs_ = 0;
};

}  // namespace molten
