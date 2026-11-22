// The editor's commands: one list that the menus, the keys and the search are made from.
#include "Test.hpp"

#include "../Editor/Commands.hpp"
#include "../Editor/Shortcuts.hpp"

using namespace molten::editor;

TEST(a_menu_lists_its_commands_and_each_submenu_once) {
    Commands commands;
    int saved = 0;
    commands.add("save", "File/Save", [&] { saved++; });
    commands.line("File");
    commands.add("cube", "Entity/3D Object/Cube");
    commands.add("empty", "Entity/Create Empty");
    commands.add("sphere", "Entity/3D Object/Sphere");
    commands.add("quit", "File/Quit");

    std::vector<Commands::Row> file = commands.rowsOf("File");
    CHECK(file.size() == 3);
    CHECK(file[0].command && file[0].command->name() == "Save");
    CHECK(file[1].command && file[1].command->isLine());
    CHECK(file[2].command && file[2].command->id == "quit");

    // The submenu is one row, where its first command was said; its own rows are asked for by its path.
    std::vector<Commands::Row> entity = commands.rowsOf("Entity");
    CHECK(entity.size() == 2);
    CHECK(!entity[0].command && entity[0].submenu == "3D Object");
    CHECK(entity[1].command && entity[1].command->id == "empty");
    std::vector<Commands::Row> objects = commands.rowsOf("Entity/3D Object");
    CHECK(objects.size() == 2 && objects[0].command->name() == "Cube" && objects[1].command->name() == "Sphere");

    CHECK(commands.menus().size() == 2 && commands.menus()[0] == "File" && commands.menus()[1] == "Entity");
    CHECK(commands.find("cube")->where() == "Entity > 3D Object" && commands.find("save")->menu() == "File");
    CHECK(commands.run("save") && saved == 1);
    CHECK(!commands.run("nothing") && !commands.run("cube"));          // unknown, and one that does nothing
}

TEST(lines_are_only_between_things) {
    Commands commands;
    commands.line("Edit");
    commands.add("undo", "Edit/Undo");
    commands.line("Edit");
    commands.line("Edit");
    commands.add("copy", "Edit/Copy");
    commands.line("Edit");
    std::vector<Commands::Row> rows = commands.rowsOf("Edit");
    CHECK(rows.size() == 3);
    CHECK(rows[0].command->id == "undo" && rows[1].command->isLine() && rows[2].command->id == "copy");
    CHECK(commands.rowsOf("Nowhere").empty());
}

TEST(a_command_says_when_it_may_be_done_and_what_it_is_called_now) {
    Commands commands;
    bool playing = false, hasSelection = false;
    int deleted = 0;
    commands.add("play", "Edit/Play", [&] { playing = !playing; }).named([&] { return std::string(playing ? "Stop" : "Play"); }).inGame();
    commands.add("delete", "Edit/Delete", [&] { deleted++; }).when([&] { return hasSelection; }).onSelection();
    commands.add("grid", "View/Grid", [] {}).ticked([&] { return playing; }).shows("G");

    CHECK(commands.find("play")->name() == "Play");
    CHECK(commands.run("play") && commands.find("play")->name() == "Stop");
    CHECK(commands.find("play")->worksInGame && !commands.find("delete")->worksInGame);
    // Not while there is nothing to do it to; a key that would fight with typing says so.
    CHECK(!commands.run("delete") && deleted == 0);
    hasSelection = true;
    CHECK(commands.run("delete") && deleted == 1);
    CHECK(commands.find("delete")->usesEditKeys && commands.find("delete")->actsOnSelection);
    CHECK(commands.find("grid")->isChecked() && commands.find("grid")->keys == "G");
    // References handed out by add() stay good as more are added.
    Command& first = commands.add("first", "Tools/First");
    for (int i = 0; i < 200; i++) commands.add("more" + std::to_string(i), "Tools/More " + std::to_string(i));
    CHECK(first.id == "first" && commands.find("first") == &first);
}

TEST(every_shortcut_has_its_own_keys) {
    // Two commands on the same keys would both run: the defaults must not do that.
    Shortcuts shortcuts;
    for (const Shortcuts::Action& action : shortcuts.actions()) {
        CHECK(!action.id.empty() && !action.title.empty() && !action.group.empty());
        if (!shortcuts.sharing(action.id).empty()) std::printf("  %s shares its keys with %s\n", action.title.c_str(), shortcuts.sharing(action.id)[0].c_str());
        CHECK(shortcuts.sharing(action.id).empty());
    }
    CHECK(shortcuts.has("copy") && shortcuts.has("paste") && shortcuts.has("createEmpty") && !shortcuts.has("nothing"));
    CHECK(shortcuts.text("delete") == "Cmd+Backspace" && shortcuts.text("firstSibling") == "Cmd+=");
}
