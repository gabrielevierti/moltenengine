// The command palette (Cmd+K): finding things by a few letters of their name.
#include "Test.hpp"

#include "../Editor/CommandPalette.hpp"

using namespace molten::editor;

TEST(the_palette_finds_by_a_few_letters) {
    CHECK(paletteScore("", "Save Scene") == 0);
    CHECK(paletteScore("svsc", "Save Scene") > 0);
    CHECK(paletteScore("scenesave", "Save Scene") == -1);        // in order, or not at all
    CHECK(paletteScore("SAVE", "save scene") > 0);               // case is not looked at
    // The start of the words counts: "ss" names "Save Scene" better than "Glass".
    CHECK(paletteScore("ss", "Save Scene") > paletteScore("ss", "Glass"));
    CHECK(paletteScore("car", "Car") > paletteScore("car", "Car Body Shadow"));
    CHECK(paletteScore("pl", "Point Light") > 0 && paletteScore("pl", "Play") > paletteScore("pl", "Point Light"));

    std::vector<PaletteCommand> commands = {{"Wheel FL", "Rival 1", "", true, {}, "Rival 1"},
                                            {"Wheel FL", "Rival 2", "", true, {}, "Rival 2"},
                                            {"Save Scene", "File", "Cmd+S", true, {}, ""},
                                            {"Wheels", "Car / Model", "", true, {}, "Car / Model"}};
    std::vector<size_t> found = rankPalette("rival 2 wheel", commands, 10);
    CHECK(found.size() == 1 && found[0] == 1);                   // the place it is in is searched too
    found = rankPalette("wheel", commands, 10);
    CHECK(found.size() == 3 && found[0] == 3);                   // the shortest name first, then the order given
    found = rankPalette("", commands, 2);
    CHECK(found.size() == 2 && found[0] == 0 && found[1] == 1);
}
