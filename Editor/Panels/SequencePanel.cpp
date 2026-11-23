// SequencePanel.cpp
// The Timeline window, as in Unity's Timeline: a cutscene, an intro, a replay, made of tracks along
// time. A Move track takes an entity through its keys; Camera cuts from one camera to another;
// Sound, Value, Show, Trigger and Clip happen at their marks. The file is a .timeline in Assets,
// played in the game by a TimelinePlayer component.
//
// Preview shows the scene as the timeline has it at the playhead, while the time is dragged or
// played; everything it moved, showed or hid is put back when the preview stops. A key is where
// its entity is in the world: move the entity in the view, then press Key on its track.
#include "Editor.hpp"
#include "Panels/Pickers.hpp"
#include "Panels/Widgets.hpp"

#include "Animation/Timeline.hpp"
#include "Core/TextFile.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace molten::editor {

namespace {

const ImU32 kKeyColor = IM_COL32(255, 175, 90, 255);
const ImU32 kMarkColor = IM_COL32(120, 200, 255, 255);
const ImU32 kPlayhead = IM_COL32(255, 122, 47, 255);

/// Each kind of track: its icon, and what a new mark of it starts as.
const char* glyphOf(TimelineTrackKind kind) {
    switch (kind) {
        case TimelineTrackKind::Move: return icon::Move3d;
        case TimelineTrackKind::Camera: return icon::Video;
        case TimelineTrackKind::Sound: return icon::Volume2;
        case TimelineTrackKind::Value: return icon::SlidersHorizontal;
        case TimelineTrackKind::Show: return icon::Eye;
        case TimelineTrackKind::Trigger: return icon::Zap;
        case TimelineTrackKind::Clip: return icon::Film;
    }
    return icon::Circle;
}

const char* aboutKind(TimelineTrackKind kind) {
    switch (kind) {
        case TimelineTrackKind::Move: return "Takes an entity smoothly through its keys: where it is in the world, and which way it faces";
        case TimelineTrackKind::Camera: return "Cuts to a camera at each mark: the one named is switched on, the others the track names off";
        case TimelineTrackKind::Sound: return "Plays a sound at each mark (the number is its volume)";
        case TimelineTrackKind::Value: return "Sets a shared value at each mark: what a script, a visual script or a menu waits for";
        case TimelineTrackKind::Show: return "Shows (1) or hides (0) an entity, and everything under it, from each mark on";
        case TimelineTrackKind::Trigger: return "Sets a trigger of the entity's Animator at each mark";
        case TimelineTrackKind::Clip: return "Plays an animation on the entity from each mark (the number is its speed)";
    }
    return "";
}

/// Whether a kind of track belongs to an entity (the others name a value, or nothing).
bool hasEntity(TimelineTrackKind kind) {
    return kind == TimelineTrackKind::Move || kind == TimelineTrackKind::Show || kind == TimelineTrackKind::Trigger || kind == TimelineTrackKind::Clip;
}

class SequencePanel final : public Panel {
public:
    SequencePanel() {
        title = "Timeline";
        isOpen = false;                 // Window > Timeline, or a .timeline opened from the Explorer
    }

    ImVec2 firstSize() const override { return ImVec2(900, 320); }

    void draw(EditorContext& editor) override {
        if (editor.scene.isPlaying()) {
            stopPreview(editor);
            ImGui::TextDisabled("The game is running: its Timeline Players play their timelines. Stop it to edit them.");
            return;
        }
        // A timeline chosen in the Explorer is the one shown.
        if (editor.selectedAsset != path_ && std::filesystem::path(editor.selectedAsset).extension() == ".timeline") open(editor, editor.selectedAsset);
        drawFileBar(editor);
        if (path_.empty()) {
            ImGui::Spacing();
            ImGui::TextWrapped("A timeline is a cutscene, an intro or a replay: entities moved along keys, cuts between cameras, "
                               "sounds, things shown and hidden, animations started, at their times. Make one with New; a "
                               "Timeline Player component (Add Component > Animation) plays it in the game.");
            return;
        }
        if (!error_.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.35f, 1), "%s", error_.c_str());
            return;
        }
        drawControls(editor);
        drawTracks(editor);
        if (isPlaying_) {
            cursor_ += ImGui::GetIO().DeltaTime;
            if (cursor_ >= timeline_.length) {
                cursor_ = timeline_.length;
                isPlaying_ = false;
            }
            showAt(editor);
        }
    }

