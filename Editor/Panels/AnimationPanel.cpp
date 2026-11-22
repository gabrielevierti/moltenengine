// AnimationPanel.cpp
// The Timeline window, as in Unity's Animation window: the animations of the chosen entity (each
// made for it, and for nothing else), their tracks (a property of it, or of an entity under it,
// along time) and their keys. Press Record: where things are is kept at the playhead; click
// another time and change things in the Inspector or move them in the view: each change is a key
// there. Key keeps everything animated as it is now. A track can also move by itself
// (Procedural: a wave, a wander, or a parameter the game sets).
// Drag the playhead to see the clip at that moment; keys are dragged along their track, and a
// right click changes how the curve comes into one, or removes it. Everything is put back as it
// was when the window stops previewing.
#include "Editor.hpp"
#include "Panels/Pickers.hpp"
#include "Panels/Widgets.hpp"

#include "Animation/AnimationClip.hpp"
#include "Animation/Animator.hpp"
#include "Animation/AnimatorController.hpp"
#include "Core/TextFile.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>

namespace molten::editor {

namespace {

const ImU32 kKeyColor = IM_COL32(255, 175, 90, 255);
const ImU32 kRecordColor = IM_COL32(235, 70, 70, 255);
const ImU32 kEventColor = IM_COL32(120, 200, 255, 255);

class AnimationPanel final : public Panel {
public:
    AnimationPanel() {
        title = "Timeline";
        isOpen = false;                 // Window > Timeline, or Open Timeline on an Animator
    }

    void draw(EditorContext& editor) override {
        Scene& scene = editor.engine.scene;
        if (editor.scene.isPlaying()) {
            stopPreview(editor);
            ImGui::TextDisabled("The game is running: its Animators play their animations. Stop it to edit them.");
            return;
        }
        Entity root = rootOf(editor);
        if (root != root_) {
            stopPreview(editor);
            root_ = root;
        }
        if (!scene.isAlive(root)) {
            stopPreview(editor);
            ImGui::TextDisabled("Choose an entity in the scene to animate it.");
            return;
        }
        Animator* animator = scene.get<Animator>(root);
        std::vector<std::string> clips = clipsOf(editor, root);
        // A clip chosen in the Explorer is the one shown, if this entity plays it.
        if (isAnimationClipFile(editor.selectedAsset) && std::find(clips.begin(), clips.end(), editor.selectedAsset) != clips.end()) {
            if (editing_ != editor.selectedAsset) open(editor, editor.selectedAsset);
        }
        if (editing_.empty() || std::find(clips.begin(), clips.end(), editing_) == clips.end()) {
            if (!clips.empty()) open(editor, clips.front());
            else {
                close(editor);
            }
        }

        drawBar(editor, root, animator, clips);
        if (editing_.empty()) {
            ImGui::Spacing();
            const Name* name = scene.get<Name>(root);
            ImGui::TextWrapped("%s has no animations yet. Create Animation makes one for it (and the Animator that plays it); then press Record.",
                               name ? name->value.c_str() : "The entity");
            return;
        }
        if (isRecording_) record(editor, root);
        if (isPlaying_) {
            cursor_ += ImGui::GetIO().DeltaTime;
            if (cursor_ > clip_.length) cursor_ = clip_.loops ? std::fmod(cursor_, std::max(clip_.length, 0.01f)) : 0.0f;
            showAt(editor, root);
        }
        drawTracks(editor, root);
    }

private:
    // ---------------------------------------------------------------- which entity, which clips

    /// The chosen entity, or the nearest above it with an Animator: the one whose clips these are.
    Entity rootOf(EditorContext& editor) {
        Scene& scene = editor.engine.scene;
        Entity chosen = editor.scene.primary();
        if (!scene.isAlive(chosen)) return scene.isAlive(root_) && isRecording_ ? root_ : Entity{};
        for (Entity at = chosen; scene.isAlive(at);) {
            if (scene.has<Animator>(at)) return at;
            const Transform* transform = scene.get<Transform>(at);
            at = transform ? transform->parent : Entity{};
        }
        return chosen;
    }

