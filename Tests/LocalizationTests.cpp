// Languages: the interface's texts shown in another language, from a table per language.
#include "Test.hpp"

#include "Core/TextFile.hpp"
#include "Core/World.hpp"
#include "Kit/Kit.hpp"

#include <filesystem>

using namespace molten;

TEST(texts_are_shown_in_the_language_chosen) {
    std::string root = "/tmp/molten-test-languages";
    std::filesystem::create_directories(root + "/Localization");
    text::writeFile(root + "/Localization/it.txt", "# a comment with \"quotes\" = \"in it\"\n\"RACE\" = \"GARA\"\n\"BEST LAP   {best:time}\" = \"GIRO MIGLIORE   {best:time}\"\n"
                                                     "\"SUN\" = \"SOLE\"\n\"RAIN\" = \"PIOGGIA\"\n\"Two\\nlines\" = \"Due\\nrighe \\\"vere\\\"\"\n");
    text::writeFile(root + "/Localization/fr.txt", "\"RACE\" = \"COURSE\"\n");
    World world;
    world.assetRoot = root;
    CHECK(world.translate("RACE") == "RACE");                                   // no language: as written
    world.language = "it";
    CHECK(world.translate("RACE") == "GARA" && world.translate("CLOUDS") == "CLOUDS");
    CHECK(world.translate("Two\nlines") == "Due\nrighe \"vere\"");
    CHECK(world.translate("# a comment with \"quotes\"") == "# a comment with \"quotes\"");
    world.setValue("best", 75.5f);
    world.setValue("weather", 1);
    CHECK(kit::HudText::filled(world, "BEST LAP   {best:time}") == "GIRO MIGLIORE   1:15.5");
    CHECK(kit::HudText::filled(world, "{weather:SUN|RAIN}") == "PIOGGIA");
    world.language = "fr";
    CHECK(world.translate("RACE") == "COURSE");
    std::vector<std::string> offered = world.languages();
    CHECK(offered.size() == 2 && offered[0] == "fr" && offered[1] == "it");
}
