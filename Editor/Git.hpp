// Git.hpp
// The project's history in Git, from inside the editor (Window > Source Control): what has
// changed, a commit, the commits before, push and pull. It runs the git the Mac has.
#pragma once

#include <string>
#include <vector>

namespace molten::editor {

struct GitChange {
    std::string path;                   // relative to the repository's folder
    char staged = ' ', unstaged = ' ';  // as "git status --porcelain" writes them: M, A, D, R, ?
    bool isNew() const { return staged == '?' || staged == 'A'; }
    bool isDeleted() const { return staged == 'D' || unstaged == 'D'; }
    /// In words: "changed", "new", "deleted", "renamed".
    const char* kind() const;
};

struct GitCommit {
    std::string hash, author, when, message;
};

/// What "git status --porcelain=v1 -uall" printed, as changes. Paths in quotes are read whole;
/// a rename is its new path.
std::vector<GitChange> parseGitStatus(const std::string& output);
/// What "git log --format=%h%x09%an%x09%ar%x09%s" printed.
std::vector<GitCommit> parseGitLog(const std::string& output);

/// Runs git in `folder` with these arguments (each quoted for the shell); what it printed,
/// errors included. `exitCode` is 0 when it worked. It never waits for a password.
std::string runGit(const std::string& folder, const std::vector<std::string>& arguments, int& exitCode);

/// What a project new to Git leaves out of it: what the editor makes again by itself.
const char* projectGitIgnore();

}  // namespace molten::editor