private:
    struct Before {
        Entity entity;
        Vec3 position, scale;
        Quat rotation;
        bool isActive = true;
    };

    // ---------------------------------------------------------------- the file

    void open(EditorContext& editor, const std::string& path) {
        stopPreview(editor);
        path_ = path;
        error_.clear();
        selected_ = {-1, -1};
        cursor_ = 0;
        std::optional<std::string> text = text::readFile(editor.assets.absolutePath(path));
        std::optional<Timeline> read = text ? Timeline::parse(*text, &error_) : std::nullopt;
        if (!text) error_ = "Cannot read Assets/" + path;
        timeline_ = read ? *read : Timeline{};
    }

    void save(EditorContext& editor) {
        for (TimelineTrack& track : timeline_.tracks) track.sort();
        if (!text::writeFile(editor.assets.absolutePath(path_), timeline_.toText())) {
            editor.notify("Cannot write Assets/" + path_, ConsoleMessage::Level::Error);
            return;
        }
        if (isPreviewing_) showAt(editor);
    }

    void drawFileBar(EditorContext& editor) {
        if (ImGui::Button("+ New")) {
            std::string path = editor.assets.uniquePath("Timelines", "New Timeline", ".timeline");
            std::error_code ignored;
            std::filesystem::create_directories(std::filesystem::path(editor.assets.absolutePath(path)).parent_path(), ignored);
            if (text::writeFile(editor.assets.absolutePath(path), Timeline{}.toText())) {
                editor.assets.refresh();
                editor.selectedAsset = path;
                open(editor, path);
            }
        }
        ui::tooltip("A new timeline in Assets/Timelines");
        ImGui::SameLine();
        std::string chosen = path_;
        if (pickFromList("##timeline", chosen, choicesFor(editor, Entity{}, "timeline"), false, 260) && chosen != path_) {
            editor.selectedAsset = chosen;
            open(editor, chosen);
        }
    }

    // ---------------------------------------------------------------- the controls above the tracks

    void drawControls(EditorContext& editor) {
        ImGui::SameLine(0, 16);
        ImGui::SetNextItemWidth(90);
        if (ImGui::DragFloat("##length", &timeline_.length, 0.05f, 0.1f, 600.0f, "%.2f s long", ImGuiSliderFlags_AlwaysClamp)) {
            cursor_ = std::min(cursor_, timeline_.length);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) save(editor);
        ui::tooltip("How long the timeline lasts: where it ends, and a Timeline Player sets its Done Value");
        ImGui::SameLine(0, 16);
        // Preview: the scene as the timeline has it, put back when it stops.
        if (ImGui::Button(isPreviewing_ ? (std::string(icon::EyeOff) + "  Stop Preview").c_str() : (std::string(icon::Eye) + "  Preview").c_str())) {
            if (isPreviewing_) stopPreview(editor);
            else showAt(editor);
        }
        ui::tooltip("Shows the scene as the timeline has it at the playhead; everything is put back when the preview stops");
        ImGui::SameLine();
        if (ImGui::Button(isPlaying_ ? (std::string(icon::Pause) + "  Pause").c_str() : (std::string(icon::Play) + "  Play").c_str())) {
            isPlaying_ = !isPlaying_;
            if (isPlaying_ && cursor_ >= timeline_.length - 1e-3f) cursor_ = 0;
        }
        ui::tooltip("Plays the preview from the playhead (sounds and values happen only in the game)");
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%.2f s", double(cursor_));
        ImGui::SameLine(0, 16);
        if (ImGui::Button("+ Track")) ImGui::OpenPopup("##add-track");
        if (ImGui::BeginPopup("##add-track")) {
            const TimelineTrackKind kinds[] = {TimelineTrackKind::Move, TimelineTrackKind::Camera, TimelineTrackKind::Sound, TimelineTrackKind::Value,
                                               TimelineTrackKind::Show, TimelineTrackKind::Trigger, TimelineTrackKind::Clip};
            for (TimelineTrackKind kind : kinds) {
                std::string label = std::string(glyphOf(kind)) + "  " + kindLabel(kind);
                if (ImGui::Selectable(label.c_str())) {
                    TimelineTrack track;
                    track.kind = kind;
                    // For the entity chosen in the scene, when the track is an entity's.
                    if (hasEntity(kind) && !editor.scene.selection().empty()) track.target = editor.scene.nameOf(editor.scene.selection().back());
                    timeline_.tracks.push_back(track);
                    save(editor);
                }
                ui::tooltip(aboutKind(kind));
            }
            ImGui::EndPopup();
        }
    }

    static const char* kindLabel(TimelineTrackKind kind) {
        switch (kind) {
            case TimelineTrackKind::Move: return "Move";
            case TimelineTrackKind::Camera: return "Camera";
            case TimelineTrackKind::Sound: return "Sound";
            case TimelineTrackKind::Value: return "Value";
            case TimelineTrackKind::Show: return "Show";
            case TimelineTrackKind::Trigger: return "Trigger";
            case TimelineTrackKind::Clip: return "Clip";
        }
        return "";
    }

    // ---------------------------------------------------------------- the tracks

    void drawTracks(EditorContext& editor) {
        const float labelWidth = 300, rowHeight = 28, rulerHeight = 22;
        ImGui::Separator();
        ImVec2 origin = ImGui::GetCursorScreenPos();
        const float width = std::max(ImGui::GetContentRegionAvail().x - labelWidth - 24, 100.0f);
        const float laneLeft = origin.x + labelWidth + 10;
        const float length = std::max(timeline_.length, 0.05f);
        const float scale = width / length;
        auto xOf = [&](float time) { return laneLeft + time * scale; };
        auto timeOf = [&](float x) {
            float t = std::clamp((x - laneLeft) / scale, 0.0f, length);
            return ImGui::GetIO().KeyShift ? t : std::round(t * 20.0f) / 20.0f;        // to twentieths of a second, unless Shift
        };
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImGuiIO& io = ImGui::GetIO();

        // The ruler: seconds, and its marks as far apart as the width allows.
        const float every = scale > 400 ? 0.1f : scale > 120 ? 0.25f : scale > 50 ? 0.5f : scale > 12 ? 1.0f : scale > 3 ? 5.0f : 10.0f;
        for (float t = 0; t <= length + 1e-3f; t += every) {
            drawList->AddLine(ImVec2(xOf(t), origin.y + rulerHeight - 6), ImVec2(xOf(t), origin.y + rulerHeight), IM_COL32(255, 255, 255, 90));
            char label[16];
            std::snprintf(label, sizeof(label), "%g", double(std::round(t * 100) / 100));
            drawList->AddText(ImVec2(xOf(t) + 3, origin.y + 2), IM_COL32(255, 255, 255, 110), label);
        }
        ImGui::SetCursorScreenPos(ImVec2(laneLeft, origin.y));
        ImGui::InvisibleButton("##ruler", ImVec2(width, rulerHeight));
        if (ImGui::IsItemActive()) {
            cursor_ = timeOf(io.MousePos.x);
            isPlaying_ = false;
            showAt(editor);
        }
        ui::tooltip("Drag to move the playhead: with Preview on, the scene follows");

        float top = origin.y + rulerHeight;
        int removedTrack = -1;
        for (size_t row = 0; row < timeline_.tracks.size(); row++) {
            TimelineTrack& track = timeline_.tracks[row];
            ImGui::PushID(int(row));
            drawList->AddRectFilled(ImVec2(laneLeft - 10, top + 2), ImVec2(laneLeft + width + 10, top + rowHeight - 2), IM_COL32(255, 255, 255, row % 2 ? 10 : 18), 4);

            // The label: what kind, for what, and its buttons.
            const float middle = top + rowHeight * 0.5f;
            if (ImFont* icons = ImGuiLayer::iconFont()) {
                ImVec2 extent = icons->CalcTextSizeA(15, FLT_MAX, 0, glyphOf(track.kind));
                drawList->AddText(icons, 15, ImVec2(origin.x + 12 - extent.x * 0.5f, middle - extent.y * 0.5f), kKeyColor, glyphOf(track.kind));
            }
            drawList->AddText(ImVec2(origin.x + 26, middle - ImGui::GetTextLineHeight() * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), kindLabel(track.kind));
            ImGui::SetCursorScreenPos(ImVec2(origin.x + 86, top + 2));
            const float pickWidth = track.kind == TimelineTrackKind::Move ? labelWidth - 86 - 72 : labelWidth - 86 - 30;
            if (hasEntity(track.kind)) {
                if (pickFromList("##target", track.target, choicesFor(editor, Entity{}, "entity"), false, pickWidth)) save(editor);
            } else if (track.kind == TimelineTrackKind::Value) {
                if (pickFromList("##value", track.target, choicesFor(editor, Entity{}, "value"), true, pickWidth)) save(editor);
            } else {
                ImGui::SetCursorScreenPos(ImVec2(origin.x + 86, middle - ImGui::GetTextLineHeight() * 0.5f));
                ImGui::TextDisabled("%s", track.kind == TimelineTrackKind::Camera ? "cuts between cameras" : "plays sounds");
            }
            ImGui::SetCursorScreenPos(ImVec2(origin.x + labelWidth - (track.kind == TimelineTrackKind::Move ? 68 : 26), top + 3));
            if (track.kind == TimelineTrackKind::Move) {
                if (ImGui::SmallButton("Key")) keyAt(editor, track, cursor_);
                ui::tooltip("A key at the playhead: where the entity is in the world now, and which way it faces");
                ImGui::SameLine(0, 4);
            }
            if (ImGui::SmallButton("x")) removedTrack = int(row);
            ui::tooltip("Remove the track");
            if (track.target.empty() && hasEntity(track.kind)) {
                drawList->AddText(ImVec2(laneLeft + 6, middle - ImGui::GetTextLineHeight() * 0.5f), IM_COL32(236, 178, 72, 220), "Choose the entity at the left");
            }

            // The lane: double-click for a key or a mark there.
            ImGui::SetCursorScreenPos(ImVec2(laneLeft, top + 2));
            ImGui::InvisibleButton("##lane", ImVec2(width, rowHeight - 4));
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                float at = timeOf(io.MousePos.x);
                if (track.kind == TimelineTrackKind::Move) keyAt(editor, track, at);
                else addMark(editor, track, at);
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                cursor_ = timeOf(io.MousePos.x);
                isPlaying_ = false;
                if (isPreviewing_) showAt(editor);
            }

            // Its keys, or its marks.
            const size_t count = track.kind == TimelineTrackKind::Move ? track.keys.size() : track.marks.size();
            int removed = -1;
            for (size_t i = 0; i < count; i++) {
                float& time = track.kind == TimelineTrackKind::Move ? track.keys[i].time : track.marks[i].time;
                ImGui::PushID(int(i));
                const ImVec2 at(xOf(time), middle);
                ImGui::SetCursorScreenPos(ImVec2(at.x - 6, top + 4));
                ImGui::InvisibleButton("##item", ImVec2(12, rowHeight - 8));
                const bool isChosen = selected_.first == int(row) && selected_.second == int(i);
                if (ImGui::IsItemActivated()) selected_ = {int(row), int(i)};
                if (ImGui::IsItemActive() && io.MouseDelta.x != 0) {
                    time = timeOf(io.MousePos.x);
                    dragged_ = true;
                }
                if (ImGui::IsItemDeactivated() && dragged_) {
                    dragged_ = false;
                    save(editor);
                    selected_ = {-1, -1};       // sorted: the numbers have moved
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right) || (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))) {
                    ImGui::OpenPopup("##item-menu");
                }
                const ImU32 color = track.kind == TimelineTrackKind::Move ? kKeyColor : kMarkColor;
                if (track.kind == TimelineTrackKind::Move) {
                    const float r = isChosen ? 6.5f : 5.5f;
                    ImVec2 diamond[4] = {ImVec2(at.x, at.y - r), ImVec2(at.x + r, at.y), ImVec2(at.x, at.y + r), ImVec2(at.x - r, at.y)};
                    drawList->AddConvexPolyFilled(diamond, 4, color);
                    if (isChosen) drawList->AddPolyline(diamond, 4, IM_COL32(255, 255, 255, 220), ImDrawFlags_Closed, 1.5f);
                } else {
                    drawList->AddRectFilled(ImVec2(at.x - 3, top + 5), ImVec2(at.x + 3, top + rowHeight - 5), color, 2);
                    if (isChosen) drawList->AddRect(ImVec2(at.x - 4, top + 4), ImVec2(at.x + 4, top + rowHeight - 4), IM_COL32(255, 255, 255, 220), 2);
                    const std::string words = markWords(track, track.marks[i]);
                    drawList->AddText(ImVec2(at.x + 7, middle - ImGui::GetTextLineHeight() * 0.5f), IM_COL32(255, 255, 255, 170), words.c_str());
                }
                if (ImGui::BeginPopup("##item-menu")) {
                    if (drawItemMenu(editor, track, i)) save(editor);
                    if (ImGui::MenuItem("Remove")) removed = int(i);
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            if (removed >= 0) {
                if (track.kind == TimelineTrackKind::Move) track.keys.erase(track.keys.begin() + removed);
                else track.marks.erase(track.marks.begin() + removed);
                selected_ = {-1, -1};
                save(editor);
            }
            ImGui::PopID();
            top += rowHeight;
        }
        if (removedTrack >= 0) {
            timeline_.tracks.erase(timeline_.tracks.begin() + removedTrack);
            selected_ = {-1, -1};
            save(editor);
        }
        // Delete removes the key or mark chosen last.
        if (selected_.first >= 0 && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
            (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) && size_t(selected_.first) < timeline_.tracks.size()) {
            TimelineTrack& track = timeline_.tracks[size_t(selected_.first)];
            size_t index = size_t(selected_.second);
            if (track.kind == TimelineTrackKind::Move && index < track.keys.size()) track.keys.erase(track.keys.begin() + long(index));
            else if (track.kind != TimelineTrackKind::Move && index < track.marks.size()) track.marks.erase(track.marks.begin() + long(index));
            selected_ = {-1, -1};
            save(editor);
        }

        // The playhead, over every track.
        drawList->AddLine(ImVec2(xOf(cursor_), origin.y), ImVec2(xOf(cursor_), top), kPlayhead, 2.0f);
        drawList->AddTriangleFilled(ImVec2(xOf(cursor_) - 5, origin.y), ImVec2(xOf(cursor_) + 5, origin.y), ImVec2(xOf(cursor_), origin.y + 7), kPlayhead);
        ImGui::SetCursorScreenPos(ImVec2(origin.x, top + 6));
        if (timeline_.tracks.empty()) ImGui::TextDisabled("No tracks yet: + Track adds one (for the entity chosen in the scene, when it is an entity's).");
        else ImGui::TextDisabled("Double-click a track for a key or a mark there; drag them; right-click one to change it. Shift: off the twentieths.");
    }

    /// The words beside a mark: what it plays, cuts to or sets.
    static std::string markWords(const TimelineTrack& track, const TimelineMark& mark) {
        char number[32];
        std::snprintf(number, sizeof(number), "%g", double(mark.number));
        switch (track.kind) {
            case TimelineTrackKind::Show: return mark.number > 0.5f ? "shown" : "hidden";
            case TimelineTrackKind::Value: return std::string("= ") + number;
            case TimelineTrackKind::Sound: return mark.text.empty() ? "(choose)" : std::filesystem::path(mark.text).filename().string();
            case TimelineTrackKind::Clip: return mark.text.empty() ? "(choose)" : std::filesystem::path(mark.text).stem().string();
            default: return mark.text.empty() ? "(choose)" : mark.text;
        }
    }

    /// What a key or a mark is, to change: in the menu that a right click opens. True if changed.
    bool drawItemMenu(EditorContext& editor, TimelineTrack& track, size_t index) {
        bool changed = false;
        ImGui::TextDisabled("%s", aboutKind(track.kind));
        ImGui::SetNextItemWidth(260);
        float& time = track.kind == TimelineTrackKind::Move ? track.keys[index].time : track.marks[index].time;
        changed |= ImGui::DragFloat("Time", &time, 0.01f, 0.0f, timeline_.length, "%.2f s");
        if (track.kind == TimelineTrackKind::Move) {
            if (ImGui::MenuItem("Set to Where It Is Now")) {
                Entity entity = editor.engine.find(track.target);
                if (const Transform* transform = editor.engine.scene.get<Transform>(entity)) {
                    Vec3 scale;
                    decompose(transform->world, track.keys[index].position, track.keys[index].rotation, scale);
                    changed = true;
                }
            }
            ui::tooltip("The key takes the entity's place in the world as it is now");
            if (ImGui::MenuItem("Go to It")) cursor_ = time;
            return changed;
        }
        TimelineMark& mark = track.marks[index];
        Entity target = editor.engine.find(track.target);
        switch (track.kind) {
            case TimelineTrackKind::Camera: {
                std::vector<std::string> cameras;
                for (Entity entity : editor.scene.entities()) {
                    if (editor.engine.scene.has<Camera>(entity)) cameras.push_back(editor.scene.nameOf(entity));
                }
                changed |= pickFromList("##camera", mark.text, cameras, false, 260);
                break;
            }
            case TimelineTrackKind::Sound:
                changed |= pickFromList("##sound", mark.text, choicesFor(editor, Entity{}, "sound"), false, 260);
                ImGui::SetNextItemWidth(260);
                changed |= ImGui::SliderFloat("Volume", &mark.number, 0.0f, 1.0f, "%.2f");
                break;
            case TimelineTrackKind::Value:
                ImGui::SetNextItemWidth(260);
                changed |= ImGui::DragFloat("Set To", &mark.number, 0.05f);
                break;
            case TimelineTrackKind::Show: {
                bool shown = mark.number > 0.5f;
                if (ImGui::Checkbox("Shown", &shown)) {
                    mark.number = shown ? 1.0f : 0.0f;
                    changed = true;
                }
                break;
            }
            case TimelineTrackKind::Trigger:
                changed |= pickFromList("##trigger", mark.text, choicesFor(editor, target, "parameter"), true, 260);
                break;
            case TimelineTrackKind::Clip: {
                std::vector<std::string> clips = choicesFor(editor, target, "clip");
                for (const std::string& animation : choicesFor(editor, target, "animation")) clips.push_back(animation);
                changed |= pickFromList("##clip", mark.text, clips, false, 260);
                ImGui::SetNextItemWidth(260);
                changed |= ImGui::DragFloat("Speed", &mark.number, 0.01f, 0.0f, 10.0f, "%.2f");
                break;
            }
            default:
                break;
        }
        return changed;
    }

    /// A key at `time` on a Move track: where its entity is in the world now. One already there is replaced.
    void keyAt(EditorContext& editor, TimelineTrack& track, float time) {
        Entity entity = editor.engine.find(track.target);
        const Transform* transform = editor.engine.scene.get<Transform>(entity);
        if (!transform) {
            editor.notify("Choose the entity the track moves first", ConsoleMessage::Level::Warning);
            return;
        }
        TimelineKey key;
        key.time = time;
        Vec3 scale;
        decompose(transform->world, key.position, key.rotation, scale);
        auto same = std::find_if(track.keys.begin(), track.keys.end(), [&](const TimelineKey& k) { return std::abs(k.time - time) < 1e-3f; });
        if (same != track.keys.end()) *same = key;
        else track.keys.push_back(key);
        save(editor);
    }

    /// A mark at `time`, starting as the one before it (a Show mark the opposite of it).
    void addMark(EditorContext& editor, TimelineTrack& track, float time) {
        TimelineMark mark;
        mark.time = time;
        const TimelineMark* before = nullptr;
        for (const TimelineMark& other : track.marks) {
            if (other.time <= time) before = &other;
        }
        if (before) {
            mark.text = before->text;
            mark.number = track.kind == TimelineTrackKind::Show ? (before->number > 0.5f ? 0.0f : 1.0f) : before->number;
        } else if (track.kind == TimelineTrackKind::Camera) {
            for (Entity entity : editor.scene.entities()) {
                if (editor.engine.scene.has<Camera>(entity)) {
                    mark.text = editor.scene.nameOf(entity);
                    break;
                }
            }
        }
        track.marks.push_back(mark);
        save(editor);
    }

    // ---------------------------------------------------------------- preview

    /// The scene as the timeline has it at the playhead; what it can change is kept first, to put back.
    void showAt(EditorContext& editor) {
        Scene& scene = editor.engine.scene;
        if (!isPreviewing_) {
            saved_.clear();
            cameras_.clear();
            scene.each<Transform>([&](Entity entity, Transform& transform) {
                saved_.push_back(Before{entity, transform.position, transform.scale, transform.rotation, transform.isActive});
            });
            scene.each<Camera>([&](Entity entity, Camera& camera) { cameras_.push_back({entity, camera.isActive}); });
            isPreviewing_ = true;
        }
        player_.timeline = player_.loadedFrom = path_;
        player_.loaded = timeline_;
        player_.seek(editor.engine, cursor_);
        scene.updateTransforms();
    }

    void stopPreview(EditorContext& editor) {
        isPlaying_ = false;
        if (!isPreviewing_) return;
        Scene& scene = editor.engine.scene;
        for (const Before& was : saved_) {
            if (Transform* transform = scene.get<Transform>(was.entity)) {
                transform->position = was.position;
                transform->rotation = was.rotation;
                transform->scale = was.scale;
                transform->isActive = was.isActive;
            }
        }
        for (const auto& [entity, isActive] : cameras_) {
            if (Camera* camera = scene.get<Camera>(entity)) camera->isActive = isActive;
        }
        scene.updateTransforms();
        saved_.clear();
        cameras_.clear();
        isPreviewing_ = false;
    }

    std::string path_, error_;
    Timeline timeline_;
    float cursor_ = 0;
    bool isPreviewing_ = false, isPlaying_ = false, dragged_ = false;
    std::pair<int, int> selected_{-1, -1};                  // the track, and the key or mark on it
    TimelinePlayer player_;
    std::vector<Before> saved_;
    std::vector<std::pair<Entity, bool>> cameras_;
};

}  // namespace

void registerSequencePanel(EditorContext& editor) {
    editor.registry.addPanel(nullptr, std::make_unique<SequencePanel>());
}

}  // namespace molten::editor
