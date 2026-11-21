#include "Assets/BuildAssets.hpp"
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
    for (const std::string& directory : {project.sourceDirectory(), project.legacySourceDirectory()}) {
        code.clear();
        fs::recursive_directory_iterator it(directory, fs::directory_options::skip_permission_denied, code), end;
        for (; !code && it != end; it.increment(code)) {
            if (it->is_regular_file(code) && it->path().extension() == ".cpp") files.push_back(it->path().string());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

uint64_t gameSourceSignature(const Project& project) {
    uint64_t signature = 1469598103934665603ull;        // FNV-1a over names, sizes and times
    auto mix = [&signature](uint64_t value) {
        for (int i = 0; i < 8; i++) {
            signature ^= (value >> (i * 8)) & 0xFF;
            signature *= 1099511628211ull;
        }
    };
    std::error_code code;
    for (const std::string& directory : {project.sourceDirectory(), project.legacySourceDirectory()}) {
        code.clear();
        fs::recursive_directory_iterator it(directory, fs::directory_options::skip_permission_denied, code), end;
        for (; !code && it != end; it.increment(code)) {
            std::string extension = it->path().extension().string();
            if (!it->is_regular_file(code) || (extension != ".cpp" && extension != ".hpp" && extension != ".h")) continue;
            for (char c : it->path().string()) mix(uint64_t(static_cast<unsigned char>(c)));
            mix(uint64_t(it->file_size(code)));
            mix(uint64_t(it->last_write_time(code).time_since_epoch().count()));
        }
    }
    return signature;
}

std::string createScriptFile(const Project& project, const std::string& name, std::string* error) {
    auto fail = [error](const std::string& message) {
        if (error) *error = message;
        return std::string();
    };
    if (name.empty()) return fail("The script needs a name");
    for (size_t i = 0; i < name.size(); i++) {
        char c = name[i];
        bool isLetter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        bool isDigit = c >= '0' && c <= '9';
        if (!isLetter && !(isDigit && i > 0)) return fail("Use letters, digits and _ only, starting with a letter: the name becomes a C++ type");
    }
    std::error_code code;
    fs::create_directories(project.sourceDirectory(), code);
    fs::path file = fs::path(project.sourceDirectory()) / (name + ".cpp");
    if (fs::exists(file, code)) return fail(name + ".cpp already exists");

    std::string source =
        "#include \"MoltenMetal.hpp\"\n"
        "\n"
        "using namespace molten;\n"
        "\n"
        "// Add this component to an entity in the editor: Add Component > " + name + ".\n"
        "struct " + name + " {\n"
        "    // Fields listed at the bottom of this file are shown in the inspector and saved in scenes.\n"
        "    float speed = 1.0f;\n"
        "\n"
        "    // Once, when the game starts.\n"
        "    void start(Engine& engine, Entity self) {\n"
        "    }\n"
        "\n"
        "    // Every frame while the game plays. `dt` is the length of the frame in seconds.\n"
        "    void update(Engine& engine, Entity self, float dt) {\n"
        "        if (Transform* transform = engine.scene.get<Transform>(self)) {\n"
        "            // An example: W and S move the entity forward and back.\n"
        "            float direction = 0;\n"
        "            if (engine.input.isDown(Key::W)) direction += 1;\n"
        "            if (engine.input.isDown(Key::S)) direction -= 1;\n"
        "            transform->position += transform->forward() * (direction * speed * dt);\n"
        "        }\n"
        "    }\n"
        "};\n"
        "\n"
        "MOLTEN_COMPONENT(" + name + ", MOLTEN_FIELD(speed))\n";
    if (!text::writeFile(file.string(), source)) return fail("Cannot write " + file.string());
    return file.string();
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
    command += " -I " + quoted(project.sourceDirectory()) + " -I " + quoted(project.legacySourceDirectory());
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
                                 const std::string& engineRoot, std::string* applicationPath, bool isDevelopment) {
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
    // The project's assets, smaller where they can be: pictures no larger than a Mac shows, sounds
    // in 16 bits, the art programs' own files left behind.
    {
        ShrinkOptions shrink;
        shrink.largestPicture = 4096;
        copyAssetsSmaller(project.assetsDirectory(), (resources / "Project" / "Assets").string(), shrink);
    }
    fs::copy_file(project.filePath(), resources / "Project" / fs::path(project.filePath()).filename(), fs::copy_options::overwrite_existing, code);
    // A development build: the player sees this mark and turns on the developer keys (F1: statistics and profiler).
    if (isDevelopment) text::writeFile((resources / "Project" / "Development").string(), "F1 statistics and profiler, F2 colliders, F3 full screen, F4 screenshot\n");
    fs::path gameLibrary = fs::path(project.libraryDirectory()) / "Game" / "Game.dylib";
    if (fs::exists(gameLibrary, code)) fs::copy_file(gameLibrary, resources / "Game.dylib", fs::copy_options::overwrite_existing, code);

    // Its icon: the project's own picture or the engine's, in the sizes the Finder and the Dock
    // ask for, packed by the system's own tools. Without them the game still runs, with no icon in the Finder.
    bool hasIcon = false;
    {
        std::string picture = project.iconFile(engineRoot);
        fs::path iconset = fs::temp_directory_path() / "molten-icon.iconset";
        fs::remove_all(iconset, code);
        fs::create_directories(iconset, code);
        int status = 0;
        std::string commands;
        for (int size : {16, 32, 128, 256, 512}) {
            std::string s = std::to_string(size), twice = std::to_string(size * 2);
            commands += "sips -z " + s + " " + s + " " + (quoted)(picture) + " --out " + (quoted)((iconset / ("icon_" + s + "x" + s + ".png")).string()) + " >/dev/null && ";
            commands += "sips -z " + twice + " " + twice + " " + (quoted)(picture) + " --out " + (quoted)((iconset / ("icon_" + s + "x" + s + "@2x.png")).string()) + " >/dev/null && ";
        }
        commands += "iconutil -c icns " + (quoted)(iconset.string()) + " -o " + (quoted)((resources / "AppIcon.icns").string());
        if (fs::exists(picture, code)) {
            run(commands, status);
            hasIcon = status == 0 && fs::exists(resources / "AppIcon.icns", code);
        }
        fs::remove_all(iconset, code);
    }

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
        + std::string(hasIcon ? "  <key>CFBundleIconFile</key><string>AppIcon</string>\n" : "") +
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
