// Git from the editor: what changed, commits, the history.
#include "Test.hpp"

#include "../Editor/Git.hpp"

#include <filesystem>
#include <fstream>

using namespace molten::editor;

TEST(git_status_and_log_are_read) {
    std::vector<GitChange> changes = parseGitStatus(" M Assets/Scenes/Main.scene\n?? \"Assets/Audio/caff\\303\\250 bar.wav\"\nR  old.txt -> new.txt\nD  gone.png\nA  added.cpp\n");
    CHECK(changes.size() == 5);
    CHECK(changes[0].path == "Assets/Scenes/Main.scene" && changes[0].unstaged == 'M' && std::string(changes[0].kind()) == "changed");
    CHECK(changes[1].path == "Assets/Audio/caffè bar.wav" && changes[1].isNew());
    CHECK(changes[2].path == "new.txt" && std::string(changes[2].kind()) == "renamed");
    CHECK(changes[3].isDeleted() && changes[4].isNew());
    std::vector<GitCommit> commits = parseGitLog("a1b2c3d\tGabriele\t2 hours ago\tRace: five lights\tand a tab\nbroken line\n");
    CHECK(commits.size() == 1 && commits[0].hash == "a1b2c3d" && commits[0].message == "Race: five lights\tand a tab");
}

TEST(git_runs_in_a_project) {
    std::string folder = "/tmp/molten-test-git";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder + "/Assets");
    int code = 0;
    runGit(folder, {"init", "-q"}, code);
    if (code != 0) return;                                  // no git here: nothing to try
    runGit(folder, {"config", "user.email", "test@example.com"}, code);
    runGit(folder, {"config", "user.name", "Test"}, code);
    std::ofstream(folder + "/Assets/a scene.scene") << "molten-scene 1\n";
    std::vector<GitChange> changes = parseGitStatus(runGit(folder, {"status", "--porcelain=v1", "-uall"}, code));
    CHECK(code == 0 && changes.size() == 1 && changes[0].path == "Assets/a scene.scene");
    runGit(folder, {"add", "--", "Assets/a scene.scene"}, code);
    runGit(folder, {"commit", "-q", "-m", "The first scene, it's here"}, code);
    CHECK(code == 0);
    std::vector<GitCommit> commits = parseGitLog(runGit(folder, {"log", "--format=%h%x09%an%x09%ar%x09%s", "-n", "10"}, code));
    CHECK(commits.size() == 1 && commits[0].message == "The first scene, it's here" && commits[0].author == "Test");
    CHECK(parseGitStatus(runGit(folder, {"status", "--porcelain=v1", "-uall"}, code)).empty());
}