    /// The clips made in the editor that the entity's Animator plays: its own, and its controller's.
    std::vector<std::string> clipsOf(EditorContext& editor, Entity root) {
        std::vector<std::string> out;
        const Animator* animator = editor.engine.scene.get<Animator>(root);
        if (!animator) return out;
        auto add = [&](const std::string& clip) {
            if (isAnimationClipFile(clip) && std::find(out.begin(), out.end(), clip) == out.end()) out.push_back(clip);
        };
        for (const std::string& name : animator->animationNames()) add(animator->resolve(name));
        add(animator->clip);
        if (std::optional<AnimatorController> controller = readController(editor, animator->controller)) {
            for (const auto& state : controller->states) for (const auto& motion : state.motions) add(motion.clip);
            for (const auto& layer : controller->layers) for (const auto& state : layer.states) for (const auto& motion : state.motions) add(motion.clip);
        }
        return out;
    }

    static std::optional<AnimatorController> readController(EditorContext& editor, const std::string& path) {
        if (path.empty()) return std::nullopt;
        std::optional<std::string> text = text::readFile(editor.engine.assetPath(path));
        return text ? AnimatorController::parse(*text) : std::nullopt;
    }

    void open(EditorContext& editor, const std::string& path) {
        stopPreview(editor);
        editing_ = path;
        clip_ = AnimationClip{};
        if (std::optional<std::string> text = text::readFile(editor.engine.assetPath(path))) {
            std::string error;
            if (std::optional<AnimationClip> read = AnimationClip::parse(*text, &error)) clip_ = std::move(*read);
            else editor.notify(path + ": " + error, ConsoleMessage::Level::Error);
        }
        cursor_ = std::min(cursor_, clip_.length);
        selected_ = {-1, -1};
    }

    void close(EditorContext& editor) {
        stopPreview(editor);
        editing_.clear();
        clip_ = AnimationClip{};
    }

