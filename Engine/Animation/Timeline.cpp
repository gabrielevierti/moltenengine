// Timeline.cpp
#include "Animation/Timeline.hpp"

#include "Animation/Animator.hpp"
#include "Scene/Components.hpp"
#include "Core/Log.hpp"
#include "Core/TextFile.hpp"
#include "Game/Component.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace molten {

void linkTimeline() {}

namespace {

/// The words of a line: quoted ones whole.
std::vector<std::string> wordsOf(const std::string& line) {
    std::vector<std::string> words;
    size_t i = 0;
    while (i < line.size()) {
        if (line[i] == ' ' || line[i] == '\t' || line[i] == '\r') {
            i++;
            continue;
        }
        std::string word;
        if (line[i] == '"') {
            for (i++; i < line.size() && line[i] != '"'; i++) {
                if (line[i] == '\\' && i + 1 < line.size()) i++;
                word += line[i];
            }
            i++;
        } else {
            while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') word += line[i++];
        }
        words.push_back(word);
    }
    return words;
}

std::string quoted(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

float number(const std::vector<std::string>& words, size_t i, float fallback = 0) {
    return i < words.size() ? std::strtof(words[i].c_str(), nullptr) : fallback;
}

const TimelineTrackKind kKinds[] = {TimelineTrackKind::Move, TimelineTrackKind::Camera, TimelineTrackKind::Sound,
                                    TimelineTrackKind::Value, TimelineTrackKind::Show, TimelineTrackKind::Trigger};

}  // namespace

const char* Timeline::kindName(TimelineTrackKind kind) {
    switch (kind) {
    case TimelineTrackKind::Move: return "move";
    case TimelineTrackKind::Camera: return "camera";
    case TimelineTrackKind::Sound: return "sound";
    case TimelineTrackKind::Value: return "value";
    case TimelineTrackKind::Show: return "show";
    case TimelineTrackKind::Trigger: return "trigger";
    }
    return "move";
}

void TimelineTrack::sort() {
    std::stable_sort(keys.begin(), keys.end(), [](const TimelineKey& a, const TimelineKey& b) { return a.time < b.time; });
    std::stable_sort(marks.begin(), marks.end(), [](const TimelineMark& a, const TimelineMark& b) { return a.time < b.time; });
}

bool TimelineTrack::sample(float time, Vec3& position, Quat& rotation) const {
    if (keys.empty()) return false;
    if (time <= keys.front().time) {
        position = keys.front().position;
        rotation = keys.front().rotation;
        return true;
    }
    if (time >= keys.back().time) {
        position = keys.back().position;
        rotation = keys.back().rotation;
        return true;
    }
    size_t i = 1;
    while (i < keys.size() && keys[i].time < time) i++;
    const TimelineKey& a = keys[i - 1];
    const TimelineKey& b = keys[i];
    float span = std::max(b.time - a.time, 1e-5f);
    float t = (time - a.time) / span;
    // Through the keys smoothly (Catmull-Rom on where it is), easing in and out of the first and last.
    Vec3 before = i >= 2 ? keys[i - 2].position : a.position;
    Vec3 after = i + 1 < keys.size() ? keys[i + 1].position : b.position;
    float t2 = t * t, t3 = t2 * t;
    position = ((a.position * 2.0f) + (b.position - before) * t + (before * 2.0f - a.position * 5.0f + b.position * 4.0f - after) * t2 +
                (a.position * 3.0f - before - b.position * 3.0f + after) * t3) * 0.5f;
    rotation = simd::slerp(a.rotation, b.rotation, t * t * (3 - 2 * t));
    return true;
}

std::optional<Timeline> Timeline::parse(const std::string& text, std::string* error) {
    Timeline timeline;
    std::istringstream lines(text);
    std::string line;
    bool hasHeader = false;
    int lineNumber = 0;
    while (std::getline(lines, line)) {
        lineNumber++;
        std::vector<std::string> words = wordsOf(line);
        if (words.empty() || words[0][0] == '#') continue;
        const std::string& keyword = words[0];
        if (keyword == "molten-timeline") {
            hasHeader = true;
        } else if (keyword == "length") {
            timeline.length = std::max(number(words, 1, 10), 0.01f);
        } else if (keyword == "track") {
            TimelineTrack track;
            bool known = false;
            for (TimelineTrackKind kind : kKinds) {
                if (words.size() > 1 && words[1] == kindName(kind)) {
                    track.kind = kind;
                    known = true;
                }
            }
            if (!known) {
                if (error) *error = "line " + std::to_string(lineNumber) + ": no track of the kind " + (words.size() > 1 ? words[1] : std::string());
                return std::nullopt;
            }
            if (words.size() > 2) track.target = words[2];
            timeline.tracks.push_back(track);
        } else if (keyword == "key" && !timeline.tracks.empty()) {
            TimelineKey key;
            key.time = number(words, 1);
            key.position = Vec3{number(words, 2), number(words, 3), number(words, 4)};
            key.rotation = Quat(number(words, 5), number(words, 6), number(words, 7), number(words, 8, 1));
            key.rotation = simd::normalize(key.rotation);
            timeline.tracks.back().keys.push_back(key);
        } else if (keyword == "at" && !timeline.tracks.empty()) {
            TimelineMark mark;
            mark.time = number(words, 1);
            TimelineTrackKind kind = timeline.tracks.back().kind;
            bool hasText = kind == TimelineTrackKind::Camera || kind == TimelineTrackKind::Sound || kind == TimelineTrackKind::Trigger;
            if (hasText) {
                mark.text = words.size() > 2 ? words[2] : std::string();
                mark.number = number(words, 3, 1);
            } else {
                mark.number = number(words, 2, 1);
            }
            timeline.tracks.back().marks.push_back(mark);
        } else {
            if (error) *error = "line " + std::to_string(lineNumber) + ": " + keyword + " is not something a timeline has";
            return std::nullopt;
        }
    }
    if (!hasHeader) {
        if (error) *error = "not a timeline: it starts with \"molten-timeline 1\"";
        return std::nullopt;
    }
    for (TimelineTrack& track : timeline.tracks) track.sort();
    return timeline;
}

std::string Timeline::toText() const {
    std::string out = "molten-timeline 1\n";
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "length %g\n", double(length));
    out += buffer;
    for (const TimelineTrack& track : tracks) {
        out += std::string("track ") + kindName(track.kind);
        if (track.kind != TimelineTrackKind::Camera && track.kind != TimelineTrackKind::Sound) out += " " + quoted(track.target);
        out += "\n";
        for (const TimelineKey& key : track.keys) {
            std::snprintf(buffer, sizeof(buffer), "  key %g  %g %g %g  %g %g %g %g\n", double(key.time), double(key.position.x), double(key.position.y),
                          double(key.position.z), double(key.rotation.vector.x), double(key.rotation.vector.y), double(key.rotation.vector.z),
                          double(key.rotation.vector.w));
            out += buffer;
        }
        for (const TimelineMark& mark : track.marks) {
            std::snprintf(buffer, sizeof(buffer), "  at %g", double(mark.time));
            out += buffer;
            bool hasText = track.kind == TimelineTrackKind::Camera || track.kind == TimelineTrackKind::Sound || track.kind == TimelineTrackKind::Trigger;
            if (hasText) out += " " + quoted(mark.text);
            if (!hasText || track.kind == TimelineTrackKind::Sound) {
                std::snprintf(buffer, sizeof(buffer), " %g", double(mark.number));
                out += buffer;
            }
            out += "\n";
        }
    }
    return out;
}

