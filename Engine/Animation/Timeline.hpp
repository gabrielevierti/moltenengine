// Timeline.hpp
// A cutscene, a replay, an intro: what happens when, on tracks along time. Things moved along
// keys, the camera cut from one to another, sounds, shared values set, things shown and hidden,
// animations started. A .timeline file of Assets, played by a TimelinePlayer.
//
//   molten-timeline 1
//   length 12
//   track move "Camera"
//     key 0  0 2 10  0 0 0 1
//     key 4  5 3 4   0 0.38 0 0.92
//   track camera
//     at 0 "Camera A"
//     at 6 "Camera B"
//   track sound
//     at 1.5 "Audio/horn.wav" 1
//   track value "introDone"
//     at 12 1
//   track show "Title"
//     at 2 1
//   track trigger "Hero"
//     at 4 "wave"
#pragma once

#include "Core/World.hpp"

#include <optional>
#include <string>
#include <vector>

namespace molten {

struct TimelineKey {
    float time = 0;
    Vec3 position{0, 0, 0};
    Quat rotation{};
};

/// Something that happens at a moment: the camera cut to, the sound played, the value set...
struct TimelineMark {
    float time = 0;
    std::string text;                   // the camera, the sound, the trigger
    float number = 1;                   // the volume, the value, shown (1) or hidden (0)
};

enum class TimelineTrackKind : uint8_t { Move, Camera, Sound, Value, Show, Trigger };

struct TimelineTrack {
    TimelineTrackKind kind = TimelineTrackKind::Move;
    std::string target;                 // the entity moved, shown or animated; the value set
    std::vector<TimelineKey> keys;      // a Move track's, by time
    std::vector<TimelineMark> marks;    // the others', by time

    void sort();
    /// Where a Move track has its entity at `time`: smoothly through the keys.
    bool sample(float time, Vec3& position, Quat& rotation) const;
};

struct Timeline {
    float length = 10.0f;
    std::vector<TimelineTrack> tracks;

    static std::optional<Timeline> parse(const std::string& text, std::string* error = nullptr);
    std::string toText() const;
    static const char* kindName(TimelineTrackKind kind);
};

/// Plays a Timeline: put it on any entity.
struct TimelinePlayer {
    std::string timeline;               // the file in Assets: "Timelines/Intro.timeline"
    bool playsOnStart = true;
    bool loops = false;
    float speed = 1.0f;
    std::string doneValue;              // a shared value set to 1 when it ends: what a script or menu waits for

    void start(World& world, Entity self);
    void update(World& world, Entity self, float dt);

    void play() { isPlaying = true; }
    void pause() { isPlaying = false; }
    /// Everything as it is at `time`, without the sounds and values that happen on the way:
    /// what the editor shows while the time is dragged.
    void seek(World& world, float time);
    /// Applies the moves and cameras at `time`, and (if `fires`) what happens between the time
    /// it was at and this one.
    void advanceTo(World& world, float time, bool fires);
    bool load(World& world);

    Timeline loaded;
    std::string loadedFrom = "\n";
    float time = 0;
    bool isPlaying = false;
    bool hasEnded = false;
};

void linkTimeline();

}  // namespace molten
