// AnimatorPanel.cpp
// The Animator window, as in Unity: an Animator Controller drawn as a graph. Its states are boxes
// (each plays a clip, or blends several by a parameter), its transitions arrows between them,
// taken when their conditions on the parameters hold. The parameters are listed on the left, with
// the layers; what is chosen in the graph is edited on the right. While the game runs, the state
// each Animator is in is lit, and its parameters can be changed to try them.
//
// The controller shown: the one chosen in the Explorer, or that of the chosen entity's Animator.
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

namespace molten::editor {

namespace {

using Controller = AnimatorController;
using State = AnimatorController::State;
using Transition = AnimatorController::Transition;
using Condition = AnimatorController::Condition;
using Parameter = AnimatorController::Parameter;

const float kNodeWidth = 170, kNodeHeight = 44;
const int kEntry = -2, kAny = -3;      // the two boxes that are not states
const ImVec2 kEntryAt(-230, 0), kAnyAt(-230, 90);

ImVec2 add(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }
ImVec2 sub(ImVec2 a, ImVec2 b) { return ImVec2(a.x - b.x, a.y - b.y); }
float distanceToSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
    ImVec2 ab = sub(b, a), ap = sub(p, a);
    float length2 = ab.x * ab.x + ab.y * ab.y;
    float t = length2 > 0 ? std::clamp((ap.x * ab.x + ap.y * ab.y) / length2, 0.0f, 1.0f) : 0.0f;
    ImVec2 at(a.x + ab.x * t - p.x, a.y + ab.y * t - p.y);
    return std::sqrt(at.x * at.x + at.y * at.y);
}

class AnimatorPanel final : public Panel {
public:
    AnimatorPanel() {
        title = "Animator";
        isOpen = false;                 // Window > Animation > Animator, or Open Animator on an Animator
    }

