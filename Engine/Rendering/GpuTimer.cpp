#include "Rendering/GpuTimer.hpp"

#include <algorithm>
#include <cstring>

namespace molten {

GpuTimer::GpuTimer(MTL::Device* device) : device_(device) {
    if (!device_ || !device_->supportsCounterSampling(MTL::CounterSamplingPointAtStageBoundary)) return;
    // The set of counters that is the GPU's clock.
    MTL::CounterSet* clock = nullptr;
    NS::Array* sets = device_->counterSets();
    for (NS::UInteger i = 0; sets && i < sets->count(); i++) {
        MTL::CounterSet* set = sets->object<MTL::CounterSet>(i);
        if (set && set->name() && set->name()->isEqualToString(MTL::CommonCounterSetTimestamp)) clock = set;
    }
    if (!clock) return;
    for (int i = 0; i < kFrames; i++) {
        auto descriptor = NS::TransferPtr(MTL::CounterSampleBufferDescriptor::alloc()->init());
        descriptor->setCounterSet(clock);
        descriptor->setSampleCount(NS::UInteger(kMostPasses * 2));
        descriptor->setStorageMode(MTL::StorageModeShared);
        NS::Error* error = nullptr;
        buffers_[i] = NS::TransferPtr(device_->newCounterSampleBuffer(descriptor.get(), &error));
        if (!buffers_[i]) return;
    }
    device_->sampleTimestamps(&firstCpu_, &firstGpu_);
    works_ = true;
}

void GpuTimer::beginFrame(int slot) {
    slot_ = std::clamp(slot, 0, kFrames - 1);
    names_[slot_].clear();
}

void GpuTimer::time(MTL::RenderPassDescriptor* pass, const char* name) {
    if (!works_ || !pass || int(names_[slot_].size()) >= kMostPasses) return;
    NS::UInteger index = NS::UInteger(names_[slot_].size());
    MTL::RenderPassSampleBufferAttachmentDescriptor* sampling = pass->sampleBufferAttachments()->object(0);
    if (!sampling) return;
    sampling->setSampleBuffer(buffers_[slot_].get());
    sampling->setStartOfVertexSampleIndex(index * 2);
    sampling->setEndOfVertexSampleIndex(MTL::CounterDontSample);
    sampling->setStartOfFragmentSampleIndex(MTL::CounterDontSample);
    sampling->setEndOfFragmentSampleIndex(index * 2 + 1);
    names_[slot_].push_back(name ? name : "");
}

void GpuTimer::endFrame(MTL::CommandBuffer* commandBuffer) {
    if (!works_ || names_[slot_].empty() || !commandBuffer) return;
    // How long a tick of the GPU's clock is: from how far it and the CPU's (in nanoseconds) have
    // gone since the timer was made. Apple's GPUs count nanoseconds; others may not.
    MTL::Timestamp cpu = 0, gpu = 0;
    device_->sampleTimestamps(&cpu, &gpu);
    if (gpu > firstGpu_ + 1000000 && cpu > firstCpu_) {
        double ratio = double(cpu - firstCpu_) / double(gpu - firstGpu_);
        if (ratio > 0.001 && ratio < 1000.0) {
            std::lock_guard<std::mutex> lock(shared_->mutex);
            shared_->nanosecondsPerTick = ratio;
        }
    }
    MTL::CounterSampleBuffer* buffer = buffers_[slot_].get();
    std::vector<std::string> names = names_[slot_];
    std::shared_ptr<Shared> shared = shared_;
    commandBuffer->addCompletedHandler([buffer, names, shared](MTL::CommandBuffer*) {
        NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
        struct Drain {
            NS::AutoreleasePool* pool;
            ~Drain() { pool->release(); }
        } drain{pool};
        NS::Data* data = buffer->resolveCounterRange(NS::Range::Make(0, NS::UInteger(names.size() * 2)));
        if (!data || data->length() < names.size() * 2 * sizeof(MTL::CounterResultTimestamp)) return;
        std::vector<MTL::CounterResultTimestamp> times(names.size() * 2);
        std::memcpy(times.data(), data->mutableBytes(), times.size() * sizeof(MTL::CounterResultTimestamp));
        double perTick = 1.0;
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            perTick = shared->nanosecondsPerTick;
        }
        // A pass drawn more than once (for each view of an editor, each step of the bloom) counts once, all of it.
        std::vector<Pass> frame;
        for (size_t i = 0; i < names.size(); i++) {
            uint64_t start = times[i * 2].timestamp, end = times[i * 2 + 1].timestamp;
            if (start == MTL::CounterErrorValue || end == MTL::CounterErrorValue || end < start) continue;
            float ms = float(double(end - start) * perTick / 1.0e6);
            auto same = std::find_if(frame.begin(), frame.end(), [&](const Pass& p) { return p.name == names[i]; });
            if (same != frame.end()) same->milliseconds += ms;
            else frame.push_back(Pass{names[i], ms});
        }
        std::lock_guard<std::mutex> lock(shared->mutex);
        // Each averaged with what the last frames had of it, for a steady number to read.
        std::vector<Pass> next;
        for (const Pass& now : frame) {
            auto before = std::find_if(shared->passes.begin(), shared->passes.end(), [&](const Pass& p) { return p.name == now.name; });
            next.push_back(Pass{now.name, before == shared->passes.end() ? now.milliseconds : before->milliseconds * 0.9f + now.milliseconds * 0.1f});
        }
        shared->passes = std::move(next);
    });
}

std::vector<GpuTimer::Pass> GpuTimer::passes() const {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    return shared_->passes;
}

}  // namespace molten