    void save(EditorContext& editor) {
        for (AnimationTrack& track : clip_.tracks) track.sort();
        std::stable_sort(clip_.events.begin(), clip_.events.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
        if (!text::writeFile(editor.engine.assetPath(editing_), clip_.toText())) editor.notify("Cannot write " + editing_, ConsoleMessage::Level::Error);
    }

    /// A new animation for the entity: in Animations/<its name>, made for it alone, listed in its
    /// Animator (made if it has none), and the one it plays first if it had none; a state of its
    /// controller too, if it has one.
    void createClip(EditorContext& editor, Entity root, bool procedural = false) {
        Scene& scene = editor.engine.scene;
        std::string owner = editor.scene.nameOf(root);
        std::string stem = newName_.empty() ? std::string(procedural ? "Procedural" : "Animation") : newName_;
        std::string path = editor.assets.uniquePath("Animations/" + owner, stem, ".anim");
        std::filesystem::create_directories(std::filesystem::path(editor.assets.absolutePath(path)).parent_path());
        AnimationClip fresh;
        fresh.length = 1;
        fresh.owner = owner;
        if (procedural) {
            // A gentle bob to start from: change its kind, size and speed by the track's right click.
            AnimationTrack bob;
            bob.component = "Transform";
            bob.field = "position";
            bob.width = 3;
            std::array<float, 4> now{};
            readProperty(scene, root, "Transform", "position", now);
            bob.setKey(0, now);
            bob.procedural.kind = Procedural::Kind::Wave;
            bob.procedural.amplitude = 0.25f;
            bob.procedural.axes = {0, 1, 0, 0};
            fresh.tracks.push_back(bob);
        }
        if (!text::writeFile(editor.assets.absolutePath(path), fresh.toText())) {
            editor.notify("Cannot write " + path, ConsoleMessage::Level::Error);
            return;
        }
        editor.assets.refresh();
        Animator* animator = scene.get<Animator>(root);
        if (!animator) {
            scene.add(root, Animator{});
            animator = scene.get<Animator>(root);
        }
        animator->animations += (animator->animations.empty() ? "" : ", ") + path;
        if (animator->clip.empty()) animator->clip = std::filesystem::path(path).stem().string();
        if (!animator->controller.empty()) {
            if (std::optional<AnimatorController> controller = readController(editor, animator->controller)) {
                AnimatorController::State state;
                state.name = std::filesystem::path(path).stem().string();
                state.motions.push_back({path, 0, 0});
                state.x = 60.0f + 30.0f * float(controller->states.size());
                state.y = 60.0f + 70.0f * float(controller->states.size() % 4);
                controller->states.push_back(state);
                if (controller->start.empty()) controller->start = state.name;
                text::writeFile(editor.engine.assetPath(animator->controller), controller->toText());
            }
        }
        editor.scene.markChanged();
        open(editor, path);
        newName_.clear();
    }

    /// Keys everything animated (or, in a clip with nothing yet, where it is: position, rotation,
    /// scale) at the playhead, as it is now.
    void keyAll(EditorContext& editor, Entity root) {
        Scene& scene = editor.engine.scene;
        if (clip_.tracks.empty()) {
            for (const char* field : {"position", "rotation", "scale"}) {
                AnimationTrack track;
                track.component = "Transform";
                track.field = field;
                track.width = 3;
                clip_.tracks.push_back(track);
            }
        }
        for (AnimationTrack& track : clip_.tracks) {
            Entity entity = entityAtPath(scene, root, track.path);
            std::array<float, 4> now{};
            if (entity.isValid() && readProperty(scene, entity, track.component, track.field, now)) track.setKey(cursor_, now);
        }
        save(editor);
    }

    // ---------------------------------------------------------------- preview: the scene at the playhead

    struct Before {
        Entity entity;
        std::string component, field;
        std::array<float, 4> value;
    };

    void remember(EditorContext& editor, Entity entity, const std::string& component, const std::string& field) {
        for (const Before& was : saved_) {
            if (was.entity == entity && was.component == component && was.field == field) return;
        }
        Before was{entity, component, field, {}};
        if (readProperty(editor.engine.scene, entity, component, field, was.value)) saved_.push_back(was);
    }

    void showAt(EditorContext& editor, Entity root) {
        Scene& scene = editor.engine.scene;
        for (const AnimationTrack& track : clip_.tracks) {
            Entity entity = entityAtPath(scene, root, track.path);
            if (entity.isValid()) remember(editor, entity, track.component, track.field);
        }
        isPreviewing_ = true;
        ClipPose::of(clip_, cursor_).apply(scene, root);
        scene.updateTransforms();
        noteValues(editor, root);
    }

    void stopPreview(EditorContext& editor) {
        Scene& scene = editor.engine.scene;
        for (auto it = saved_.rbegin(); it != saved_.rend(); ++it) {
            if (scene.isAlive(it->entity)) writeProperty(scene, it->entity, it->component, it->field, it->value);
        }
        if (!saved_.empty()) scene.updateTransforms();
        saved_.clear();
        seen_.clear();
        isPreviewing_ = isRecording_ = isPlaying_ = false;
    }

    // ---------------------------------------------------------------- recording

    /// Every animatable value of the entity and those under it, as it is now: what record() compares with.
    void noteValues(EditorContext& editor, Entity root) {
        Scene& scene = editor.engine.scene;
        seen_.clear();
        forEachUnder(editor, root, [&](Entity entity, const std::string& path) {
            for (const AnimatableProperty& property : animatableProperties(scene, entity)) {
                Seen seen{entity, path, property.component, property.field, property.width, {}};
                if (readProperty(scene, entity, property.component, property.field, seen.value)) seen_[path + "\n" + property.component + "\n" + property.field] = seen;
            }
        });
    }

    template <class Visit> void forEachUnder(EditorContext& editor, Entity root, Visit&& visit) {
        Scene& scene = editor.engine.scene;
        int count = 0;
        for (Entity entity : editor.scene.entities()) {
            if (count > 200) break;
            std::optional<std::string> path = pathFromRoot(scene, root, entity);
            if (!path) continue;
            visit(entity, *path);
            count++;
        }
    }

    /// While recording: whatever was changed since the last frame (in the Inspector, with the
    /// gizmos, anywhere) becomes a key at the playhead.
    void record(EditorContext& editor, Entity root) {
        if (seen_.empty()) {
            showAt(editor, root);
            return;
        }
        Scene& scene = editor.engine.scene;
        bool changed = false;
        for (auto& [id, seen] : seen_) {
            if (!scene.isAlive(seen.entity)) continue;
            std::array<float, 4> now{};
            if (!readProperty(scene, seen.entity, seen.component, seen.field, now)) continue;
            bool differs = false;
            for (int i = 0; i < seen.width; i++) differs |= std::abs(now[size_t(i)] - seen.value[size_t(i)]) > 1e-5f;
            if (!differs) continue;
            // Put back as it was before the recording, when the recording stops.
            bool kept = false;
            for (const Before& was : saved_) kept |= was.entity == seen.entity && was.component == seen.component && was.field == seen.field;
            if (!kept) saved_.push_back(Before{seen.entity, seen.component, seen.field, seen.value});
            AnimationTrack* track = clip_.find(seen.path, seen.component, seen.field);
            if (!track) {
                AnimationTrack fresh;
                fresh.path = seen.path;
                fresh.component = seen.component;
                fresh.field = seen.field;
                fresh.width = seen.width;
                // A new track starts from what it was, at the start, so the change is a movement.
                if (cursor_ > 0.01f) fresh.setKey(0, seen.value);
                clip_.tracks.push_back(fresh);
                track = &clip_.tracks.back();
            }
            track->setKey(cursor_, now);
            seen.value = now;
            changed = true;
        }
        if (changed) {
            clip_.length = std::max(clip_.length, cursor_);
            save(editor);
        }
    }

    // ---------------------------------------------------------------- the bar

    void drawBar(EditorContext& editor, Entity root, Animator* animator, const std::vector<std::string>& clips) {
        // Record: red while on.
        if (!editing_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Button, isRecording_ ? ImVec4(0.8f, 0.2f, 0.2f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
            if (ImGui::Button(isRecording_ ? "Recording" : "Record")) {
                isRecording_ = !isRecording_;
                // The first position: where it is now, at the playhead.
                if (isRecording_ && clip_.tracks.empty()) keyAll(editor, root);
                if (isRecording_) showAt(editor, root);
            }
            ImGui::PopStyleColor();
            ui::tooltip("While on, whatever you change on the entity (or those under it) is kept as a key at the playhead: click another time, change it, and so on");
            ImGui::SameLine();
            if (ImGui::Button("Key")) keyAll(editor, root);
            ui::tooltip("Keeps everything animated as it is now, at the playhead");
            ImGui::SameLine();
            if (ImGui::Button(isPlaying_ ? "Pause" : "Play")) {
                isPlaying_ = !isPlaying_;
                if (isPlaying_) showAt(editor, root);
            }
            ImGui::SameLine();
            if (ImGui::Button("Stop")) {
                cursor_ = 0;
                stopPreview(editor);
            }
            ui::tooltip("Everything back as it was before the preview");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80);
            if (ImGui::DragFloat("##time", &cursor_, 0.01f, 0.0f, clip_.length, "%.2f s")) showAt(editor, root);
            ImGui::SameLine();
        }
        // Which clip.
        ImGui::SetNextItemWidth(220);
        std::string shown = editing_.empty() ? std::string("No animation") : std::filesystem::path(editing_).stem().string();
        if (ImGui::BeginCombo("##clip", shown.c_str())) {
            for (const std::string& clip : clips) {
                if (ImGui::Selectable(clip.c_str(), clip == editing_)) open(editor, clip);
            }
            if (!clips.empty()) ImGui::Separator();
            if (ImGui::Selectable("Create Animation...")) wantsNewClip_ = true;
            if (ImGui::Selectable("Create Procedural Animation")) createClip(editor, root, true);
            ImGui::EndCombo();
        }
        if (editing_.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("Create Animation")) wantsNewClip_ = true;
        }
        if (wantsNewClip_) {
            ImGui::OpenPopup("new clip");
            wantsNewClip_ = false;
        }
        if (ImGui::BeginPopup("new clip")) {
            ImGui::TextUnformatted("A new animation for this entity");
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            bool entered = ImGui::InputTextWithHint("##name", "its name: Open, Idle, Flicker", &newName_, ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button("Create") || entered) {
                createClip(editor, root);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (editing_.empty()) return;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        if (ImGui::DragFloat("##length", &clip_.length, 0.02f, 0.05f, 600.0f, "%.2f s long")) {
            clip_.length = std::max(clip_.length, 0.05f);
            save(editor);
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Loops", &clip_.loops)) save(editor);
        ImGui::SameLine();
        if (ImGui::Button("Add Property")) ImGui::OpenPopup("add property");
        if (ImGui::BeginPopup("add property")) {
            drawAddProperty(editor, root);
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130);
        std::vector<std::string> floats = choicesFor(editor, root, "parameter");
        if (pickFromList("timeBy", clip_.timeBy, floats, true, 130)) save(editor);
        ui::tooltip("Time by: a parameter (0 to 1) that says how far through it is, instead of time: a door opened as much as \"open\" says. None: by time");
        if (animator && ImGui::Button("Open Animator")) editor.openPanel("Animator");
    }

    void drawAddProperty(EditorContext& editor, Entity root) {
        Scene& scene = editor.engine.scene;
        forEachUnder(editor, root, [&](Entity entity, const std::string& path) {
            std::string label = path.empty() ? editor.scene.nameOf(entity) + " (itself)" : path;
            if (!ImGui::BeginMenu(label.c_str())) return;
            std::string component;
            for (const AnimatableProperty& property : animatableProperties(scene, entity)) {
                if (property.component != component) {
                    if (!component.empty()) ImGui::Separator();
                    ImGui::TextDisabled("%s", property.component.c_str());
                    component = property.component;
                }
                bool has = clip_.find(path, property.component, property.field) != nullptr;
                if (ImGui::MenuItem(property.field.c_str(), nullptr, has, !has)) {
                    AnimationTrack track;
                    track.path = path;
                    track.component = property.component;
                    track.field = property.field;
                    track.width = property.width;
                    std::array<float, 4> now{};
                    readProperty(scene, entity, property.component, property.field, now);
                    track.setKey(0, now);
                    if (clip_.length > 0.05f) track.setKey(clip_.length, now);
                    clip_.tracks.push_back(track);
                    save(editor);
                }
            }
            ImGui::EndMenu();
        });
    }

    // ---------------------------------------------------------------- the tracks

    void drawTracks(EditorContext& editor, Entity root) {
        Scene& scene = editor.engine.scene;
        const float labelWidth = 300, rowHeight = 26, rulerHeight = 22;
        ImGui::Separator();
        ImVec2 origin = ImGui::GetCursorScreenPos();
        float width = std::max(ImGui::GetContentRegionAvail().x - labelWidth - 24, 100.0f);
        float laneLeft = origin.x + labelWidth + 10;
        float length = std::max(clip_.length, 0.05f);
        float scale = width / length;
        auto xOf = [&](float time) { return laneLeft + time * scale; };
        auto timeOf = [&](float x) {
            float t = std::clamp((x - laneLeft) / scale, 0.0f, length);
            return ImGui::GetIO().KeyShift ? t : std::round(t * 60.0f) / 60.0f;     // to frames, unless Shift
        };
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImGuiIO& io = ImGui::GetIO();

        // The ruler, in seconds and tenths.
        float every = scale > 400 ? 0.1f : scale > 120 ? 0.25f : scale > 50 ? 0.5f : scale > 12 ? 1.0f : 5.0f;
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
            showAt(editor, root);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            eventAt_ = timeOf(io.MousePos.x);
            ImGui::OpenPopup("ruler");
        }
        if (ImGui::BeginPopup("ruler")) {
            if (ImGui::MenuItem("Add Event Here")) {
                clip_.events.push_back(AnimationClip::Event{eventAt_, ""});
                save(editor);
            }
            ui::tooltip("A sound played, or a message sent to the entity (On Message), when the clip passes this time");
            ImGui::EndPopup();
        }

        // The events: a row of their own, under the ruler.
        float top = origin.y + rulerHeight;
        ImGui::SetCursorScreenPos(ImVec2(origin.x + 4, top + 4));
        ImGui::TextDisabled("Events");
        int removedEvent = -1;
        for (size_t i = 0; i < clip_.events.size(); i++) {
            AnimationClip::Event& event = clip_.events[i];
            ImGui::PushID(int(i) + 100000);
            ImVec2 middle(xOf(event.time), top + rowHeight * 0.5f);
            ImGui::SetCursorScreenPos(ImVec2(middle.x - 5, top + 2));
            ImGui::InvisibleButton("##event", ImVec2(10, rowHeight - 4));
            if (ImGui::IsItemActive() && io.MouseDelta.x != 0) {
                event.time = timeOf(io.MousePos.x);
                dragged_ = true;
            }
            if (ImGui::IsItemDeactivated() && dragged_) {
                dragged_ = false;
                save(editor);
            }
            drawList->AddRectFilled(ImVec2(middle.x - 3, top + 4), ImVec2(middle.x + 3, top + rowHeight - 4), kEventColor, 2);
            std::string named = event.name.empty() ? std::string("(choose)") : std::filesystem::path(event.name).filename().string();
            drawList->AddText(ImVec2(middle.x + 6, top + 5), IM_COL32(255, 255, 255, 170), named.c_str());
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right) || (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))) ImGui::OpenPopup("event");
            if (ImGui::BeginPopup("event")) {
                ImGui::TextDisabled("A sound is played; a message is sent to the entity");
                std::vector<std::string> choices = choicesFor(editor, root, "sound");
                for (const std::string& message : choicesFor(editor, root, "message")) choices.push_back(message);
                if (pickFromList("event", event.name, choices, true, 260)) save(editor);
                ImGui::SetNextItemWidth(260);
                if (ImGui::DragFloat("Time", &event.time, 0.01f, 0.0f, clip_.length, "%.2f s")) save(editor);
                if (ImGui::MenuItem("Remove")) removedEvent = int(i);
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (removedEvent >= 0) {
            clip_.events.erase(clip_.events.begin() + removedEvent);
            save(editor);
        }
        top += rowHeight;

        int removedTrack = -1;
        for (size_t row = 0; row < clip_.tracks.size(); row++) {
            AnimationTrack& track = clip_.tracks[row];
            ImGui::PushID(int(row));
            drawList->AddRectFilled(ImVec2(laneLeft - 10, top + 2), ImVec2(laneLeft + width + 10, top + rowHeight - 2), IM_COL32(255, 255, 255, row % 2 ? 10 : 18), 4);
            Entity entity = entityAtPath(scene, root, track.path);
            // The property, and its value at the playhead: changed here, it is a key there.
            ImGui::SetCursorScreenPos(ImVec2(origin.x + 4, top + 4));
            if (!entity.isValid()) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.4f, 1.0f));
            ImGui::TextUnformatted((track.label() + (track.procedural.isOn() ? "  ~" : "")).c_str());
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("procedural");
            if (ImGui::BeginPopup("procedural")) {
                drawProcedural(editor, track);
                ImGui::EndPopup();
            }
            ui::tooltip("Right click: make it move by itself (a wave, a wander, a parameter)");
            if (!entity.isValid()) {
                ImGui::PopStyleColor();
                ui::tooltip("There is no entity at this path under the one animated: renamed, or moved?");
            }
            ImGui::SetCursorScreenPos(ImVec2(origin.x + labelWidth - 140, top + 2));
            std::array<float, 4> value = track.sample(cursor_);
            ImGui::SetNextItemWidth(110);
            bool edited = false;
            if (track.width == 1) edited = ImGui::DragFloat("##value", &value[0], 0.01f);
            else if (track.width == 2) edited = ImGui::DragFloat2("##value", value.data(), 0.01f);
            else if (track.width == 3) edited = ImGui::DragFloat3("##value", value.data(), 0.01f, 0, 0, "%.2f");
            else edited = ImGui::DragFloat4("##value", value.data(), 0.01f, 0, 0, "%.2f");
            if (edited) {
                track.setKey(cursor_, value);
                save(editor);
                showAt(editor, root);
            }
            ui::tooltip("Its value at the playhead: change it to put a key there");
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) removedTrack = int(row);
            ui::tooltip("Remove the track");
            // A double click on the lane: a key there, with the value the curve has there.
            ImGui::SetCursorScreenPos(ImVec2(laneLeft - 10, top + 2));
            ImGui::InvisibleButton("##lane", ImVec2(width + 20, rowHeight - 4));
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                float at = timeOf(io.MousePos.x);
                track.setKey(at, track.sample(at));
                save(editor);
            }
            // The keys: diamonds, dragged along the lane; right click: how it eases in, remove.
            int removedKey = -1;
            for (size_t i = 0; i < track.keys.size(); i++) {
                AnimationKey& key = track.keys[i];
                ImGui::PushID(int(i));
                ImVec2 middle(xOf(key.time), top + rowHeight * 0.5f);
                ImGui::SetCursorScreenPos(ImVec2(middle.x - 7, middle.y - 9));
                ImGui::InvisibleButton("##key", ImVec2(14, 18));
                bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
                if (ImGui::IsItemClicked()) selected_ = {int(row), int(i)};
                if (active && io.MouseDelta.x != 0) {
                    key.time = timeOf(io.MousePos.x);
                    dragged_ = true;
                }
                if (ImGui::IsItemDeactivated() && dragged_) {
                    dragged_ = false;
                    save(editor);
                    if (isPreviewing_) showAt(editor, root);
                }
                bool isSelected = selected_.first == int(row) && selected_.second == int(i);
                float r = hovered || active || isSelected ? 7.5f : 6.0f;
                if (key.ease == AnimationKey::Ease::Step) {
                    drawList->AddRectFilled(ImVec2(middle.x - r * 0.7f, middle.y - r * 0.7f), ImVec2(middle.x + r * 0.7f, middle.y + r * 0.7f), kKeyColor);
                } else {
                    ImVec2 diamond[4] = {ImVec2(middle.x, middle.y - r), ImVec2(middle.x + r, middle.y), ImVec2(middle.x, middle.y + r), ImVec2(middle.x - r, middle.y)};
                    if (key.ease == AnimationKey::Ease::Linear) drawList->AddPolyline(diamond, 4, kKeyColor, ImDrawFlags_Closed, 2.0f);
                    else drawList->AddConvexPolyFilled(diamond, 4, kKeyColor);
                }
                if (isSelected) drawList->AddCircle(middle, r + 3, IM_COL32(255, 255, 255, 160));
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("key");
                if (ImGui::BeginPopup("key")) {
                    ImGui::TextDisabled("Into this key");
                    if (ImGui::MenuItem("Smooth", nullptr, key.ease == AnimationKey::Ease::Smooth)) key.ease = AnimationKey::Ease::Smooth, save(editor);
                    if (ImGui::MenuItem("Linear", nullptr, key.ease == AnimationKey::Ease::Linear)) key.ease = AnimationKey::Ease::Linear, save(editor);
                    if (ImGui::MenuItem("Step", nullptr, key.ease == AnimationKey::Ease::Step)) key.ease = AnimationKey::Ease::Step, save(editor);
                    ImGui::Separator();
                    ImGui::SetNextItemWidth(160);
                    if (ImGui::DragFloat("Time", &key.time, 0.01f, 0.0f, clip_.length, "%.2f s")) save(editor);
                    if (ImGui::MenuItem("Go to its time")) {
                        cursor_ = key.time;
                        showAt(editor, root);
                    }
                    if (ImGui::MenuItem("Remove")) removedKey = int(i);
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            if (removedKey >= 0) {
                track.keys.erase(track.keys.begin() + removedKey);
                selected_ = {-1, -1};
                save(editor);
            }
            ImGui::PopID();
            top += rowHeight;
        }
        if (removedTrack >= 0) {
            clip_.tracks.erase(clip_.tracks.begin() + removedTrack);
            selected_ = {-1, -1};
            save(editor);
        }
        // Delete removes the chosen key, while this window has the keyboard.
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            editor.panelOwnsEditKeys = true;
            if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
                if (selected_.first >= 0 && size_t(selected_.first) < clip_.tracks.size()) {
                    std::vector<AnimationKey>& keys = clip_.tracks[size_t(selected_.first)].keys;
                    if (selected_.second >= 0 && size_t(selected_.second) < keys.size()) {
                        keys.erase(keys.begin() + selected_.second);
                        save(editor);
                    }
                }
                selected_ = {-1, -1};
            }
        }
        // The playhead, red while recording.
        drawList->AddLine(ImVec2(xOf(cursor_), origin.y), ImVec2(xOf(cursor_), top), isRecording_ ? kRecordColor : IM_COL32(255, 122, 47, 255), 2);
        ImGui::SetCursorScreenPos(ImVec2(origin.x, top + 8));
        if (clip_.tracks.empty()) {
            ImGui::TextDisabled("Press Record and change something (move it, turn it, a light's intensity...), or Add Property.");
        } else {
            ImGui::TextDisabled("Double-click a track for a key there; drag keys; right-click a key or the ruler for more. Shift: off the frames.");
        }
    }

    /// How a track moves by itself, over its keys.
    void drawProcedural(EditorContext& editor, AnimationTrack& track) {
        Procedural& p = track.procedural;
        ImGui::TextDisabled("Moves by itself");
        int kind = int(p.kind);
        ImGui::SetNextItemWidth(180);
        if (ImGui::Combo("##kind", &kind, "Only its keys\0Wave\0Wander (noise)\0By a parameter\0")) {
            p.kind = Procedural::Kind(kind);
            save(editor);
        }
        if (!p.isOn()) return;
        ImGui::SetNextItemWidth(180);
        if (ImGui::DragFloat(p.kind == Procedural::Kind::Parameter ? "For each 1" : "Amount", &p.amplitude, 0.01f)) save(editor);
        if (p.kind == Procedural::Kind::Parameter) {
            std::vector<std::string> parameters = choicesFor(editor, root_, "parameter");
            ImGui::TextDisabled("Parameter (animator->setFloat)");
            if (pickFromList("parameter", p.parameter, parameters, true, 180)) save(editor);
        } else {
            ImGui::SetNextItemWidth(180);
            if (ImGui::DragFloat(p.kind == Procedural::Kind::Wave ? "Cycles a second" : "Speed", &p.frequency, 0.01f, 0.0f, 50.0f)) save(editor);
            ImGui::SetNextItemWidth(180);
            if (ImGui::SliderFloat("Phase", &p.phase, 0.0f, 1.0f)) save(editor);
        }
        if (track.width > 1) {
            ImGui::TextDisabled("Which of its numbers");
            const char* names[4] = {"x", "y", "z", "w"};
            for (int i = 0; i < track.width; i++) {
                if (i) ImGui::SameLine();
                bool on = p.axes[size_t(i)] != 0;
                if (ImGui::Checkbox(names[i], &on)) {
                    p.axes[size_t(i)] = on ? 1.0f : 0.0f;
                    save(editor);
                }
            }
        }
    }

    struct Seen {
        Entity entity;
        std::string path, component, field;
        int width = 1;
        std::array<float, 4> value;
    };

    Entity root_;
    std::string editing_;
    AnimationClip clip_;
    float cursor_ = 0;
    float eventAt_ = 0;
    bool isPreviewing_ = false, isRecording_ = false, isPlaying_ = false;
    bool dragged_ = false, wantsNewClip_ = false;
    std::string newName_;
    std::pair<int, int> selected_{-1, -1};
    std::vector<Before> saved_;
    std::map<std::string, Seen> seen_;
};

}  // namespace

void registerAnimationPanel(EditorContext& editor) {
    editor.registry.addPanel(nullptr, std::make_unique<AnimationPanel>());
}

}  // namespace molten::editor
