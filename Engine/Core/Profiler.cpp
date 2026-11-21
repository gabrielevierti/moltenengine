#include "Core/Profiler.hpp"

namespace molten {

double Profiler::now() {
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

void Profiler::beginFrame() {
    if (!isEnabled) {
        isInFrame_ = false;
        return;
    }
    current_ = Frame{};
    current_.number = ++frameNumber_;
    current_.sections.reserve(64);
    open_.clear();
    frameStart_ = now();
    isInFrame_ = true;
}

void Profiler::begin(const std::string& name) {
    if (!isEnabled || !isInFrame_) return;
    Section section;
    section.name = name;
    section.depth = int(open_.size());
    current_.sections.push_back(std::move(section));
    open_.push_back(Open{current_.sections.size() - 1, now()});
}

void Profiler::end() {
    if (!isInFrame_ || open_.empty()) return;
    Open last = open_.back();
    open_.pop_back();
    current_.sections[last.index].ms = float((now() - last.start) * 1000.0);
}

void Profiler::setDrawStatistics(int drawCalls, int triangles, int visibleObjects) {
    current_.drawCalls = drawCalls;
    current_.triangles = triangles;
    current_.visibleObjects = visibleObjects;
}

void Profiler::endFrame() {
    if (!isInFrame_) return;
    while (!open_.empty()) end();               // a section left open ends with the frame
    current_.cpuMs = float((now() - frameStart_) * 1000.0);
    current_.gpuMs = gpuMs_;
    if (ring_.size() < size_t(kHistory)) {
        ring_.push_back(std::move(current_));
    } else {
        ring_[nextSlot_] = std::move(current_);
    }
    nextSlot_ = (nextSlot_ + 1) % size_t(kHistory);
    isInFrame_ = false;
}

std::vector<const Profiler::Frame*> Profiler::frames() const {
    std::vector<const Frame*> out;
    out.reserve(ring_.size());
    if (ring_.size() < size_t(kHistory)) {
        for (const Frame& frame : ring_) out.push_back(&frame);
    } else {
        for (size_t i = 0; i < ring_.size(); i++) out.push_back(&ring_[(nextSlot_ + i) % ring_.size()]);
    }
    return out;
}

const Profiler::Frame* Profiler::lastFrame() const {
    if (ring_.empty()) return nullptr;
    return &ring_[(nextSlot_ + ring_.size() - 1) % ring_.size()];
}

}  // namespace molten
