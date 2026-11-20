// DeviceExport.cpp
#include "DeviceExport.hpp"

#include <cctype>
#include <cstdio>

namespace molten::editor {

namespace {

/// A piece of text as one word of a shell command, whatever is in it.
std::string quoted(const std::string& text) {
    std::string out = "'";
    for (char c : text) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
}

/// "My Game" -> "com.example.mygame": an identifier for a project that has not been given one.
std::string identifierFor(const Project& project) {
    if (!project.identifier.empty()) return project.identifier;
    std::string name;
    for (char c : project.name) {
        if (std::isalnum(static_cast<unsigned char>(c))) name += char(std::tolower(static_cast<unsigned char>(c)));
    }
    return "com.example." + (name.empty() ? std::string("game") : name);
}

}  // namespace

std::string DeviceExport::script(const Project& project, const std::string& engineRoot, const std::string& team, Goal goal) {
    std::string build = buildDirectory(project);
    std::string name = project.name.empty() ? std::string("Game") : project.name;
    std::string identifier = identifierFor(project);
    std::string s;
    // The editor may have been started from the Finder, where the programs installed with
    // Homebrew or with CMake's own installer are not on the path.
    s += "export PATH=\"/opt/homebrew/bin:/usr/local/bin:/Applications/CMake.app/Contents/bin:$PATH\"\n";
    s += "set -o pipefail\n";
    s += "BUILD=" + quoted(build) + "\n";
    s += "if ! command -v cmake >/dev/null; then echo 'error: CMake was not found. Install it (brew install cmake) and try again.'; exit 1; fi\n";
    s += "if ! xcode-select -p >/dev/null 2>&1 || ! command -v xcodebuild >/dev/null; then echo 'error: Xcode was not found. Install it from the App Store and open it once.'; exit 1; fi\n";
    s += "echo '== Making the Xcode project'\n";
    s += "cmake -S " + quoted(engineRoot) + " -B \"$BUILD\" -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DMOLTEN_IOS_PROJECT=" + quoted(project.directory) +
         " -DMOLTEN_IOS_BUNDLE_ID=" + quoted(identifier) + " -DMOLTEN_IOS_NAME=" + quoted(name) + " -DMOLTEN_IOS_TEAM=" + quoted(team) + " -DMOLTEN_IOS_ICON=" + quoted(project.iconFile(engineRoot)) + " || exit 1\n";
    if (goal == Goal::OpenInXcode) {
        s += "echo '== Opening it in Xcode: choose the MoltenPlayer scheme and your device, and press Run'\n";
        s += "open \"$BUILD/MoltenMetalEngine.xcodeproj\" || exit 1\n";
        return s;
    }
    s += "echo '== Building the application (the first time takes a few minutes)'\n";
    s += "xcodebuild -project \"$BUILD/MoltenMetalEngine.xcodeproj\" -scheme MoltenPlayer -configuration Release -destination 'generic/platform=iOS' "
         "-allowProvisioningUpdates build 2>&1 | grep -E --line-buffered 'error|warning: |BUILD|Signing|Provisioning|^===' ; BUILT=${pipestatus[1]:-${PIPESTATUS[0]}}\n";
    s += "if [ \"$BUILT\" != 0 ]; then echo 'error: Xcode could not build the application. Opening the project in Xcode shows why.'; exit 1; fi\n";
    s += "APP=\"$BUILD/Release-iphoneos/\"" + quoted(name + ".app") + "\n";
    s += "if [ ! -d \"$APP\" ]; then APP=$(find \"$BUILD\" -maxdepth 3 -name '*.app' -path '*iphoneos*' | head -1); fi\n";
    s += "if [ ! -d \"$APP\" ]; then echo 'error: the application was built but not found.'; exit 1; fi\n";
    s += "echo '== Looking for a device'\n";
    s += "xcrun devicectl list devices --json-output \"$BUILD/devices.json\" >/dev/null 2>&1\n";
    s += "DEVICE=$(/usr/bin/python3 -c 'import json,sys\n"
         "try:\n"
         "    devices = json.load(open(sys.argv[1]))[\"result\"][\"devices\"]\n"
         "except Exception:\n"
         "    devices = []\n"
         "ready = [d for d in devices if d.get(\"connectionProperties\", {}).get(\"tunnelState\") in (\"connected\", \"disconnected\") and d.get(\"hardwareProperties\", {}).get(\"platform\") in (\"iOS\", None)]\n"
         "ready.sort(key=lambda d: d.get(\"connectionProperties\", {}).get(\"tunnelState\") != \"connected\")\n"
         "print(ready[0][\"identifier\"] if ready else \"\")' \"$BUILD/devices.json\")\n";
    s += "if [ -z \"$DEVICE\" ]; then echo 'error: no iPhone or iPad was found. Plug one in, unlock it and trust this Mac, then try again.'; exit 1; fi\n";
    s += "echo '== Putting it on the device'\n";
    s += "xcrun devicectl device install app --device \"$DEVICE\" \"$APP\" || exit 1\n";
    s += "echo '== Starting it'\n";
    s += "xcrun devicectl device process launch --device \"$DEVICE\" " + quoted(identifier) +
         " || echo 'It is on the device: start it there. (The first time, trust the developer in Settings > General > VPN & Device Management.)'\n";
    s += "echo '== Done'\n";
    return s;
}

DeviceExport::~DeviceExport() {
    if (worker_.joinable()) worker_.join();
}

bool DeviceExport::start(const Project& project, const std::string& engineRoot, const std::string& team, Goal goal) {
    if (isRunning_) return false;
    if (worker_.joinable()) worker_.join();
    std::string text = script(project, engineRoot, team, goal);
    isRunning_ = true;
    hasEnded_ = false;
    succeeded_ = false;
    worker_ = std::thread([this, text] {
        // A login shell, so that the user's own path (Homebrew, their profile) is in force.
        std::string command = "/bin/zsh -lc " + quoted(text) + " 2>&1";
        FILE* pipe = popen(command.c_str(), "r");
        bool worked = false;
        if (pipe) {
            char buffer[2048];
            while (fgets(buffer, sizeof(buffer), pipe)) {
                std::string line(buffer);
                while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
                if (line.empty()) continue;
                std::lock_guard<std::mutex> lock(mutex_);
                lines_.push_back(line);
            }
            worked = pclose(pipe) == 0;
        } else {
            std::lock_guard<std::mutex> lock(mutex_);
            lines_.push_back("error: the shell could not be started");
        }
        succeeded_ = worked;
        hasEnded_ = true;
        isRunning_ = false;
    });
    return true;
}

std::vector<std::string> DeviceExport::takeLines() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> taken;
    taken.swap(lines_);
    return taken;
}

}  // namespace molten::editor