    void draw(EditorContext& editor) override {
        Scene& scene = editor.engine.scene;
        Entity owner = ownerOf(editor);
        std::string path = controllerPath(editor, owner);
        if (path.empty()) {
            Animator* animator = scene.get<Animator>(owner);
            if (animator) {
                ImGui::TextWrapped("%s's Animator has no controller: it plays one clip. A controller gives it states, and transitions between "
                                   "them that the game drives with parameters.", editor.scene.nameOf(owner).c_str());
                if (ImGui::Button("Create Controller")) createController(editor, owner);
            } else {
                ImGui::TextDisabled("Choose an entity with an Animator, or an Animator Controller in the Explorer.");
            }
            path_.clear();
            return;
        }
        if (path != path_) load(editor, path);
        owner_ = owner;
        if (!isValid_) {
            ImGui::TextColored(ImVec4(1, 0.55f, 0.4f, 1), "%s cannot be read: %s", path_.c_str(), error_.c_str());
            if (ImGui::Button("Read Again")) load(editor, path);
            return;
        }

        ImGui::TextUnformatted(std::filesystem::path(path_).stem().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", path_.c_str());
        if (scene.isAlive(owner) && scene.has<Animator>(owner)) {
            ImGui::SameLine();
            ImGui::TextDisabled("  on %s", editor.scene.nameOf(owner).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Open Timeline")) editor.openPanel("Timeline");
            ui::tooltip("Its animations, to key");
        }

        float height = ImGui::GetContentRegionAvail().y;
        if (ImGui::BeginChild("##left", ImVec2(230, height), ImGuiChildFlags_Borders)) drawLeft(editor);
        ImGui::EndChild();
        ImGui::SameLine();
        float graphWidth = std::max(ImGui::GetContentRegionAvail().x - 310, 200.0f);
        if (ImGui::BeginChild("##graph", ImVec2(graphWidth, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            drawGraph(editor);
        }
        ImGui::EndChild();
        ImGui::SameLine();
        if (ImGui::BeginChild("##right", ImVec2(0, height), ImGuiChildFlags_Borders)) drawSelected(editor);
        ImGui::EndChild();
    }

private:
    // ---------------------------------------------------------------- which controller

    Entity ownerOf(EditorContext& editor) {
        Scene& scene = editor.engine.scene;
        for (Entity at = editor.scene.primary(); scene.isAlive(at);) {
            if (scene.has<Animator>(at)) return at;
            const Transform* transform = scene.get<Transform>(at);
            at = transform ? transform->parent : Entity{};
        }
        // A controller chosen in the Explorer: the first entity that plays it, if one does.
        if (isControllerFile(editor.selectedAsset)) {
            Entity found;
            scene.each<Animator>([&](Entity entity, Animator& animator) {
                if (!found.isValid() && animator.controller == editor.selectedAsset) found = entity;
            });
            return found;
        }
        return scene.isAlive(owner_) && scene.has<Animator>(owner_) ? owner_ : Entity{};
    }

    static bool isControllerFile(const std::string& path) {
        return path.size() > 9 && path.compare(path.size() - 9, 9, ".animator") == 0;
    }

    std::string controllerPath(EditorContext& editor, Entity owner) {
        if (isControllerFile(editor.selectedAsset)) return editor.selectedAsset;
        const Animator* animator = editor.engine.scene.get<Animator>(owner);
        return animator ? animator->controller : std::string();
    }

    void load(EditorContext& editor, const std::string& path) {
        path_ = path;
        layer_ = -1;
        selectedState_ = selectedTransition_ = -1;
        linkFrom_ = -1000;
        error_.clear();
        isValid_ = false;
        std::optional<std::string> text = text::readFile(editor.engine.assetPath(path));
        if (!text) {
            error_ = "the file is not there";
            return;
        }
        if (std::optional<Controller> read = Controller::parse(*text, &error_)) {
            controller_ = std::move(*read);
            isValid_ = true;
            placeUnplaced(controller_.states);
            for (auto& layer : controller_.layers) placeUnplaced(layer.states);
        }
    }

    /// States written by hand, with no place: in a row, then rows below.
    static void placeUnplaced(std::vector<State>& states) {
        bool none = std::all_of(states.begin(), states.end(), [](const State& s) { return s.x == 0 && s.y == 0; });
        if (!none) return;
        for (size_t i = 0; i < states.size(); i++) {
            states[i].x = 40.0f + float(i % 3) * 230.0f;
            states[i].y = float(i / 3) * 110.0f;
        }
    }

    void save(EditorContext& editor) {
        if (!text::writeFile(editor.engine.assetPath(path_), controller_.toText())) {
            editor.notify("Cannot write " + path_, ConsoleMessage::Level::Error);
            return;
        }
        // The Animators that play it read it again, keeping the state they are in.
        editor.engine.scene.each<Animator>([&](Entity, Animator& animator) {
            if (animator.controller == path_) animator.graphFrom = "\n";
        });
    }

    void createController(EditorContext& editor, Entity owner) {
        Animator* animator = editor.engine.scene.get<Animator>(owner);
        if (!animator) return;
        Controller fresh;
        // What it played becomes the first state.
        std::string clip = animator->clip;
        if (clip.empty()) {
            std::vector<std::string> clips = choicesFor(editor, owner, "animation");
            if (!clips.empty()) clip = clips.front();
        }
        if (!clip.empty()) {
            State state;
            state.name = isAnimationClipFile(clip) ? std::filesystem::path(clip).stem().string() : clip;
            state.motions.push_back({clip, 0, 0});
            state.x = 40;
            fresh.states.push_back(state);
            fresh.start = state.name;
        }
        std::string path = editor.assets.uniquePath("Animations", editor.scene.nameOf(owner), ".animator");
        std::filesystem::create_directories(std::filesystem::path(editor.assets.absolutePath(path)).parent_path());
        if (!text::writeFile(editor.assets.absolutePath(path), fresh.toText())) {
            editor.notify("Cannot write " + path, ConsoleMessage::Level::Error);
            return;
        }
        editor.assets.refresh();
        animator->controller = path;
        editor.scene.markChanged();
    }

    // ---------------------------------------------------------------- the layer being edited

    std::vector<State>& states() { return layer_ < 0 ? controller_.states : controller_.layers[size_t(layer_)].states; }
    std::vector<Transition>& transitions() { return layer_ < 0 ? controller_.transitions : controller_.layers[size_t(layer_)].transitions; }
    std::string& start() { return layer_ < 0 ? controller_.start : controller_.layers[size_t(layer_)].start; }

    int findState(const std::string& name) {
        for (size_t i = 0; i < states().size(); i++) if (states()[i].name == name) return int(i);
        return -1;
    }

    std::string uniqueStateName(const std::string& wanted) {
        std::string name = wanted.empty() ? std::string("New State") : wanted;
        for (int n = 2; findState(name) >= 0; n++) name = wanted + " " + std::to_string(n);
        return name;
    }

    /// The live Animator, while the game runs: the one on the owner.
    Animator* live(EditorContext& editor) {
        if (!editor.scene.isPlaying()) return nullptr;
        Animator* animator = editor.engine.scene.get<Animator>(owner_);
        return animator && animator->controller == path_ ? animator : nullptr;
    }

    // ---------------------------------------------------------------- left: layers and parameters

    void drawLeft(EditorContext& editor) {
        ImGui::TextUnformatted("Layers");
        if (ImGui::Selectable("Base Layer", layer_ < 0)) selectLayer(-1);
        for (size_t i = 0; i < controller_.layers.size(); i++) {
            ImGui::PushID(int(i));
            if (ImGui::Selectable(controller_.layers[i].name.c_str(), layer_ == int(i))) selectLayer(int(i));
            ImGui::PopID();
        }
        if (ImGui::SmallButton("+ Layer")) {
            Controller::Layer layer;
            layer.name = "Layer " + std::to_string(controller_.layers.size() + 1);
            controller_.layers.push_back(layer);
            selectLayer(int(controller_.layers.size()) - 1);
            save(editor);
        }
        ui::tooltip("States that play over the base on part of the body: arms that aim while the legs walk");
        if (layer_ >= 0) drawLayerSettings(editor);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextUnformatted("Parameters");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 14);
        if (ImGui::SmallButton("+")) ImGui::OpenPopup("add parameter");
        if (ImGui::BeginPopup("add parameter")) {
            auto addKind = [&](const char* label, Parameter::Kind kind, const char* stem) {
                if (!ImGui::MenuItem(label)) return;
                Parameter parameter;
                parameter.kind = kind;
                parameter.name = stem;
                for (int n = 2; controller_.findParameter(parameter.name); n++) parameter.name = std::string(stem) + std::to_string(n);
                controller_.parameters.push_back(parameter);
                save(editor);
            };
            addKind("Float (a number: speed)", Parameter::Kind::Float, "speed");
            addKind("Bool (yes or no: grounded)", Parameter::Kind::Bool, "grounded");
            addKind("Trigger (once: jump)", Parameter::Kind::Trigger, "jump");
            ImGui::EndPopup();
        }
        Animator* running = live(editor);
        if (running) ImGui::TextDisabled("Live: change them to try");
        int removed = -1;
        for (size_t i = 0; i < controller_.parameters.size(); i++) {
            Parameter& parameter = controller_.parameters[i];
            ImGui::PushID(int(i));
            const char* kind = parameter.kind == Parameter::Kind::Float ? "F" : parameter.kind == Parameter::Kind::Bool ? "B" : "T";
            ImGui::TextDisabled("%s", kind);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            std::string name = parameter.name;
            if (ImGui::InputText("##name", &name, ImGuiInputTextFlags_EnterReturnsTrue) && !name.empty() && !controller_.findParameter(name)) {
                renameParameter(parameter.name, name);
                save(editor);
            }
            ui::tooltip("Its name, as code and visual scripts set it; Return to rename it everywhere it is used");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(60);
            float* value = running ? &running->parameters[parameter.name] : &parameter.value;
            if (parameter.kind == Parameter::Kind::Float) {
                if (ImGui::DragFloat("##value", value, 0.02f) && !running) save(editor);
            } else if (parameter.kind == Parameter::Kind::Bool) {
                bool on = *value > 0.5f;
                if (ImGui::Checkbox("##value", &on)) {
                    *value = on ? 1.0f : 0.0f;
                    if (!running) save(editor);
                }
            } else if (running) {
                if (ImGui::SmallButton("Fire")) running->setTrigger(parameter.name);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) removed = int(i);
            ImGui::PopID();
        }
        if (removed >= 0) {
            controller_.parameters.erase(controller_.parameters.begin() + removed);
            save(editor);
        }
        if (controller_.parameters.empty()) ImGui::TextDisabled("None yet: + adds one.");
    }

    void selectLayer(int layer) {
        layer_ = layer;
        selectedState_ = selectedTransition_ = -1;
        linkFrom_ = -1000;
    }

    void renameParameter(const std::string& from, const std::string& to) {
        for (Parameter& parameter : controller_.parameters) if (parameter.name == from) parameter.name = to;
        auto inStates = [&](std::vector<State>& list) {
            for (State& state : list) {
                if (state.blendBy == from) state.blendBy = to;
                if (state.blendByY == from) state.blendByY = to;
            }
        };
        auto inTransitions = [&](std::vector<Transition>& list) {
            for (Transition& transition : list) for (Condition& condition : transition.conditions) if (condition.parameter == from) condition.parameter = to;
        };
        inStates(controller_.states);
        inTransitions(controller_.transitions);
        for (auto& layer : controller_.layers) {
            inStates(layer.states);
            inTransitions(layer.transitions);
            if (layer.weightBy == from) layer.weightBy = to;
        }
    }

    void drawLayerSettings(EditorContext& editor) {
        Controller::Layer& layer = controller_.layers[size_t(layer_)];
        ImGui::Indent(8);
        ImGui::SetNextItemWidth(150);
        std::string name = layer.name;
        if (ImGui::InputText("Name##layer", &name, ImGuiInputTextFlags_EnterReturnsTrue) && !name.empty()) {
            layer.name = name;
            save(editor);
        }
        std::vector<std::string> floats = {"(a number)"};
        for (const Parameter& parameter : controller_.parameters) if (parameter.kind == Parameter::Kind::Float) floats.push_back(parameter.name);
        std::string by = layer.weightBy.empty() ? floats[0] : layer.weightBy;
        ImGui::SetNextItemWidth(150);
        if (ImGui::BeginCombo("Weight by", by.c_str())) {
            for (const std::string& choice : floats) {
                if (ImGui::Selectable(choice.c_str(), choice == by)) {
                    layer.weightBy = choice == floats[0] ? std::string() : choice;
                    save(editor);
                }
            }
            ImGui::EndCombo();
        }
        if (layer.weightBy.empty()) {
            ImGui::SetNextItemWidth(150);
            if (ImGui::SliderFloat("Weight", &layer.weight, 0, 1)) save(editor);
        }
        if (ImGui::Checkbox("Additive", &layer.additive)) save(editor);
        ui::tooltip("Adds its movement to what is under it (a breath, a flinch), instead of taking its place");
        // The bones it acts on: from the model, ticked.
        std::string bones;
        for (size_t i = 0; i < layer.mask.size(); i++) bones += (i ? ", " : "") + layer.mask[i];
        ImGui::TextDisabled("Bones (and all below them)");
        if (pickValueInline(editor, owner_, "bones", bones, 200)) {
            layer.mask.clear();
            size_t from = 0;
            while (from < bones.size()) {
                size_t comma = bones.find(',', from);
                std::string bone = bones.substr(from, comma == std::string::npos ? std::string::npos : comma - from);
                while (!bone.empty() && bone.front() == ' ') bone.erase(bone.begin());
                if (!bone.empty()) layer.mask.push_back(bone);
                if (comma == std::string::npos) break;
                from = comma + 1;
            }
            save(editor);
        }
        if (ImGui::SmallButton("Remove Layer")) {
            controller_.layers.erase(controller_.layers.begin() + layer_);
            selectLayer(-1);
            save(editor);
        }
        ImGui::Unindent(8);
    }

    // ---------------------------------------------------------------- the graph

    ImVec2 placeOf(int node) {
        if (node == kEntry) return kEntryAt;
        if (node == kAny) return kAnyAt;
        const State& state = states()[size_t(node)];
        return ImVec2(state.x, state.y);
    }

    void drawGraph(EditorContext& editor) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 corner = ImGui::GetCursorScreenPos();
        ImVec2 size = ImGui::GetContentRegionAvail();
        ImVec2 origin = add(add(corner, pan_), ImVec2(260, 40));
        auto screen = [&](ImVec2 at) { return add(origin, at); };
        auto center = [&](int node) {
            ImVec2 at = screen(placeOf(node));
            return ImVec2(at.x + kNodeWidth * 0.5f, at.y + kNodeHeight * 0.5f);
        };
        // A grid, moving with the view.
        drawList->AddRectFilled(corner, add(corner, size), IM_COL32(28, 30, 35, 255));
        const float step = 32;
        for (float x = std::fmod(pan_.x, step); x < size.x; x += step) drawList->AddLine(ImVec2(corner.x + x, corner.y), ImVec2(corner.x + x, corner.y + size.y), IM_COL32(255, 255, 255, 12));
        for (float y = std::fmod(pan_.y, step); y < size.y; y += step) drawList->AddLine(ImVec2(corner.x, corner.y + y), ImVec2(corner.x + size.x, corner.y + y), IM_COL32(255, 255, 255, 12));

        // The empty canvas takes the clicks the boxes do not.
        ImGui::SetCursorScreenPos(corner);
        ImGui::InvisibleButton("##canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        bool canvasHovered = ImGui::IsItemHovered();
        if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || (io.KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left)))) {
            pan_ = add(pan_, io.MouseDelta);
        }
        Animator* running = live(editor);
        int liveState = -1;
        float liveProgress = 0;
        if (running) {
            if (layer_ < 0) liveState = running->state, liveProgress = running->progress;
            else if (size_t(layer_) < running->layerPlaying.size()) liveState = running->layerPlaying[size_t(layer_)].state, liveProgress = running->layerPlaying[size_t(layer_)].progress;
        }

        // The transitions: arrows, side by side where two go each way between the same states.
        int hoveredTransition = -1;
        std::vector<Transition>& list = transitions();
        for (size_t i = 0; i < list.size(); i++) {
            const Transition& transition = list[i];
            int from = transition.from == "Any" ? kAny : findState(transition.from);
            int to = findState(transition.to);
            if ((from < 0 && from != kAny) || to < 0) continue;
            ImVec2 a = center(from), b = center(to);
            ImVec2 along = sub(b, a);
            float length = std::max(std::sqrt(along.x * along.x + along.y * along.y), 1.0f);
            ImVec2 unit(along.x / length, along.y / length), side(-unit.y * 6, unit.x * 6);
            a = add(a, side);
            b = add(b, side);
            bool isSelected = selectedTransition_ == int(i);
            bool isHovered = canvasHovered && distanceToSegment(io.MousePos, a, b) < 6;
            if (isHovered) hoveredTransition = int(i);
            ImU32 color = isSelected ? IM_COL32(120, 190, 255, 255) : isHovered ? IM_COL32(230, 230, 230, 255) : IM_COL32(180, 184, 192, 200);
            drawList->AddLine(a, b, color, isSelected ? 3.0f : 2.0f);
            ImVec2 middle((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
            ImVec2 tip = add(middle, ImVec2(unit.x * 7, unit.y * 7));
            ImVec2 left = add(sub(middle, ImVec2(unit.x * 5, unit.y * 5)), ImVec2(-unit.y * 6, unit.x * 6));
            ImVec2 right = add(sub(middle, ImVec2(unit.x * 5, unit.y * 5)), ImVec2(unit.y * 6, -unit.x * 6));
            drawList->AddTriangleFilled(tip, left, right, color);
        }
        // Entry points at the start state.
        if (int first = findState(start()); first >= 0) {
            ImVec2 a = center(kEntry), b = center(first);
            drawList->AddLine(a, b, IM_COL32(255, 160, 60, 200), 2);
        }

        // The boxes.
        int hoveredNode = -1000;
        auto box = [&](int node, const char* label, ImU32 fill, bool isSelected, bool isLive) {
            ImVec2 at = screen(placeOf(node));
            ImVec2 end(at.x + kNodeWidth, at.y + kNodeHeight);
            ImGui::PushID(node + 10000);
            ImGui::SetCursorScreenPos(at);
            ImGui::InvisibleButton("##node", ImVec2(kNodeWidth, kNodeHeight), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            bool hovered = ImGui::IsItemHovered();
            if (hovered) hoveredNode = node;
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && node >= 0 && linkFrom_ == -1000) {
                State& state = states()[size_t(node)];
                state.x += io.MouseDelta.x;
                state.y += io.MouseDelta.y;
                moved_ = true;
            }
            if (ImGui::IsItemDeactivated() && moved_) {
                moved_ = false;
                save(editor);
            }
            drawList->AddRectFilled(at, end, fill, 6);
            if (isLive) {
                drawList->AddRect(at, end, IM_COL32(90, 200, 255, 255), 6.0f, 0, 3.0f);
                float shown = std::clamp(liveProgress - std::floor(liveProgress), 0.0f, 1.0f);
                drawList->AddRectFilled(ImVec2(at.x + 6, end.y - 7), ImVec2(at.x + 6 + (kNodeWidth - 12) * shown, end.y - 4), IM_COL32(90, 200, 255, 255), 2);
            } else if (isSelected || hovered) {
                drawList->AddRect(at, end, isSelected ? IM_COL32(120, 190, 255, 255) : IM_COL32(255, 255, 255, 120), 6.0f, 0, isSelected ? 2.0f : 1.0f);
            }
            ImVec2 textSize = ImGui::CalcTextSize(label);
            drawList->AddText(ImVec2(at.x + (kNodeWidth - textSize.x) * 0.5f, at.y + (kNodeHeight - textSize.y) * 0.5f - 2), IM_COL32(240, 240, 240, 255), label);
            bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left), rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
            ImGui::PopID();
            return std::make_pair(clicked, rightClicked);
        };
        for (int node : {kEntry, kAny}) {
            auto [clicked, rightClicked] = box(node, node == kEntry ? "Entry" : "Any State", node == kEntry ? IM_COL32(60, 120, 60, 255) : IM_COL32(60, 120, 140, 255), false, false);
            if (clicked && linkFrom_ == -1000) selectedState_ = selectedTransition_ = -1;
            if (rightClicked && node == kAny) {
                menuNode_ = kAny;
                ImGui::OpenPopup("node");
            }
        }
        for (size_t i = 0; i < states().size(); i++) {
            const State& state = states()[i];
            bool isStart = state.name == start();
            std::string label = state.name + (state.blendBy.empty() ? "" : "  (blend)");
            auto [clicked, rightClicked] = box(int(i), label.c_str(), isStart ? IM_COL32(170, 95, 40, 255) : IM_COL32(64, 68, 78, 255), selectedState_ == int(i), liveState == int(i));
            if (clicked) {
                if (linkFrom_ != -1000) {
                    // The arrow being drawn ends here.
                    Transition transition;
                    transition.from = linkFrom_ == kAny ? std::string("Any") : states()[size_t(linkFrom_)].name;
                    transition.to = state.name;
                    if (transition.from != transition.to) {
                        transitions().push_back(transition);
                        selectedTransition_ = int(transitions().size()) - 1;
                        selectedState_ = -1;
                        save(editor);
                    }
                    linkFrom_ = -1000;
                } else {
                    selectedState_ = int(i);
                    selectedTransition_ = -1;
                }
            }
            if (rightClicked) {
                menuNode_ = int(i);
                ImGui::OpenPopup("node");
            }
        }
        // The arrow being drawn, to the pointer.
        if (linkFrom_ != -1000) {
            drawList->AddLine(center(linkFrom_), io.MousePos, IM_COL32(120, 190, 255, 255), 2);
            if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) linkFrom_ = -1000;
        }
        // A click on the canvas: an arrow, or nothing chosen; a right click: what can be made.
        if (canvasHovered && hoveredNode == -1000 && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt) {
            if (linkFrom_ != -1000) linkFrom_ = -1000;
            else {
                selectedTransition_ = hoveredTransition;
                selectedState_ = -1;
            }
        }
        if (canvasHovered && hoveredNode == -1000 && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && linkFrom_ == -1000 && !ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            createAt_ = sub(io.MousePos, origin);
            ImGui::OpenPopup("canvas");
        }
        if (ImGui::BeginPopup("canvas")) {
            drawCreateMenu(editor);
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopup("node")) {
            drawNodeMenu(editor);
            ImGui::EndPopup();
        }
        // Delete removes what is chosen, while the window has the keyboard.
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
            editor.panelOwnsEditKeys = true;
            if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) removeSelected(editor);
        }
        drawList->AddText(ImVec2(corner.x + 8, corner.y + size.y - 20), IM_COL32(150, 156, 168, 200),
                          linkFrom_ != -1000 ? "Click the state it goes to (Esc: cancel)" : "Right-click: new state, transition.  Middle-drag or Alt-drag: move the view");
    }

    void drawCreateMenu(EditorContext& editor) {
        auto make = [&](const std::string& name, const std::string& clip, bool isBlend) {
            State state;
            state.name = uniqueStateName(name);
            state.x = createAt_.x;
            state.y = createAt_.y;
            if (!clip.empty()) state.motions.push_back({clip, 0, 0});
            if (isBlend) {
                for (const Parameter& parameter : controller_.parameters) {
                    if (parameter.kind == Parameter::Kind::Float && state.blendBy.empty()) state.blendBy = parameter.name;
                }
                if (state.blendBy.empty()) {
                    Parameter speed;
                    speed.name = "speed";
                    for (int n = 2; controller_.findParameter(speed.name); n++) speed.name = "speed" + std::to_string(n);
                    controller_.parameters.push_back(speed);
                    state.blendBy = speed.name;
                }
            }
            states().push_back(state);
            if (start().empty()) start() = state.name;
            selectedState_ = int(states().size()) - 1;
            selectedTransition_ = -1;
            save(editor);
        };
        if (ImGui::MenuItem("Empty State")) make("New State", "", false);
        if (ImGui::BeginMenu("State From Clip")) {
            std::vector<std::string> clips = choicesFor(editor, owner_, "animation");
            for (const std::string& clip : clips) {
                std::string name = isAnimationClipFile(clip) ? std::filesystem::path(clip).stem().string() : clip;
                if (ImGui::MenuItem(clip.c_str())) make(name, clip, false);
            }
            if (clips.empty()) ImGui::TextDisabled("No clips: the entity's model has none, and none are made (Animation window)");
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Blend Tree")) make("Blend Tree", "", true);
        ui::tooltip("Several clips mixed by a number: idle, walk and run by speed");
        Animator* animator = editor.engine.scene.get<Animator>(owner_);
        if (ImGui::MenuItem("Procedural Animation", nullptr, false, animator != nullptr)) {
            // A new animation of the entity that moves by itself, driven by a parameter of this
            // controller: a float "amount" (made if missing) that code or a script sets.
            std::string parameterName = "amount";
            if (!controller_.findParameter(parameterName)) {
                Parameter amount;
                amount.name = parameterName;
                amount.value = 1;
                controller_.parameters.push_back(amount);
            }
            std::string ownerName = editor.scene.nameOf(owner_);
            std::string path = editor.assets.uniquePath("Animations/" + ownerName, "Procedural", ".anim");
            std::filesystem::create_directories(std::filesystem::path(editor.assets.absolutePath(path)).parent_path());
            AnimationClip clip;
            clip.owner = ownerName;
            AnimationTrack track;
            track.component = "Transform";
            track.field = "rotation";
            track.width = 3;
            std::array<float, 4> now{};
            readProperty(editor.engine.scene, owner_, "Transform", "rotation", now);
            track.setKey(0, now);
            track.procedural.kind = Procedural::Kind::Parameter;
            track.procedural.amplitude = 90;
            track.procedural.parameter = parameterName;
            track.procedural.axes = {0, 1, 0, 0};
            clip.tracks.push_back(track);
            if (text::writeFile(editor.assets.absolutePath(path), clip.toText())) {
                editor.assets.refresh();
                animator->animations += (animator->animations.empty() ? "" : ", ") + path;
                editor.scene.markChanged();
                make("Procedural", path, false);
                editor.notify("Turned by \"amount\": animator->setFloat(\"amount\", v). Change what it moves in the Timeline (right click its track)");
            }
        }
        ui::tooltip("An animation that moves by itself as a parameter says: turned, moved or scaled as the game sets a number");
    }

    void drawNodeMenu(EditorContext& editor) {
        if (ImGui::MenuItem("Make Transition")) linkFrom_ = menuNode_;
        if (menuNode_ < 0 || size_t(menuNode_) >= states().size()) return;
        State& state = states()[size_t(menuNode_)];
        if (ImGui::MenuItem("Set as Start", nullptr, state.name == start())) {
            start() = state.name;
            save(editor);
        }
        if (ImGui::MenuItem("Delete")) {
            selectedState_ = menuNode_;
            selectedTransition_ = -1;
            removeSelected(editor);
        }
    }

    void removeSelected(EditorContext& editor) {
        if (selectedTransition_ >= 0 && size_t(selectedTransition_) < transitions().size()) {
            transitions().erase(transitions().begin() + selectedTransition_);
        } else if (selectedState_ >= 0 && size_t(selectedState_) < states().size()) {
            std::string name = states()[size_t(selectedState_)].name;
            states().erase(states().begin() + selectedState_);
            auto& list = transitions();
            list.erase(std::remove_if(list.begin(), list.end(), [&](const Transition& t) { return t.from == name || t.to == name; }), list.end());
            if (start() == name) start() = states().empty() ? std::string() : states().front().name;
        } else {
            return;
        }
        selectedState_ = selectedTransition_ = -1;
        save(editor);
    }

    // ---------------------------------------------------------------- right: what is chosen

    void drawSelected(EditorContext& editor) {
        if (selectedState_ >= 0 && size_t(selectedState_) < states().size()) drawState(editor, states()[size_t(selectedState_)]);
        else if (selectedTransition_ >= 0 && size_t(selectedTransition_) < transitions().size()) drawTransition(editor, transitions()[size_t(selectedTransition_)]);
        else {
            ImGui::TextDisabled("Choose a state or a transition.");
            ImGui::Spacing();
            ImGui::TextWrapped("The game drives the controller with its parameters: from code (animator->setFloat(\"speed\", v), setBool, setTrigger, "
                               "crossFade) or from a visual script (the Animation boxes). Its events come to the entity's scripts as messages.");
        }
    }

    std::vector<std::string> floatParameters() {
        std::vector<std::string> names;
        for (const Parameter& parameter : controller_.parameters) if (parameter.kind == Parameter::Kind::Float) names.push_back(parameter.name);
        return names;
    }

    void drawState(EditorContext& editor, State& state) {
        ImGui::TextDisabled("State");
        std::string name = state.name;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("##state name", &name, ImGuiInputTextFlags_EnterReturnsTrue) && !name.empty() && findState(name) < 0) {
            for (Transition& transition : transitions()) {
                if (transition.from == state.name) transition.from = name;
                if (transition.to == state.name) transition.to = name;
            }
            if (start() == state.name) start() = name;
            state.name = name;
            save(editor);
        }
        ui::tooltip("Return to rename it");
        bool isStart = start() == state.name;
        if (ImGui::Checkbox("Start", &isStart) && isStart) {
            start() = state.name;
            save(editor);
        }
        if (ImGui::Checkbox("Loops", &state.loops)) save(editor);
        ImGui::TextDisabled("Time by");
        ImGui::SameLine();
        if (pickFromList("timeBy", state.timeBy, floatParameters(), false, 120)) save(editor);
        ui::tooltip("A float parameter (0 to 1) that says how far through it is, instead of time. None: by time");
        ImGui::SetNextItemWidth(120);
        if (ImGui::DragFloat("Speed", &state.speed, 0.01f, -4, 4)) save(editor);

        // One clip, or a blend tree: clips mixed by one parameter (or two).
        ImGui::Spacing();
        bool isBlend = !state.blendBy.empty();
        if (ImGui::Checkbox("Blend Tree", &isBlend)) {
            std::vector<std::string> floats = floatParameters();
            state.blendBy = isBlend ? (floats.empty() ? std::string() : floats.front()) : std::string();
            state.blendByY.clear();
            if (isBlend && floats.empty()) editor.notify("A blend tree needs a Float parameter: add one on the left", ConsoleMessage::Level::Warning);
            save(editor);
        }
        if (isBlend) {
            std::vector<std::string> floats = floatParameters();
            ImGui::TextDisabled("By");
            ImGui::SameLine();
            if (pickFromList("blendBy", state.blendBy, floats, false, 110)) save(editor);
            ImGui::SameLine();
            std::vector<std::string> second = floats;
            if (pickFromList("blendByY", state.blendByY, second, false, 110)) save(editor);
            ui::tooltip("A second parameter: a 2D blend tree (walking in eight directions). None: one parameter");
        }
        ImGui::Spacing();
        ImGui::TextDisabled(isBlend ? "Motions, each where it plays alone" : "Motion");
        int removed = -1;
        for (size_t i = 0; i < state.motions.size(); i++) {
            AnimatorController::Motion& motion = state.motions[i];
            ImGui::PushID(int(i));
            if (pickValueInline(editor, owner_, "animation", motion.clip, isBlend ? 130 : 220)) save(editor);
            if (isBlend) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(state.blendByY.empty() ? 70 : 34);
                if (ImGui::DragFloat("##at", &motion.at, 0.02f)) save(editor);
                if (!state.blendByY.empty()) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(34);
                    if (ImGui::DragFloat("##y", &motion.y, 0.02f)) save(editor);
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) removed = int(i);
            ImGui::PopID();
        }
        if (removed >= 0) {
            state.motions.erase(state.motions.begin() + removed);
            save(editor);
        }
        if ((isBlend || state.motions.empty()) && ImGui::SmallButton("+ Motion")) {
            float at = state.motions.empty() ? 0.0f : state.motions.back().at + 1.0f;
            state.motions.push_back({"", at, 0});
            save(editor);
        }

        // Events: a sound played, or a message to the entity, at a point of the state.
        ImGui::Spacing();
        ImGui::TextDisabled("Events");
        int removedEvent = -1;
        for (size_t i = 0; i < state.events.size(); i++) {
            AnimatorController::Event& event = state.events[i];
            ImGui::PushID(int(i) + 500);
            std::vector<std::string> choices = choicesFor(editor, owner_, "sound");
            for (const std::string& message : choicesFor(editor, owner_, "message")) choices.push_back(message);
            if (pickFromList("event", event.name, choices, true, 140)) save(editor);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(50);
            if (ImGui::SliderFloat("##at", &event.at, 0, 1, "%.2f")) save(editor);
            ui::tooltip("How far through the state: 0 as it begins, 1 at its end");
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) removedEvent = int(i);
            ImGui::PopID();
        }
        if (removedEvent >= 0) {
            state.events.erase(state.events.begin() + removedEvent);
            save(editor);
        }
        if (ImGui::SmallButton("+ Event")) {
            state.events.push_back({"", 0});
            save(editor);
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Transitions from it: right-click it, Make Transition.");
    }

    void drawTransition(EditorContext& editor, Transition& transition) {
        ImGui::TextDisabled("Transition");
        ImGui::TextWrapped("%s  ->  %s", transition.from.c_str(), transition.to.c_str());
        ImGui::Spacing();
        bool hasExit = transition.exitAt >= 0;
        if (ImGui::Checkbox("Has Exit Time", &hasExit)) {
            transition.exitAt = hasExit ? 0.9f : -1.0f;
            save(editor);
        }
        ui::tooltip("Not before this much of the state has played: the end of a jump before the fall");
        if (hasExit) {
            ImGui::SetNextItemWidth(140);
            if (ImGui::SliderFloat("Exit At", &transition.exitAt, 0, 1, "%.2f")) save(editor);
        }
        ImGui::SetNextItemWidth(140);
        if (ImGui::DragFloat("Blend", &transition.blend, 0.01f, 0, 5, "%.2f s")) {
            transition.blend = std::max(transition.blend, 0.0f);
            save(editor);
        }
        ui::tooltip("Seconds to ease from one state into the other");

        ImGui::Spacing();
        ImGui::TextDisabled("Conditions (all of them)");
        std::vector<std::string> names;
        for (const Parameter& parameter : controller_.parameters) names.push_back(parameter.name);
        int removed = -1;
        for (size_t i = 0; i < transition.conditions.size(); i++) {
            Condition& condition = transition.conditions[i];
            ImGui::PushID(int(i));
            if (pickFromList("parameter", condition.parameter, names, false, 100)) {
                // The test fits the kind of parameter it now is.
                if (const Parameter* parameter = controller_.findParameter(condition.parameter)) {
                    condition.test = parameter->kind == Parameter::Kind::Trigger ? Condition::Test::Triggered
                                     : parameter->kind == Parameter::Kind::Bool  ? Condition::Test::IsTrue
                                                                                 : Condition::Test::Greater;
                }
                save(editor);
            }
            const Parameter* parameter = controller_.findParameter(condition.parameter);
            ImGui::SameLine();
            if (parameter && parameter->kind == Parameter::Kind::Bool) {
                ImGui::SetNextItemWidth(70);
                int which = condition.test == Condition::Test::IsFalse ? 1 : 0;
                if (ImGui::Combo("##test", &which, "true\0false\0")) {
                    condition.test = which ? Condition::Test::IsFalse : Condition::Test::IsTrue;
                    save(editor);
                }
            } else if (parameter && parameter->kind == Parameter::Kind::Float) {
                ImGui::SetNextItemWidth(44);
                int which = condition.test == Condition::Test::Less ? 1 : 0;
                if (ImGui::Combo("##test", &which, ">\0<\0")) {
                    condition.test = which ? Condition::Test::Less : Condition::Test::Greater;
                    save(editor);
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(50);
                if (ImGui::DragFloat("##value", &condition.value, 0.02f)) save(editor);
            } else if (parameter) {
                ImGui::TextDisabled("fired");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) removed = int(i);
            ImGui::PopID();
        }
        if (removed >= 0) {
            transition.conditions.erase(transition.conditions.begin() + removed);
            save(editor);
        }
        if (ImGui::SmallButton("+ Condition")) {
            if (controller_.parameters.empty()) {
                editor.notify("Add a parameter first (on the left): what the condition tests", ConsoleMessage::Level::Warning);
            } else {
                Condition condition;
                condition.parameter = controller_.parameters.front().name;
                Parameter::Kind kind = controller_.parameters.front().kind;
                condition.test = kind == Parameter::Kind::Trigger ? Condition::Test::Triggered : kind == Parameter::Kind::Bool ? Condition::Test::IsTrue : Condition::Test::Greater;
                transition.conditions.push_back(condition);
                save(editor);
            }
        }
        if (transition.conditions.empty() && transition.exitAt < 0) ImGui::TextColored(ImVec4(1, 0.75f, 0.4f, 1), "No condition and no exit time: taken at once.");
        ImGui::Spacing();
        if (ImGui::Button("Delete Transition")) removeSelected(editor);
    }

    std::string path_, error_;
    bool isValid_ = false;
    Controller controller_;
    Entity owner_;
    int layer_ = -1;                    // -1: the base layer
    int selectedState_ = -1, selectedTransition_ = -1;
    int linkFrom_ = -1000;              // a transition being drawn from this node; -1000: none
    int menuNode_ = -1000;
    bool moved_ = false;
    ImVec2 pan_{0, 0}, createAt_{0, 0};
};

}  // namespace

void registerAnimatorPanel(EditorContext& editor) {
    editor.registry.addPanel(nullptr, std::make_unique<AnimatorPanel>());
}

}  // namespace molten::editor
