// TimelinePanel.cpp
// Window > Timeline: the tracks of the Timeline Player chosen (or the scene's first), along
// time. Drag the time to see the scene as it will be then; Preview plays it; Stop puts
// everything back where it was. Keys and marks are dragged along their tracks; a right click
// on one changes or removes it. Every change is written to the .timeline file at once.
#include "Editor.hpp"
#include "Animation/Timeline.hpp"
#include "Core/TextFile.hpp"
#include "Panels/Widgets.hpp"
#include "Panels/Pickers.hpp"
#include "Animation/AnimationClip.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace molten::editor {

namespace {

ImU32 colorOf(TimelineTrackKind kind) {
    switch (kind) {
    case TimelineTrackKind::Move: return IM_COL32(255, 140, 60, 255);
    case TimelineTrackKind::Camera: return IM_COL32(110, 200, 255, 255);
    case TimelineTrackKind::Sound: return IM_COL32(150, 230, 120, 255);
    case TimelineTrackKind::Value: return IM_COL32(230, 210, 100, 255);
    case TimelineTrackKind::Show: return IM_COL32(200, 160, 255, 255);
    case TimelineTrackKind::Trigger: return IM_COL32(255, 120, 170, 255);
    case TimelineTrackKind::Clip: return IM_COL32(255, 175, 90, 255);
    }
    return IM_COL32(255, 255, 255, 255);
}

const char* labelOf(TimelineTrackKind kind) {
    switch (kind) {
    case TimelineTrackKind::Move: return "Move";
    case TimelineTrackKind::Camera: return "Camera cuts";
    case TimelineTrackKind::Sound: return "Sounds";
    case TimelineTrackKind::Value: return "Set value";
    case TimelineTrackKind::Show: return "Show";
    case TimelineTrackKind::Trigger: return "Animator trigger";
    case TimelineTrackKind::Clip: return "Clip";
    }
    return "";
}

}  // namespace

class TimelinePanel final : public Panel {
public:
    TimelinePanel() {
        title = "Timeline";
        isOpen = false;                 // Window > Timeline
    }

