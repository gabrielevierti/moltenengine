// DeviceExport.hpp
// From a project to a game on an iPhone or an iPad, from the editor: what used to be a line
// pasted into a terminal. It is three steps for programs that are not ours (CMake makes
// the Xcode project with the game in it, Xcode builds and signs the application, and the
// device takes it), so it runs beside the editor, which goes on working, and says what it
// is doing line by line.
#pragma once

#include "Project/Project.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace molten::editor {

class DeviceExport {
public:
    enum class Goal {
        OpenInXcode,        // make the Xcode project and open it: the device is chosen and Run is pressed there
        Install,            // build it, put it on the device that is plugged in and start it
        AppStore,           // archive it for release and send it to App Store Connect (TestFlight, then review)
    };

    ~DeviceExport();

    /// Starts the work. `team` is the Apple developer team that signs the application; it may
    /// be empty for OpenInXcode (it is then chosen in Xcode). False if one is already running.
    bool start(const Project& project, const std::string& engineRoot, const std::string& team, Goal goal);
    /// Runs any script the same way: beside the editor, its lines kept for takeLines().
    bool startScript(const std::string& text);
    /// A Mac application made ready for other Macs: signed with the Developer ID certificate in
    /// this Mac's keychain, sent to Apple to be notarized (with the credentials kept by
    /// `xcrun notarytool store-credentials <profile>`), and stapled.
    static std::string notarizeScript(const std::string& application, const std::string& profile);

    bool isRunning() const { return isRunning_; }
    /// True once a run has ended, until the next starts; `succeeded` says how.
    bool hasEnded() const { return hasEnded_; }
    bool succeeded() const { return succeeded_; }
    /// The lines the tools have said since this was last asked.
    std::vector<std::string> takeLines();
    /// Where the Xcode project is (made or to be made) for a project.
    static std::string buildDirectory(const Project& project) { return project.directory + "/Build/iOS"; }

    /// The script that is run: for tests, and for whoever wants to see what the button does.
    static std::string script(const Project& project, const std::string& engineRoot, const std::string& team, Goal goal);

private:
    std::thread worker_;
    std::mutex mutex_;
    std::vector<std::string> lines_;
    std::atomic<bool> isRunning_{false}, hasEnded_{false}, succeeded_{false};
};

}  // namespace molten::editor
