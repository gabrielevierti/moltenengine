#include "GameBuild.hpp"

#include "Core/TextFile.hpp"

#include <mach-o/dyld.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>

// Where the engine's headers are, as CMake knew them when the editor was built: the
// folders a game's code is compiled against, separated by '|'.
#ifndef MOLTEN_GAME_INCLUDE_DIRS
#define MOLTEN_GAME_INCLUDE_DIRS ""
#endif

namespace molten::editor {

namespace fs = std::filesystem;

namespace {

/// A path as one word of a shell command.
std::string quoted(const std::string& path) {
    std::string out = "'";
    for (char c : path) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
}

/// Runs a command and returns what it printed, errors included. `exitCode` is 0 on success.
std::string run(const std::string& command, int& exitCode) {
    std::string output;
    FILE* pipe = popen((command + " 2>&1").c_str(), "r");
    if (!pipe) {
        exitCode = -1;
        return "cannot start: " + command;
    }
    char buffer[4096];
    while (size_t count = fread(buffer, 1, sizeof(buffer), pipe)) output.append(buffer, count);
    exitCode = pclose(pipe);
    return output;
}

}  // namespace

std::string currentExecutablePath() {
    char buffer[4096];
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) != 0) return {};
    std::error_code code;
    fs::path resolved = fs::canonical(fs::path(buffer), code);
    return code ? std::string(buffer) : resolved.string();
}

std::vector<std::string> gameSourceFiles(const Project& project) {
    std::vector<std::string> files;
    std::error_code code;
    fs::recursive_directory_iterator it(project.sourceDirectory(), fs::directory_options::skip_permission_denied, code), end;
    for (; !code && it != end; it.increment(code)) {
        if (it->is_regular_file(code) && it->path().extension() == ".cpp") files.push_back(it->path().string());
    }
    std::sort(files.begin(), files.end());
    return files;
}

BuildResult buildGameCode(const Project& project, const std::string& hostExecutable) {
    BuildResult result;
    std::vector<std::string> sources = gameSourceFiles(project);
    result.hadSources = !sources.empty();
    if (sources.empty()) return result;

    std::error_code code;
    fs::path folder = fs::path(project.libraryDirectory()) / "Game";
    fs::create_directories(folder, code);
    // The next number after the builds that are there.
    int number = 1;
    for (const auto& entry : fs::directory_iterator(folder, code)) {
        std::string name = entry.path().stem().string();       // "Game-12"
        if (name.rfind("Game-", 0) == 0) number = std::max(number, std::atoi(name.c_str() + 5) + 1);
    }
    fs::path library = folder / ("Game-" + std::to_string(number) + ".dylib");

    // A "bundle" is a library made to be loaded into one particular program: what the code
    // uses of the engine is looked up in that program when it is loaded, not copied in.
    std::string command = "xcrun clang++ -std=c++20 -g -O1 -mmacosx-version-min=13.0 -bundle -bundle_loader " + quoted(hostExecutable);
    command += " -I " + quoted(project.sourceDirectory());
    std::string includes = MOLTEN_GAME_INCLUDE_DIRS;
    size_t start = 0;
    while (start < includes.size()) {
        size_t stop = includes.find('|', start);
        if (stop == std::string::npos) stop = includes.size();
        if (stop > start) command += " -isystem " + quoted(includes.substr(start, stop - start));
        start = stop + 1;
    }
    for (const std::string& source : sources) command += " " + quoted(source);
    command += " -o " + quoted(library.string());

    auto began = std::chrono::steady_clock::now();
    int exitCode = 0;
    result.log = run(command, exitCode);
    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    result.succeeded = exitCode == 0 && fs::exists(library, code);
    if (!result.succeeded) return result;
    result.libraryPath = library.string();

    fs::copy_file(library, folder / "Game.dylib", fs::copy_options::overwrite_existing, code);
    // Older builds are no longer needed on disk (the ones still loaded stay in memory). Keep the last few.
    for (int old = number - 3; old >= 1 && old > number - 40; old--) {
        fs::remove(folder / ("Game-" + std::to_string(old) + ".dylib"), code);
        fs::remove_all(folder / ("Game-" + std::to_string(old) + ".dylib.dSYM"), code);
    }
    return result;
}

std::string exportMacApplication(const Project& project, const std::string& destination, const std::string& playerExecutable,
                                 const std::string& engineRoot, std::string* applicationPath) {
    std::error_code code;
    if (!fs::exists(playerExecutable, code)) return "The player was not found at " + playerExecutable + ". Build the target molten-player.";
    std::string name = Project::fileNameFor(project.name);
    fs::path app = fs::path(destination) / (name + ".app");
    if (fs::exists(app, code)) return app.string() + " already exists. Move it away or choose another folder.";

    fs::path contents = app / "Contents", macos = contents / "MacOS", resources = contents / "Resources";
    fs::create_directories(macos, code);
    fs::create_directories(resources, code);
    if (code) return "Cannot create " + app.string() + ": " + code.message();

    auto copyTree = [&](const fs::path& from, const fs::path& to) {
        if (!fs::exists(from, code)) return;
        fs::create_directories(to.parent_path(), code);
        fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, code);
    };
    fs::copy_file(playerExecutable, macos / name, fs::copy_options::overwrite_existing, code);
    if (code) return "Cannot copy the player: " + code.message();
    // The engine's own shaders and assets, then the project on top: a project may replace an engine asset.
    copyTree(fs::path(engineRoot) / "Shaders", resources / "Shaders");
    copyTree(fs::path(engineRoot) / "Assets", resources / "Assets");
    copyTree(project.assetsDirectory(), resources / "Project" / "Assets");
    fs::copy_file(project.filePath(), resources / "Project" / fs::path(project.filePath()).filename(), fs::copy_options::overwrite_existing, code);
    fs::path gameLibrary = fs::path(project.libraryDirectory()) / "Game" / "Game.dylib";
    if (fs::exists(gameLibrary, code)) fs::copy_file(gameLibrary, resources / "Game.dylib", fs::copy_options::overwrite_existing, code);

    std::string identifier = project.identifier.empty() ? "com.example." + name : project.identifier;
    std::string plist =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n<dict>\n"
        "  <key>CFBundleExecutable</key><string>" + name + "</string>\n"
        "  <key>CFBundleIdentifier</key><string>" + identifier + "</string>\n"
        "  <key>CFBundleName</key><string>" + name + "</string>\n"
        "  <key>CFBundlePackageType</key><string>APPL</string>\n"
        "  <key>CFBundleShortVersionString</key><string>" + project.version + "</string>\n"
        "  <key>CFBundleVersion</key><string>1</string>\n"
        "  <key>LSMinimumSystemVersion</key><string>13.0</string>\n"
        "  <key>NSHighResolutionCapable</key><true/>\n"
        "  <key>LSApplicationCategoryType</key><string>public.app-category.games</string>\n"
        "</dict>\n</plist>\n";
    if (!text::writeFile((contents / "Info.plist").string(), plist)) return "Cannot write Info.plist";

    // Signed for this Mac only ("ad hoc"): enough to run here. Giving it to others needs a
    // Developer ID signature and notarization, which is a step for the developer's own account.
    int exitCode = 0;
    std::string signing = run("codesign --force --deep --sign - " + quoted(app.string()), exitCode);
    if (applicationPath) *applicationPath = app.string();
    return exitCode == 0 ? std::string() : "The application was made but could not be signed: " + signing;
}

}  // namespace molten::editor