    void draw(EditorContext& editor) override {
        Scene& scene = editor.engine.scene;
        Entity owner = playerEntity(editor);
        TimelinePlayer* player = scene.get<TimelinePlayer>(owner);
        if (!player || editor.scene.isPlaying()) {
            restore(editor);
            ImGui::TextDisabled(editor.scene.isPlaying() ? "The game is running: the timeline plays in it."
                                                         : "Add a Timeline Player to an entity (Add Component > Animation) to make a cutscene.");
            return;
        }
        if (player->timeline.empty()) {
            if (ImGui::Button("New Timeline")) {
                const Name* name = scene.get<Name>(owner);
                std::string path = editor.assets.uniquePath("Timelines", name ? name->value : std::string("Timeline"), ".timeline");
                Timeline fresh;
                text::writeFile(editor.assets.absolutePath(path), fresh.toText());
                editor.assets.refresh();
                player->timeline = path;
                editor.scene.markChanged();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("or name a .timeline file in its Timeline field");
            return;
        }
        if (editing_ != player->timeline || owner != owner_) {
            restore(editor);
            owner_ = owner;
            editing_ = player->timeline;
            player->load(editor.engine);
            timeline_ = player->loaded;
            cursor_ = 0;
        }

        drawBar(editor, *player);
        drawTracks(editor, *player);
        if (isPreviewing_) {
            cursor_ += ImGui::GetIO().DeltaTime * std::max(player->speed, 0.01f);
            if (cursor_ >= timeline_.length) {
                cursor_ = timeline_.length;
                isPreviewing_ = false;
            }
            show(editor, *player);
        }
    }

private:
    /// The Timeline Player chosen, or the scene's first.
    Entity playerEntity(EditorContext& editor) {
        Scene& scene = editor.engine.scene;
        for (Entity chosen : editor.scene.selection()) {
            if (scene.has<TimelinePlayer>(chosen)) return chosen;
        }
        if (scene.has<TimelinePlayer>(owner_)) return owner_;
        Entity first;
        scene.each<TimelinePlayer>([&](Entity entity, TimelinePlayer&) {
            if (!first.isValid()) first = entity;
        });
        return first;
    }

    // ---------------------------------------------------------------- the scene as it is at the cursor

    /// What a preview moves, as it was before: put back by restore().
    struct Before {
        Entity entity;
        Vec3 position;
        Quat rotation;
        bool isActive;
        bool hasCamera = false, isCameraActive = false;
    };

    struct BeforeProperty {
        Entity entity;
        std::string component, field;
        std::array<float, 4> value;
    };
    std::vector<BeforeProperty> savedProperties_;

    void remember(EditorContext& editor) {
        if (!saved_.empty()) return;
        Scene& scene = editor.engine.scene;
        auto keep = [&](Entity entity) {
            const Transform* transform = scene.get<Transform>(entity);
            if (!transform) return;
            for (const Before& was : saved_) {
                if (was.entity == entity) return;
            }
            Before was{entity, transform->position, transform->rotation, transform->isActive};
            if (const Camera* camera = scene.get<Camera>(entity)) {
                was.hasCamera = true;
                was.isCameraActive = camera->isActive;
            }
            saved_.push_back(was);
        };
        for (const TimelineTrack& track : timeline_.tracks) {
            if (!track.target.empty()) keep(editor.engine.find(track.target));
            for (const TimelineMark& mark : track.marks) {
                if (track.kind == TimelineTrackKind::Camera) keep(editor.engine.find(mark.text));
                // A clip: every property it animates, on the entities it animates.
                if (track.kind != TimelineTrackKind::Clip || !isAnimationClipFile(mark.text)) continue;
                std::shared_ptr<const AnimationClip> clip = sharedAnimationClip(editor.engine.assetPath(mark.text));
                Entity root = editor.engine.find(track.target);
                if (!clip || !root.isValid()) continue;
                for (const AnimationTrack& animated : clip->tracks) {
                    Entity entity = entityAtPath(scene, root, animated.path);
                    BeforeProperty was{entity, animated.component, animated.field, {}};
                    if (entity.isValid() && readProperty(scene, entity, animated.component, animated.field, was.value)) savedProperties_.push_back(was);
                }
            }
        }
    }

    void restore(EditorContext& editor) {
        Scene& scene = editor.engine.scene;
        for (const Before& was : saved_) {
            Transform* transform = scene.get<Transform>(was.entity);
            if (!transform) continue;
            transform->position = was.position;
            transform->rotation = was.rotation;
            transform->isActive = was.isActive;
            if (Camera* camera = scene.get<Camera>(was.entity); camera && was.hasCamera) camera->isActive = was.isCameraActive;
        }
        // Last first, so that a property kept twice ends as it was first.
        for (auto it = savedProperties_.rbegin(); it != savedProperties_.rend(); ++it) writeProperty(scene, it->entity, it->component, it->field, it->value);
        saved_.clear();
        savedProperties_.clear();
        isPreviewing_ = false;
    }

    void show(EditorContext& editor, TimelinePlayer& player) {
        remember(editor);
        player.loaded = timeline_;
        player.loadedFrom = player.timeline;
        player.seek(editor.engine, cursor_);
        editor.engine.scene.updateTransforms();
    }

    void save(EditorContext& editor, TimelinePlayer& player) {
        for (TimelineTrack& track : timeline_.tracks) track.sort();
        if (!text::writeFile(editor.assets.absolutePath(player.timeline), timeline_.toText())) {
            editor.notify("Cannot write " + player.timeline, ConsoleMessage::Level::Error);
        }
        player.loaded = timeline_;
        player.loadedFrom = player.timeline;
    }

    // ---------------------------------------------------------------- the bar

    void drawBar(EditorContext& editor, TimelinePlayer& player) {
        if (ImGui::Button(isPreviewing_ ? "Pause" : "Preview")) {
            if (!isPreviewing_ && cursor_ >= timeline_.length) cursor_ = 0;
            isPreviewing_ = !isPreviewing_;
        }
        ui::tooltip("Plays it here, without its sounds and values");
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
            cursor_ = 0;
            restore(editor);
        }
        ui::tooltip("Everything back where it was before the preview");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        if (ImGui::DragFloat("##time", &cursor_, 0.02f, 0.0f, timeline_.length, "%.2f s")) show(editor, player);
        ImGui::SameLine();
        ImGui::TextDisabled("of");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        if (ImGui::DragFloat("##length", &timeline_.length, 0.05f, 0.1f, 3600.0f, "%.1f s")) {
            timeline_.length = std::max(timeline_.length, 0.1f);
            save(editor, player);
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Track")) ImGui::OpenPopup("add track");
        if (ImGui::BeginPopup("add track")) {
            Scene& scene = editor.engine.scene;
            Entity chosen = editor.scene.selection().empty() ? Entity{} : editor.scene.selection().front();
            const Name* chosenName = scene.get<Name>(chosen);
            std::string target = chosenName ? chosenName->value : std::string();
            auto add = [&](TimelineTrackKind kind, const std::string& name) {
                TimelineTrack track;
                track.kind = kind;
                track.target = name;
                timeline_.tracks.push_back(track);
                save(editor, player);
            };
            if (ImGui::MenuItem(("Move " + (target.empty() ? std::string("(choose an entity first)") : target)).c_str(), nullptr, false, !target.empty())) {
                add(TimelineTrackKind::Move, target);
            }
            if (ImGui::MenuItem(("Show or hide " + (target.empty() ? std::string("(choose an entity first)") : target)).c_str(), nullptr, false, !target.empty())) {
                add(TimelineTrackKind::Show, target);
            }
            if (ImGui::MenuItem(("Clips on " + (target.empty() ? std::string("(choose an entity first)") : target)).c_str(), nullptr, false, !target.empty())) {
                add(TimelineTrackKind::Clip, target);
            }
            ui::tooltip("Animation clips played on it from their marks: made in the Animation window, or its model's");
            if (ImGui::MenuItem(("Animator triggers of " + (target.empty() ? std::string("(choose an entity first)") : target)).c_str(), nullptr, false, !target.empty())) {
                add(TimelineTrackKind::Trigger, target);
            }
            ui::tooltip("Triggers of its Animator Controller, set as the time passes them");
            if (ImGui::MenuItem("Camera cuts")) add(TimelineTrackKind::Camera, "");
            if (ImGui::MenuItem("Sounds")) add(TimelineTrackKind::Sound, "");
            if (ImGui::MenuItem("Set a shared value")) add(TimelineTrackKind::Value, "cutsceneStep");
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", player.timeline.c_str());
    }

    // ---------------------------------------------------------------- the tracks

    void drawTracks(EditorContext& editor, TimelinePlayer& player) {
        const float labelWidth = 230, rowHeight = 30, rulerHeight = 22;
        ImVec2 origin = ImGui::GetCursorScreenPos();
        float width = std::max(ImGui::GetContentRegionAvail().x - labelWidth - 24, 100.0f);
        float laneLeft = origin.x + labelWidth + 10;                     // room for a diamond at 0
        float scale = width / std::max(timeline_.length, 0.1f);          // window points per second
        auto xOf = [&](float time) { return laneLeft + time * scale; };
        auto timeOf = [&](float x) { return std::clamp((x - laneLeft) / scale, 0.0f, timeline_.length); };
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImGuiIO& io = ImGui::GetIO();

        // The ruler: a mark each second (or each five, or ten, as there is room), numbered.
        float every = scale > 60 ? 1 : scale > 12 ? 5 : 10;
        for (float t = 0; t <= timeline_.length + 1e-3f; t += every) {
            drawList->AddLine(ImVec2(xOf(t), origin.y + rulerHeight - 6), ImVec2(xOf(t), origin.y + rulerHeight), IM_COL32(255, 255, 255, 90));
            char label[16];
            std::snprintf(label, sizeof(label), "%gs", double(t));
            drawList->AddText(ImVec2(xOf(t) + 3, origin.y + 2), IM_COL32(255, 255, 255, 110), label);
        }
        // Clicked or dragged on the ruler: the time goes there.
        ImGui::SetCursorScreenPos(ImVec2(laneLeft, origin.y));
        ImGui::InvisibleButton("##ruler", ImVec2(width, rulerHeight));
        if (ImGui::IsItemActive()) {
            cursor_ = timeOf(io.MousePos.x);
            isPreviewing_ = false;
            show(editor, player);
        }

        int removed = -1;
        for (size_t row = 0; row < timeline_.tracks.size(); row++) {
            TimelineTrack& track = timeline_.tracks[row];
            float top = origin.y + rulerHeight + float(row) * rowHeight;
            ImGui::PushID(int(row));
            drawList->AddRectFilled(ImVec2(laneLeft - 10, top + 3), ImVec2(laneLeft + width + 10, top + rowHeight - 3), IM_COL32(255, 255, 255, row % 2 ? 10 : 18), 4);
            // The label, and what can be done to the track.
            ImGui::SetCursorScreenPos(ImVec2(origin.x + 4, top + 5));
            drawList->AddRectFilled(ImVec2(origin.x, top + 8), ImVec2(origin.x + 3, top + rowHeight - 8), colorOf(track.kind));
            std::string label = std::string(labelOf(track.kind)) + (track.target.empty() ? "" : "  " + track.target);
            ImGui::TextUnformatted(label.c_str());
            ImGui::SetCursorScreenPos(ImVec2(laneLeft - 86, top + 4));
            if (ImGui::SmallButton(track.kind == TimelineTrackKind::Move ? "Key" : "Add")) addAtCursor(editor, player, track);
            ui::tooltip(track.kind == TimelineTrackKind::Move ? "Where it is now, kept at this time" : "Something that happens at this time");
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) removed = int(row);
            ui::tooltip("Remove the track");

            // Its keys or marks: diamonds, dragged along the track; a right click changes or removes one.
            size_t count = track.kind == TimelineTrackKind::Move ? track.keys.size() : track.marks.size();
            for (size_t i = 0; i < count; i++) {
                float& time = track.kind == TimelineTrackKind::Move ? track.keys[i].time : track.marks[i].time;
                ImVec2 middle(xOf(time), top + rowHeight * 0.5f);
                ImGui::PushID(int(i));
                ImGui::SetCursorScreenPos(ImVec2(middle.x - 7, middle.y - 9));
                ImGui::InvisibleButton("##point", ImVec2(14, 18));
                bool isHovered = ImGui::IsItemHovered(), isActive = ImGui::IsItemActive();
                if (isActive && io.MouseDelta.x != 0) {
                    time = timeOf(io.MousePos.x);
                    dragged_ = true;
                }
                if (ImGui::IsItemDeactivated() && dragged_) {
                    dragged_ = false;
                    save(editor, player);
                    show(editor, player);
                }
                float r = isHovered || isActive ? 8.0f : 6.5f;
                ImVec2 diamond[4] = {ImVec2(middle.x, middle.y - r), ImVec2(middle.x + r, middle.y), ImVec2(middle.x, middle.y + r), ImVec2(middle.x - r, middle.y)};
                drawList->AddConvexPolyFilled(diamond, 4, colorOf(track.kind));
                if (track.kind != TimelineTrackKind::Move && i < track.marks.size()) {
                    const TimelineMark& mark = track.marks[i];
                    char text[96];
                    if (track.kind == TimelineTrackKind::Value || track.kind == TimelineTrackKind::Show) std::snprintf(text, sizeof(text), "%g", double(mark.number));
                    else std::snprintf(text, sizeof(text), "%s", std::filesystem::path(mark.text).filename().string().c_str());
                    drawList->AddText(ImVec2(middle.x + 9, middle.y - 8), IM_COL32(255, 255, 255, 170), text);
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("point");
                if (ImGui::BeginPopup("point")) {
                    drawPointMenu(editor, player, track, i);
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        if (removed >= 0) {
            timeline_.tracks.erase(timeline_.tracks.begin() + removed);
            save(editor, player);
        }
        // The time, across every track.
        float bottom = origin.y + rulerHeight + float(timeline_.tracks.size()) * rowHeight;
        drawList->AddLine(ImVec2(xOf(cursor_), origin.y), ImVec2(xOf(cursor_), bottom), IM_COL32(255, 122, 47, 255), 2);
        ImGui::SetCursorScreenPos(ImVec2(origin.x, bottom + 8));
        if (timeline_.tracks.empty()) ImGui::TextDisabled("Add a track: choose an entity in the scene, then Add Track > Move it.");
        else ImGui::Dummy(ImVec2(1, 1));               // the window knows how far down the tracks go
    }

    void addAtCursor(EditorContext& editor, TimelinePlayer& player, TimelineTrack& track) {
        Scene& scene = editor.engine.scene;
        if (track.kind == TimelineTrackKind::Move) {
            const Transform* transform = scene.get<Transform>(editor.engine.find(track.target));
            if (!transform) {
                editor.notify("There is no " + track.target + " in the scene", ConsoleMessage::Level::Warning);
                return;
            }
            Vec3 position, scale;
            Quat rotation;
            decompose(transform->world, position, rotation, scale);
            // A key at the same time is replaced.
            track.keys.erase(std::remove_if(track.keys.begin(), track.keys.end(), [&](const TimelineKey& key) { return std::abs(key.time - cursor_) < 1e-3f; }),
                             track.keys.end());
            track.keys.push_back(TimelineKey{cursor_, position, rotation});
        } else {
            TimelineMark mark;
            mark.time = cursor_;
            if (track.kind == TimelineTrackKind::Camera) {
                // The camera chosen, or the scene's first.
                for (Entity chosen : editor.scene.selection()) {
                    if (scene.has<Camera>(chosen) && scene.get<Name>(chosen)) mark.text = scene.get<Name>(chosen)->value;
                }
                if (mark.text.empty()) {
                    scene.each<Camera>([&](Entity entity, Camera&) {
                        if (mark.text.empty() && scene.get<Name>(entity)) mark.text = scene.get<Name>(entity)->value;
                    });
                }
            } else if (track.kind == TimelineTrackKind::Sound) {
                mark.text = editor.selectedAsset;
                if (mark.text.empty()) {
                    for (const AssetInfo& asset : editor.assets.assets()) {
                        if (asset.type == AssetType::Audio && mark.text.empty()) mark.text = asset.path;
                    }
                }
            } else if (track.kind == TimelineTrackKind::Trigger) {
                std::vector<std::string> triggers = choicesFor(editor, editor.engine.find(track.target), "parameter");
                mark.text = triggers.empty() ? std::string() : triggers.front();
            } else if (track.kind == TimelineTrackKind::Clip) {
                std::vector<std::string> clips = choicesFor(editor, editor.engine.find(track.target), "animation");
                mark.text = isAnimationClipFile(editor.selectedAsset) ? editor.selectedAsset : clips.empty() ? std::string() : clips.front();
            } else if (track.kind == TimelineTrackKind::Show) {
                mark.number = track.marks.empty() ? 1.0f : 1.0f - track.marks.back().number;      // shown, then hidden...
            }
            track.marks.push_back(mark);
        }
        save(editor, player);
        show(editor, player);
    }

    void drawPointMenu(EditorContext& editor, TimelinePlayer& player, TimelineTrack& track, size_t i) {
        bool changed = false;
        if (track.kind == TimelineTrackKind::Move) {
            if (ImGui::MenuItem("Where it is now")) {
                if (const Transform* transform = editor.engine.scene.get<Transform>(editor.engine.find(track.target))) {
                    Vec3 scale;
                    decompose(transform->world, track.keys[i].position, track.keys[i].rotation, scale);
                    changed = true;
                }
            }
            if (ImGui::MenuItem("Go to its time")) {
                cursor_ = track.keys[i].time;
                show(editor, player);
            }
            if (ImGui::MenuItem("Remove")) {
                track.keys.erase(track.keys.begin() + long(i));
                changed = true;
            }
        } else {
            TimelineMark& mark = track.marks[i];
            ImGui::SetNextItemWidth(220);
            if (track.kind == TimelineTrackKind::Camera || track.kind == TimelineTrackKind::Sound || track.kind == TimelineTrackKind::Trigger ||
                track.kind == TimelineTrackKind::Clip) {
                // A camera of the scene, a sound of the project, a trigger of the track's Animator, a clip: from lists.
                const char* what = track.kind == TimelineTrackKind::Camera ? "Camera" : track.kind == TimelineTrackKind::Sound ? "Sound" :
                                   track.kind == TimelineTrackKind::Clip ? "Clip" : "Trigger";
                const char* kind = track.kind == TimelineTrackKind::Camera ? "entity" : track.kind == TimelineTrackKind::Sound ? "sound" :
                                   track.kind == TimelineTrackKind::Clip ? "animation" : "parameter";
                ImGui::TextUnformatted(what);
                ImGui::SameLine();
                if (pickValueInline(editor, editor.engine.find(track.target), kind, mark.text, 220)) changed = true;
            }
            if (track.kind != TimelineTrackKind::Camera && track.kind != TimelineTrackKind::Trigger) {
                ImGui::SetNextItemWidth(220);
                const char* what = track.kind == TimelineTrackKind::Sound ? "Volume" : track.kind == TimelineTrackKind::Show ? "Shown (1) or hidden (0)" :
                                   track.kind == TimelineTrackKind::Clip ? "Speed" : "Value";
                if (ImGui::DragFloat(what, &mark.number, 0.05f)) changed = true;
            }
            ImGui::SetNextItemWidth(220);
            if (ImGui::DragFloat("Time", &mark.time, 0.01f, 0.0f, timeline_.length, "%.2f s")) changed = true;
            if (ImGui::MenuItem("Remove")) {
                track.marks.erase(track.marks.begin() + long(i));
                changed = true;
            }
        }
        if (changed) {
            save(editor, player);
            show(editor, player);
        }
    }

    Entity owner_{};
    std::string editing_;
    Timeline timeline_;
    float cursor_ = 0;
    bool isPreviewing_ = false;
    bool dragged_ = false;
    std::vector<Before> saved_;
};

void registerTimelinePanel(EditorContext& editor) {
    editor.registry.addPanel(nullptr, std::make_unique<TimelinePanel>());
}

}  // namespace molten::editor
