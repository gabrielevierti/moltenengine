// Git.cpp
#include "Git.hpp"

#include <cstdio>
#include <sstream>

namespace molten::editor {

const char* GitChange::kind() const {
    if (staged == '?') return "new";
    if (staged == 'A') return "added";
    if (isDeleted()) return "deleted";
    if (staged == 'R') return "renamed";
    if (staged == 'U' || unstaged == 'U') return "in conflict";
    return "changed";
}

namespace {

/// A path as git writes it: in quotes, with \\ escapes, when it has spaces or odd letters.
std::string unquoted(const std::string& text) {
    if (text.size() < 2 || text.front() != '"' || text.back() != '"') return text;
    std::string out;
    for (size_t i = 1; i + 1 < text.size(); i++) {
        if (text[i] == '\\' && i + 2 < text.size()) {
            char next = text[++i];
            if (next >= '0' && next <= '7' && i + 2 < text.size()) {        // a byte in octal: letters beyond ASCII
                int value = (next - '0') * 64 + (text[i + 1] - '0') * 8 + (text[i + 2] - '0');
                out += char(value);
                i += 2;
            } else {
                out += next == 'n' ? '\n' : next == 't' ? '\t' : next;
            }
        } else {
            out += text[i];
        }
    }
    return out;
}

std::string shellQuoted(const std::string& text) {
    std::string out = "'";
    for (char c : text) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
}

}  // namespace

std::vector<GitChange> parseGitStatus(const std::string& output) {
    std::vector<GitChange> changes;
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.size() < 4 || line[2] != ' ') continue;
        GitChange change;
        change.staged = line[0];
        change.unstaged = line[1];
        std::string path = line.substr(3);
        size_t arrow = path.find(" -> ");                   // a rename: "old -> new"
        if (arrow != std::string::npos) path = path.substr(arrow + 4);
        change.path = unquoted(path);
        changes.push_back(change);
    }
    return changes;
}

std::vector<GitCommit> parseGitLog(const std::string& output) {
    std::vector<GitCommit> commits;
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        std::vector<std::string> parts;
        size_t start = 0;
        for (int k = 0; k < 3; k++) {
            size_t tab = line.find('\t', start);
            if (tab == std::string::npos) break;
            parts.push_back(line.substr(start, tab - start));
            start = tab + 1;
        }
        if (parts.size() < 3) continue;
        commits.push_back(GitCommit{parts[0], parts[1], parts[2], line.substr(start)});
    }
    return commits;
}

std::string runGit(const std::string& folder, const std::vector<std::string>& arguments, int& exitCode) {
    // No questions on a terminal nobody sees: a push that needs a password fails and says so.
    std::string command = "cd " + shellQuoted(folder) + " && GIT_TERMINAL_PROMPT=0 GIT_ASKPASS=true git";
    for (const std::string& argument : arguments) command += " " + shellQuoted(argument);
    command += " 2>&1";
    std::string output;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        exitCode = -1;
        return "cannot run git";
    }
    char buffer[4096];
    while (size_t count = fread(buffer, 1, sizeof(buffer), pipe)) output.append(buffer, count);
    exitCode = pclose(pipe);
    return output;
}

const char* projectGitIgnore() {
    return "# Made again by the editor: not part of the project's history.\n"
           "Library/\n"
           "Build/\n"
           ".DS_Store\n";
}

}  // namespace molten::editor
