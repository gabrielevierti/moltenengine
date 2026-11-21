// Files written while the editor runs are found, once, when they are left alone.
#include "Test.hpp"

#include "Assets/FileChanges.hpp"

using namespace molten;

TEST(files_written_again_are_found_once_they_settle) {
    FileChanges changes;
    std::vector<std::pair<std::string, int64_t>> files = {{"Textures/wood.png", 100}, {"Models/car.glb", 200}};
    CHECK(changes.look(files, 0.0).empty());                    // the first look only learns them
    CHECK(changes.look(files, 1.0).empty());                    // nothing written
    files[0].second = 150;                                       // saved in another program
    CHECK(changes.look(files, 2.0).empty());                    // just written: it may not be finished
    files[0].second = 160;                                       // still being written
    CHECK(changes.look(files, 2.2).empty());
    std::vector<std::string> found = changes.look(files, 2.7);
    CHECK(found.size() == 1 && found[0] == "Textures/wood.png");
    CHECK(changes.look(files, 3.5).empty());                    // told once
    files.push_back({"Textures/new.png", 300});                  // a new file is not a change
    CHECK(changes.look(files, 4.0).empty() && changes.look(files, 5.0).empty());
    files[1].second = 210;
    changes.look(files, 6.0);
    found = changes.look(files, 7.0);
    CHECK(found.size() == 1 && found[0] == "Models/car.glb");
}
