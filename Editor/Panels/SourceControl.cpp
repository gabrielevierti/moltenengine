// SourceControl.cpp
// Window > Source Control: the project in Git. What has changed since the last commit (click
// one to see how), a message and Commit, the commits before, Pull and Push. A project not in
// Git yet can be put in it with one button.
#include "Editor.hpp"
#include "Git.hpp"
#include "Core/TextFile.hpp"
#include "Panels/Widgets.hpp"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <future>
#include <set>

namespace molten::editor {

class SourceControlPanel final : public Panel {
public:
    SourceControlPanel() {
        title = "Source Control";
        isOpen = false;                 // Window > Source Control
    }

    void draw(EditorContext& editor) override {
        if (!editor.hasProject) {
            ImGui::TextDisabled("Open a project.");
            return;
        }
        std::string folder = editor.project.directory;
        if (folder != folder_) {
            folder_ = folder;
            nextLook_ = 0;
            selected_.clear();
            diffOf_.clear();
        }
        collectWork(editor);
        double now = ImGui::GetTime();
        if (now >= nextLook_ && !work_.valid()) {
            nextLook_ = now + 3.0;
            look();
        }
        if (!isRepository_) {
            ImGui::TextWrapped("This project is not in Git. Git keeps every version of it: what changed, when, and why, "
                               "and lets you go back.");
            if (ImGui::Button("Put the Project in Git")) start();
            if (!lastOutput_.empty()) ImGui::TextDisabled("%s", lastOutput_.c_str());
            return;
        }

        ImGui::Text("On %s", branch_.c_str());
        ImGui::SameLine();
        bool isBusy = work_.valid();
        if (isBusy) ImGui::BeginDisabled();
        if (ImGui::SmallButton("Pull")) begin({"pull", "--ff-only"}, "Pulled");
        ui::tooltip("Brings the commits made elsewhere");
        ImGui::SameLine();
        if (ImGui::SmallButton("Push")) begin({"push"}, "Pushed");
        ui::tooltip("Sends your commits to where the project is kept (GitHub...)");
        if (isBusy) ImGui::EndDisabled();
        if (isBusy) {
            ImGui::SameLine();
            ImGui::TextDisabled("working...");
        }
        if (!lastOutput_.empty()) ImGui::TextWrapped("%s", lastOutput_.c_str());
        ImGui::Separator();

        // What changed, each to keep in the commit or not.
        ImGui::Text("%zu changed", changes_.size());
        if (!changes_.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("All")) {
                for (const GitChange& change : changes_) left_.erase(change.path);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("None")) {
                for (const GitChange& change : changes_) left_.insert(change.path);
            }
        }
        float listHeight = std::min(ImGui::GetContentRegionAvail().y * 0.45f, ImGui::GetFrameHeightWithSpacing() * float(changes_.size()) + 12);
        ImGui::BeginChild("##changes", ImVec2(0, std::max(listHeight, 30.0f)), ImGuiChildFlags_Borders);
        for (const GitChange& change : changes_) {
            ImGui::PushID(change.path.c_str());
            bool keeps = !left_.count(change.path);
            if (ImGui::Checkbox("##keep", &keeps)) {
                if (keeps) left_.erase(change.path);
                else left_.insert(change.path);
            }
            ImGui::SameLine();
            ImVec4 color = change.isNew() ? ImVec4(0.45f, 0.85f, 0.5f, 1) : change.isDeleted() ? ImVec4(0.95f, 0.45f, 0.4f, 1) : ImVec4(0.95f, 0.75f, 0.35f, 1);
            ImGui::TextColored(color, "%-8s", change.kind());
            ImGui::SameLine();
            if (ImGui::Selectable(change.path.c_str(), selected_ == change.path)) showDiff(change);
            if (ImGui::BeginPopupContextItem("change")) {
                if (ImGui::MenuItem(change.isNew() ? "Delete the New File..." : "Undo Its Changes...")) confirmDiscard_ = change.path;
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        drawDiscard(editor);

        // The commit.
        ImGui::SetNextItemWidth(-90);
        ImGui::InputTextWithHint("##message", "What changed, in a few words", message_, sizeof(message_));
        ImGui::SameLine();
        size_t kept = 0;
        for (const GitChange& change : changes_) kept += !left_.count(change.path);
        bool canCommit = kept > 0 && std::strlen(message_) > 0 && !isBusy;
        if (!canCommit) ImGui::BeginDisabled();
        if (ImGui::Button("Commit", ImVec2(-1, 0))) commit(editor);
        if (!canCommit) ImGui::EndDisabled();
        if (editor.scene.isDirty()) ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1), "The scene has changes not saved: save it first to commit them.");

