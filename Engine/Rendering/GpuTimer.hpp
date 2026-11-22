// GpuTimer.hpp
// What each pass of a frame costs the GPU: the graphics chip notes the time as each pass starts
// and ends (Metal's counters, at the boundaries of its stages), and once the frame is done the
// times are read back. Shown in the Profiler (Frame), pass by pass, averaged over the last frames.
// Where the GPU cannot take the times, there are none: nothing else changes.
#pragma once

#include <Metal/Metal.hpp>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace molten {

class GpuTimer {
public:
    struct Pass {
        std::string name;
        float milliseconds = 0;             // averaged over the last frames
    };

    static constexpr int kMostPasses = 48;
    static constexpr int kFrames = 3;

    explicit GpuTimer(MTL::Device* device);

    /// The start of a frame: `slot` is which of the frames in flight it is (each has its own buffer).
    void beginFrame(int slot);
    /// The times of this pass are taken, under `name`. Before its encoder is made.
    void time(MTL::RenderPassDescriptor* pass, const char* name);
    /// The frame's times are read when `commandBuffer` is done.
    void endFrame(MTL::CommandBuffer* commandBuffer);
    /// Pass by pass, in the order they ran, of the frames read so far.
    std::vector<Pass> passes() const;
    bool works() const { return works_; }

private:
    struct Shared {
        std::mutex mutex;
        std::vector<Pass> passes;
        double nanosecondsPerTick = 1.0;
    };
    MTL::Device* device_ = nullptr;
    bool works_ = false;
    NS::SharedPtr<MTL::CounterSampleBuffer> buffers_[kFrames];
    std::vector<std::string> names_[kFrames];
    int slot_ = 0;
    std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
    MTL::Timestamp firstCpu_ = 0, firstGpu_ = 0;
};

}  // namespace molten