// ---------------------------------------------------------------- the player

bool TimelinePlayer::load(World& world) {
    loadedFrom = timeline;
    loaded = Timeline{};
    if (timeline.empty()) return false;
    std::optional<std::string> text = text::readFile(world.assetPath(timeline));
    if (!text) {
        logWarning("there is no timeline %s", timeline.c_str());
        return false;
    }
    std::string error;
    std::optional<Timeline> read = Timeline::parse(*text, &error);
    if (!read) {
        logWarning("%s: %s", timeline.c_str(), error.c_str());
        return false;
    }
    loaded = *read;
    return true;
}

void TimelinePlayer::start(World& world, Entity) {
    load(world);
    time = 0;
    hasEnded = false;
    isPlaying = playsOnStart;
    if (isPlaying) advanceTo(world, 0, true);
}

void TimelinePlayer::advanceTo(World& world, float to, bool fires) {
    float from = time;
    time = to;
    Scene& scene = world.scene;
    for (const TimelineTrack& track : loaded.tracks) {
        switch (track.kind) {
        case TimelineTrackKind::Move: {
            Entity entity = world.find(track.target);
            Transform* transform = scene.get<Transform>(entity);
            Vec3 position;
            Quat rotation;
            if (!transform || !track.sample(to, position, rotation)) break;
            // Keys are where it is in the world; its transform keeps it relative to its parent.
            Mat4 placed = compose(position, rotation, transform->scale);
            if (const Transform* parent = scene.get<Transform>(transform->parent)) placed = simd::inverse(parent->world) * placed;
            Vec3 scale;
            decompose(placed, transform->position, transform->rotation, scale);
            break;
        }
        case TimelineTrackKind::Camera: {
            // The last cut at or before now: that camera on, the others the track names off.
            const TimelineMark* now = nullptr;
            for (const TimelineMark& mark : track.marks) {
                if (mark.time <= to + 1e-5f) now = &mark;
            }
            if (!now) break;
            for (const TimelineMark& mark : track.marks) {
                if (Camera* camera = scene.get<Camera>(world.find(mark.text))) camera->isActive = mark.text == now->text;
            }
            break;
        }
        default: {
            for (const TimelineMark& mark : track.marks) {
                // What happens in (from, to], and at 0 when it begins there.
                bool crosses = (mark.time > from && mark.time <= to) || (from == 0 && to == 0 && mark.time == 0);
                bool isNow = mark.time <= to;
                if (track.kind == TimelineTrackKind::Show) {
                    // Shown or hidden as the last mark before now says, however it was reached.
                    if (!isNow) continue;
                    if (Transform* transform = scene.get<Transform>(world.find(track.target))) transform->isActive = mark.number > 0.5f;
                    continue;
                }
                if (!crosses || !fires) continue;
                if (track.kind == TimelineTrackKind::Sound) world.playSound(mark.text, mark.number);
                else if (track.kind == TimelineTrackKind::Value) world.setValue(track.target, mark.number);
                else if (track.kind == TimelineTrackKind::Trigger) {
                    if (Animator* animator = scene.get<Animator>(world.find(track.target))) animator->setTrigger(mark.text);
                }
            }
            break;
        }
        }
    }
}

void TimelinePlayer::seek(World& world, float to) {
    if (loadedFrom != timeline) load(world);
    time = 0;
    advanceTo(world, std::clamp(to, 0.0f, loaded.length), false);
}

void TimelinePlayer::update(World& world, Entity, float dt) {
    if (loadedFrom != timeline) load(world);
    if (!isPlaying || hasEnded) return;
    float to = time + dt * speed;
    if (to >= loaded.length) {
        advanceTo(world, loaded.length, true);
        if (loops) {
            time = 0;
            advanceTo(world, 0, true);
            return;
        }
        hasEnded = true;
        isPlaying = false;
        if (!doneValue.empty()) world.setValue(doneValue, 1);
        return;
    }
    advanceTo(world, to, true);
}

}  // namespace molten

using namespace molten;

MOLTEN_COMPONENT(TimelinePlayer, MOLTEN_FIELD(timeline).help("The file in Assets: Timelines/Intro.timeline"), MOLTEN_FIELD(playsOnStart),
                 MOLTEN_FIELD(loops), MOLTEN_FIELD(speed).range(0, 4),
                 MOLTEN_FIELD(doneValue).help("A shared value set to 1 when it ends: what a menu or a script waits for"))
