// Saved games: values by name, written to files of their own and read back.
#include "Test.hpp"

#include "Core/World.hpp"

#include <filesystem>

using namespace molten;

TEST(a_save_game_keeps_numbers_words_and_positions) {
    SaveGame save;
    save.setNumber("level", 3);
    save.setNumber("best lap", 41.25f);
    save.setText("name", "Ann \"the fast\"\nsecond line");
    save.setVector("player", Vec3{1.5f, -2, 300});
    SaveGame back = SaveGame::fromText(save.toText());
    CHECK(back.number("level") == 3 && back.number("best lap") == 41.25f);
    CHECK(back.text("name") == "Ann \"the fast\"\nsecond line");
    CHECK(back.vector("player").x == 1.5f && back.vector("player").z == 300);
    CHECK(back.number("missing", 7) == 7 && back.text("level", "no") == "no");       // a number is not a text
    CHECK(back.names().size() == 4);
    back.remove("level");
    CHECK(!back.has("level"));
}

TEST(saved_games_are_files_beside_the_preferences) {
    std::string folder = "/tmp/molten-test-saves";
    std::filesystem::remove_all(folder);
    World world;
    world.preferencesFile = folder + "/Preferences.txt";
    CHECK(!world.hasSavedGame() && !world.loadGame());
    SaveGame save;
    save.setNumber("coins", 12);
    CHECK(world.saveGame(save));
    CHECK(world.saveGame(save, "Slot 2/../x"));                       // made a safe file name
    CHECK(std::filesystem::exists(folder + "/Saves/Save.save"));
    CHECK(world.hasSavedGame() && world.loadGame()->number("coins") == 12);
    CHECK(world.savedGames().size() == 2);
    CHECK(world.deleteSavedGame() && !world.hasSavedGame());

    World alone;                                                      // no file: kept for the run
    alone.saveGame(save, "A");
    CHECK(alone.loadGame("A") && alone.loadGame("A")->number("coins") == 12 && alone.savedGames().size() == 1);
}
