// FileChanges.hpp
// Which files were written since the last look: what is read again while the game or the
// editor runs, when a picture is saved in another program or a model is exported again.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace molten {

class FileChanges {
public:
    /// One look at the files (their names and when each was last written), at `time` seconds.
    /// Gives back those written since the look before, once each has been left alone for
    /// `settle` seconds: a program that writes a file in several goes is not caught halfway.
    /// The first look only learns the files; a file seen for the first time is new, not changed.
    std::vector<std::string> look(const std::vector<std::pair<std::string, int64_t>>& files, double time, double settle = 0.4) {
        std::vector<std::string> changed;
        for (const auto& [name, written] : files) {
            auto known = known_.find(name);
            if (known == known_.end()) {
                known_[name] = Known{written, written, -1};
                continue;
            }
            Known& file = known->second;
            if (written != file.seen) {
                file.seen = written;
                file.since = time;                  // written again: wait until it is left alone
            }
            if (file.seen != file.reported && file.since >= 0 && time - file.since >= settle) {
                file.reported = file.seen;
                file.since = -1;
                changed.push_back(name);
            }
        }
        return changed;
    }
    void forget() { known_.clear(); }

private:
    struct Known {
        int64_t seen = 0;                           // when it was written, the last time it was looked at
        int64_t reported = 0;                       // the last writing that was told about
        double since = -1;                          // when `seen` was first seen
    };
    std::unordered_map<std::string, Known> known_;
};

}  // namespace molten