        // How the file chosen changed; then the history.
        if (ImGui::BeginTabBar("##below")) {
            if (ImGui::BeginTabItem("Changes")) {
                drawDiff();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("History")) {
                ImGui::BeginChild("##history");
                for (const GitCommit& commit : commits_) {
                    ImGui::TextDisabled("%s", commit.hash.c_str());
                    ImGui::SameLine();
                    ImGui::TextUnformatted(commit.message.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s, %s", commit.author.c_str(), commit.when.c_str());
                }
                if (commits_.empty()) ImGui::TextDisabled("No commits yet.");
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

private:
    void look() {
        int code = 0;
        std::string top = runGit(folder_, {"rev-parse", "--show-toplevel"}, code);
        isRepository_ = code == 0;
        if (!isRepository_) return;
        while (!top.empty() && (top.back() == '\n' || top.back() == '\r')) top.pop_back();
        repository_ = top;
        branch_ = runGit(folder_, {"rev-parse", "--abbrev-ref", "HEAD"}, code);
        while (!branch_.empty() && branch_.back() == '\n') branch_.pop_back();
        if (code != 0) branch_ = "a new branch";
        // Only the project's own files, wherever the repository starts.
        changes_ = parseGitStatus(runGit(folder_, {"status", "--porcelain=v1", "-uall", "--", "."}, code));
        commits_ = parseGitLog(runGit(folder_, {"log", "--format=%h%x09%an%x09%ar%x09%s", "-n", "60"}, code));
    }

    void start() {
        int code = 0;
        lastOutput_ = runGit(folder_, {"init", "-q"}, code);
        if (code == 0) {
            std::string ignore = folder_ + "/.gitignore";
            if (!std::filesystem::exists(ignore)) text::writeFile(ignore, projectGitIgnore());
            lastOutput_ = "Done: commit its files to keep this first version.";
        }
        nextLook_ = 0;
    }

    void showDiff(const GitChange& change) {
        selected_ = change.path;
        int code = 0;
        if (change.isNew()) {
            diffOf_ = "(a new file)";
            return;
        }
        // Paths of the status are the repository's: run from its top.
        diffOf_ = runGit(repository_, {"diff", "HEAD", "--", change.path}, code);
        if (diffOf_.size() > 200000) diffOf_ = diffOf_.substr(0, 200000) + "\n...";
        if (diffOf_.find("Binary files") != std::string::npos) diffOf_ = "(not text: a picture, a sound, a model)";
    }

    void drawDiff() {
        ImGui::BeginChild("##diff", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        if (selected_.empty()) ImGui::TextDisabled("Click a file above to see how it changed.");
        size_t start = 0;
        while (start < diffOf_.size()) {
            size_t end = diffOf_.find('\n', start);
            if (end == std::string::npos) end = diffOf_.size();
            std::string line = diffOf_.substr(start, end - start);
            start = end + 1;
            if (line.rfind("diff --git", 0) == 0 || line.rfind("index ", 0) == 0 || line.rfind("--- ", 0) == 0 || line.rfind("+++ ", 0) == 0) continue;
            ImVec4 color = line.rfind("@@", 0) == 0 ? ImVec4(0.55f, 0.65f, 0.85f, 1)
                           : !line.empty() && line[0] == '+' ? ImVec4(0.45f, 0.85f, 0.5f, 1)
                           : !line.empty() && line[0] == '-' ? ImVec4(0.95f, 0.45f, 0.4f, 1)
                                                             : ImVec4(0.75f, 0.78f, 0.82f, 1);
            ImGui::TextColored(color, "%s", line.c_str());
        }
        ImGui::EndChild();
    }

    void drawDiscard(EditorContext& editor) {
        if (!confirmDiscard_.empty()) ImGui::OpenPopup("Undo the changes?");
        if (!ImGui::BeginPopupModal("Undo the changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
        const GitChange* change = nullptr;
        for (const GitChange& candidate : changes_) {
            if (candidate.path == confirmDiscard_) change = &candidate;
        }
        bool isNew = change && change->isNew();
        ImGui::Text(isNew ? "%s is deleted: it was never committed." : "%s goes back to its last commit.", confirmDiscard_.c_str());
        ImGui::TextDisabled("This cannot be undone.");
        if (ImGui::Button(isNew ? "Delete It" : "Undo the Changes")) {
            int code = 0;
            if (isNew) {
                std::error_code ignored;
                std::filesystem::remove(std::filesystem::path(repository_) / confirmDiscard_, ignored);
            } else {
                lastOutput_ = runGit(repository_, {"checkout", "HEAD", "--", confirmDiscard_}, code);
            }
            editor.assets.refresh();
            confirmDiscard_.clear();
            nextLook_ = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep Them")) {
            confirmDiscard_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void commit(EditorContext& editor) {
        std::vector<std::string> add = {"add", "-A", "--"};
        for (const GitChange& change : changes_) {
            if (!left_.count(change.path)) add.push_back(change.path);
        }
        int code = 0;
        std::string output = runGit(repository_, add, code);
        if (code == 0) {
            // Only those: what was added before and is left out goes back to waiting.
            std::vector<std::string> commitArgs = {"commit", "-q", "-m", message_, "--"};
            for (const GitChange& change : changes_) {
                if (!left_.count(change.path)) commitArgs.push_back(change.path);
            }
            output = runGit(repository_, commitArgs, code);
        }
        if (code == 0) {
            lastOutput_ = "Committed: " + std::string(message_);
            message_[0] = 0;
            editor.notify(lastOutput_);
        } else {
            lastOutput_ = output;
        }
        nextLook_ = 0;
    }

    /// Pull and push take a while (the network): done beside the editor, and told when done.
    void begin(std::vector<std::string> arguments, std::string done) {
        std::string folder = folder_;
        doneWord_ = std::move(done);
        work_ = std::async(std::launch::async, [folder, arguments] {
            int code = 0;
            std::string output = runGit(folder, arguments, code);
            return std::make_pair(code, output);
        });
    }

    void collectWork(EditorContext& editor) {
        if (!work_.valid() || work_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        auto [code, output] = work_.get();
        lastOutput_ = code == 0 ? doneWord_ : output;
        if (code != 0 && output.find("could not read Username") != std::string::npos) {
            lastOutput_ = "Git needs to sign in: push once from Terminal (or set up GitHub Desktop or a key), then it works from here.";
        }
        if (code == 0) editor.assets.refresh();
        nextLook_ = 0;
    }

    std::string folder_, repository_, branch_;
    bool isRepository_ = false;
    double nextLook_ = 0;
    std::vector<GitChange> changes_;
    std::vector<GitCommit> commits_;
    std::set<std::string> left_;        // changes not to commit
    std::string selected_, diffOf_, lastOutput_, confirmDiscard_, doneWord_;
    char message_[512] = {};
    std::future<std::pair<int, std::string>> work_;
};

void registerSourceControlPanel(EditorContext& editor) {
    editor.registry.addPanel(nullptr, std::make_unique<SourceControlPanel>());
}

}  // namespace molten::editor
