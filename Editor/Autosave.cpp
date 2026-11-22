// Autosave.cpp
// Unsaved work survives the editor closing badly. While the open scene has changes that are
// not saved, a copy of it is written every few minutes into the project's Library/Autosave
// folder; the scene's own file is never touched. Saving, or choosing not to save, removes
// the copy. A scene that is opened while a copy newer than its file exists asks whether to
// recover it. (Preferences > General turns this off and sets how often.)
#include "EditorApp.hpp"

#include "Core/TextFile.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <optional>

namespace molten::editor {

namespace fs = std::filesystem;

namespace {

double secondsNow() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

std::string EditorApp::autosavePathFor(const std::string& sceneFile) const {
    const EditorContext& editor = *context_;
    if (!editor.hasProject) return {};
    // Named after where the scene is among the assets: "Scenes/Main.scene" is kept as "Scenes_Main.scene".
    std::string name = "Untitled.scene";
    if (!sceneFile.empty()) {
        std::error_code code;
        name = fs::relative(fs::path(sceneFile), fs::path(editor.project.assetsDirectory()), code).generic_string();
        if (code || name.empty() || name.rfind("..", 0) == 0) name = fs::path(sceneFile).filename().string();
        for (char& c : name) {
            if (c == '/' || c == '\\') c = '_';
        }
    }
    return editor.project.libraryDirectory() + "/Autosave/" + name;
}

void EditorApp::autosave() {
    EditorContext& editor = *context_;
    const double now = secondsNow();
    if (nextAutosave_ <= 0) nextAutosave_ = now + autosaveMinutes_ * 60.0;
    if (now < nextAutosave_) return;
    nextAutosave_ = now + autosaveMinutes_ * 60.0;
    if (!autosaves_ || !editor.hasProject || isWorking() || editor.scene.isPlaying() || !editor.scene.isDirty() || !recoverPath_.empty()) return;
    std::string path = autosavePathFor(editor.scene.filePath);
    std::error_code code;
    fs::create_directories(fs::path(path).parent_path(), code);
    if (!text::writeFile(path, editor.scene.currentText())) editor.notify("The autosave could not be written: " + path, ConsoleMessage::Level::Warning);
}

void EditorApp::forgetAutosave() {
    EditorContext& editor = *context_;
    if (!editor.hasProject) return;
    std::error_code code;
    fs::remove(autosavePathFor(editor.scene.filePath), code);
    nextAutosave_ = 0;
}

void EditorApp::lookForAutosave() {
    EditorContext& editor = *context_;
    recoverPath_.clear();
    recoverText_.clear();
    if (!editor.hasProject || editor.scene.filePath.empty()) return;
    std::string path = autosavePathFor(editor.scene.filePath);
    std::error_code code;
    if (!fs::exists(path, code)) return;
    // Only a copy made after the file was last written is news.
    auto copyTime = fs::last_write_time(path, code);
    auto fileTime = fs::last_write_time(editor.scene.filePath, code);
    std::optional<std::string> kept = text::readFile(path);
    if (code || copyTime <= fileTime || !kept || *kept == editor.scene.currentText()) {
        fs::remove(path, code);
        return;
    }
    recoverPath_ = path;
    recoverText_ = *kept;
}

void EditorApp::drawRecoverDialog() {
    EditorContext& editor = *context_;
    if (recoverPath_.empty()) return;
    if (!ImGui::IsPopupOpen("Recover Unsaved Changes?")) ImGui::OpenPopup("Recover Unsaved Changes?");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Recover Unsaved Changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("\"%s\" had changes that were not saved when the editor last closed.", editor.scene.displayName().c_str());
    ImGui::TextDisabled("The editor kept a copy of them. Recover puts them back in the scene, for you to look at and save;\n"
                        "the scene's file stays as it is until you do. Discard throws the copy away.");
    ImGui::Spacing();
    bool closes = false;
    if (ImGui::Button("Recover", ImVec2(120, 0))) {
        if (editor.scene.recover(recoverText_)) editor.notify("Recovered the unsaved changes of " + editor.scene.displayName() + ": save to keep them, Undo to go back to the file");
        else editor.notify("The copy could not be read: " + recoverPath_, ConsoleMessage::Level::Error);
        closes = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard", ImVec2(120, 0))) {
        std::error_code code;
        fs::remove(recoverPath_, code);
        closes = true;
    }
    if (closes) {
        recoverPath_.clear();
        recoverText_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

}  // namespace molten::editor
