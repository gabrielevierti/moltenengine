// Layouts: pieces of interface placed one after the other, lists that scroll, sliders.
#include "Test.hpp"

#include "Core/World.hpp"
#include "Kit/Kit.hpp"
#include "Kit/Layout.hpp"
#include "Rendering/Canvas.hpp"

#include <cmath>
#include <filesystem>

using namespace molten;
using namespace molten::kit;

namespace {

const Vec2 kScreen{1280, 720};            // as other tests take the screen to be

Entity piece(World& world, Entity parent, const char* name, Vec2 size, float y) {
    Entity entity = world.scene.create(name);
    Transform transform;
    transform.parent = parent;
    world.scene.add(entity, transform);
    UIButton button;
    button.label = name;
    button.size = size;
    button.offset = Vec2{0, y};
    world.scene.add(entity, button);
    return entity;
}

Vec2 middleOf(World& world, Entity entity) {
    const UIButton* button = world.scene.get<UIButton>(entity);
    return placeOnScreen(kScreen, button->anchor, button->offset);
}

/// A frame of the game on a screen of kScreen: what is touched, then what is drawn.
void frame(World& world, Canvas& canvas) {
    world.interfaceReference = kScreen;         // drawn at its own size: points are pixels
    world.advance(1.0 / 60.0);
    canvas.begin(kScreen);
    world.drawComponents(canvas);
    world.input.endFrame();
}

}  // namespace

TEST(a_column_places_its_pieces_one_under_the_other) {
    World world;
    kit::registerKit();
    Entity menu = world.scene.create("Menu");
    world.scene.add(menu, Transform{});
    UILayout& column = world.scene.add<UILayout>(menu);
    column.size = Vec2{300, 400};
    column.padding = 10;
    column.spacing = 5;
    // Made in another order than they are on the screen: it is where they are that counts.
    Entity quit = piece(world, menu, "QUIT", Vec2{100, 40}, 300);
    Entity play = piece(world, menu, "PLAY", Vec2{200, 50}, -100);
    Entity settings = piece(world, menu, "SETTINGS", Vec2{150, 40}, 0);
    world.scene.updateTransforms();
    column.layOut(world, menu, kScreen);
    // The top of the column is at 360 - 200 + 10 = 170.
    CHECK(std::abs(middleOf(world, play).y - (170 + 25)) < 1e-3f);
    CHECK(std::abs(middleOf(world, settings).y - (170 + 50 + 5 + 20)) < 1e-3f);
    CHECK(std::abs(middleOf(world, quit).y - (170 + 50 + 5 + 40 + 5 + 20)) < 1e-3f);
    CHECK(std::abs(middleOf(world, play).x - 640) < 1e-3f);               // in the middle across
    // At the start across, and as wide as the column.
    column.alignment = 0;
    column.layOut(world, menu, kScreen);
    CHECK(std::abs(middleOf(world, quit).x - (490 + 10 + 50)) < 1e-3f);
    column.stretches = true;
    column.layOut(world, menu, kScreen);
    CHECK(std::abs(world.scene.get<UIButton>(quit)->size.x - 280) < 1e-3f);
    // As long as what it holds.
    column.fitsContent = true;
    column.layOut(world, menu, kScreen);
    CHECK(std::abs(column.size.y - (50 + 40 + 40 + 2 * 5 + 2 * 10)) < 1e-3f);
    // A piece dragged above another (its offset moved) takes its place.
    world.scene.get<UIButton>(quit)->offset.y -= 200;
    column.layOut(world, menu, kScreen);
    CHECK(middleOf(world, quit).y < middleOf(world, play).y);
    // Switched off: no room is kept for it.
    world.scene.get<Transform>(quit)->isActive = false;
    column.layOut(world, menu, kScreen);
    CHECK(std::abs(column.size.y - (50 + 40 + 5 + 2 * 10)) < 1e-3f);
}

TEST(a_row_and_a_grid) {
    World world;
    kit::registerKit();
    Entity bar = world.scene.create("Bar");
    world.scene.add(bar, Transform{});
    UILayout& row = world.scene.add<UILayout>(bar);
    row.arrangement = 0;
    row.size = Vec2{500, 80};
    row.padding = 0;
    row.spacing = 10;
    row.alignment = 0;
    Entity a = piece(world, bar, "A", Vec2{100, 40}, 0), b = piece(world, bar, "B", Vec2{100, 40}, 0);
    world.scene.get<UIButton>(b)->offset.x = 50;
    world.scene.updateTransforms();
    row.layOut(world, bar, kScreen);
    CHECK(std::abs(middleOf(world, a).x - 440) < 1e-3f && std::abs(middleOf(world, b).x - 550) < 1e-3f);
    CHECK(std::abs(middleOf(world, a).y - (320 + 20)) < 1e-3f);           // at the top

    Entity levels = world.scene.create("Levels");
    world.scene.add(levels, Transform{});
    UILayout& grid = world.scene.add<UILayout>(levels);
    grid.arrangement = 2;
    grid.columns = 3;
    grid.size = Vec2{320, 400};
    grid.padding = 10;
    grid.spacing = 0;
    grid.stretches = true;
    std::vector<Entity> cells;
    for (int k = 0; k < 7; k++) cells.push_back(piece(world, levels, "L", Vec2{50, 60}, float(k) * 10));
    world.scene.updateTransforms();
    grid.layOut(world, levels, kScreen);
    // Three to a row, each a third of 300 wide: the fourth starts the second row.
    CHECK(std::abs(world.scene.get<UIButton>(cells[0])->size.x - 100) < 1e-3f);
    CHECK(std::abs(middleOf(world, cells[1]).x - (490 + 100 + 50)) < 1e-3f);
    CHECK(std::abs(middleOf(world, cells[3]).y - middleOf(world, cells[0]).y - 60) < 1e-3f);
    CHECK(std::abs(middleOf(world, cells[3]).x - middleOf(world, cells[0]).x) < 1e-3f);
    // Laid out again, they stay where they are: their order is read from where they are.
    std::vector<Vec2> before;
    for (Entity cell : cells) before.push_back(middleOf(world, cell));
    grid.layOut(world, levels, kScreen);
    for (size_t k = 0; k < cells.size(); k++) CHECK(simd::length(middleOf(world, cells[k]) - before[k]) < 1e-3f);
}

TEST(a_list_scrolls_and_only_what_is_in_it_is_touched) {
    World world;
    kit::registerKit();
    Canvas canvas;
    Entity list = world.scene.create("List");
    world.scene.add(list, Transform{});
    UILayout layout;
    layout.size = Vec2{300, 200};
    layout.padding = 0;
    layout.spacing = 0;
    layout.scrolls = true;
    world.scene.add(list, layout);
    std::vector<Entity> rows;
    for (int k = 0; k < 10; k++) rows.push_back(piece(world, list, "Row", Vec2{300, 50}, float(k)));
    world.scene.updateTransforms();
    frame(world, canvas);
    UILayout* placed = world.scene.get<UILayout>(list);
    CHECK(std::abs(placed->contentLength - 500) < 1e-3f);
    CHECK(std::abs(middleOf(world, rows[0]).y - 285) < 1e-3f);            // the list's top is at 260
    // The fifth row is below the list's bottom (460): pressing there does not click it.
    auto click = [&](Vec2 at) {
        world.input.handleMouseMove(at, at - world.input.mousePosition());
        world.input.handleMouseButton(MouseButton::Left, true);
        frame(world, canvas);
        world.input.handleMouseButton(MouseButton::Left, false);
        world.advance(1.0 / 60.0);
    };
    click(Vec2{640, 485});
    CHECK(!world.scene.get<UIButton>(rows[4])->isHeld && !world.scene.get<UIButton>(rows[4])->wasClicked);
    click(Vec2{640, 285});
    CHECK(world.scene.get<UIButton>(rows[0])->wasClicked);
    // Dragged up by 120 points, the rows follow; never past the end.
    Vec2 at{640, 450};
    world.input.handleMouseMove(at, at - world.input.mousePosition());
    world.input.handleMouseButton(MouseButton::Left, true);
    frame(world, canvas);
    for (int k = 1; k <= 4; k++) {
        Vec2 to = at - Vec2{0, 30.0f * float(k)};
        world.input.handleMouseMove(to, to - world.input.mousePosition());
        frame(world, canvas);
    }
    CHECK(std::abs(placed->scrolled - 120) < 1e-3f);
    CHECK(std::abs(middleOf(world, rows[0]).y - (285 - 120)) < 1e-3f);
    for (int k = 1; k <= 10; k++) {
        Vec2 to = at - Vec2{0, 120 + 60.0f * float(k)};
        world.input.handleMouseMove(to, to - world.input.mousePosition());
        frame(world, canvas);
    }
    world.input.handleMouseButton(MouseButton::Left, false);
    frame(world, canvas);
    CHECK(std::abs(placed->scrolled - 300) < 1e-3f);                       // 500 of rows in 200 of list
    // The wheel, over the list, brings it back.
    world.input.handleMouseMove(Vec2{640, 360}, Vec2{640, 360} - world.input.mousePosition());
    world.input.handleScroll(10);
    frame(world, canvas);
    CHECK(placed->scrolled == 0);
    // What is in it is drawn only inside it.
    Vec2 min, max;
    CHECK(interfaceClip(world, rows[9], kScreen, min, max));
    CHECK(std::abs(min.y - 260) < 1e-3f && std::abs(max.y - 460) < 1e-3f);
    CHECK(!interfaceClip(world, list, kScreen, min, max));                  // the list itself is in none
}

TEST(a_slider_sets_a_value_and_keeps_it) {
    World world;
    kit::registerKit();
    Canvas canvas;
    Entity entity = world.scene.create("Volume");
    world.scene.add(entity, Transform{});
    UISlider slider;
    slider.label = "";
    slider.showsNumber = false;
    slider.size = Vec2{228, 40};                // the bar from 14 to 214: 200 points
    slider.least = 0;
    slider.most = 10;
    slider.step = 1;
    slider.isSaved = true;
    world.scene.add(entity, slider);
    frame(world, canvas);
    CHECK(world.value("volume") == 0);
    // The bar starts at 640 - 114 + 14 = 540: pressing at 610 is 35% of it, 3.5, made 4 by the step.
    Vec2 at{610, 360};
    world.input.handleMouseMove(at, at - world.input.mousePosition());
    world.input.handleMouseButton(MouseButton::Left, true);
    frame(world, canvas);
    CHECK(world.value("volume") == 4);
    Vec2 to{1200, 360};                          // dragged far past its end: the most
    world.input.handleMouseMove(to, to - world.input.mousePosition());
    frame(world, canvas);
    CHECK(world.value("volume") == 10 && world.preference("volume") == 10);
    world.input.handleMouseButton(MouseButton::Left, false);
    frame(world, canvas);
    CHECK(!world.scene.get<UISlider>(entity)->isHeld);
}

TEST(a_text_field_takes_what_is_typed) {
    CHECK(UITextField::typedInto("ab", "c\rd\t", 0) == "abcd");               // no Return, no Tab
    CHECK(UITextField::typedInto("ab", "cdef", 4) == "abcd");
    CHECK(UITextField::typedInto("é", "èà", 2) == "éè");                        // letters, not bytes
    CHECK(UITextField::withoutLast("caffè") == "caff" && UITextField::withoutLast("") == "");

    std::string preferences = "/tmp/molten-test-textfield/preferences.txt";
    std::filesystem::remove_all("/tmp/molten-test-textfield");
    World world;
    kit::registerKit();
    world.preferencesFile = preferences;
    Canvas canvas;
    Entity entity = world.scene.create("Name");
    world.scene.add(entity, Transform{});
    UITextField field;
    field.maxLength = 8;
    field.isSaved = true;
    world.scene.add(entity, field);
    Entity greeting = world.scene.create("Greeting");
    world.scene.add(greeting, Transform{});
    world.scene.add(greeting, HudText{"Hello {playerName}"});
    frame(world, canvas);
    UITextField* placed = world.scene.get<UITextField>(entity);
    // Typing before it is clicked does nothing.
    world.input.handleText('x');
    frame(world, canvas);
    CHECK(world.text("playerName").empty() && !placed->isFocused);
    Vec2 at{640, 360};
    world.input.handleMouseMove(at, at - world.input.mousePosition());
    world.input.handleMouseButton(MouseButton::Left, true);
    frame(world, canvas);
    world.input.handleMouseButton(MouseButton::Left, false);
    CHECK(placed->isFocused);
    for (char c : std::string("Gabriele V")) world.input.handleText(uint32_t(c));
    frame(world, canvas);
    CHECK(world.text("playerName") == "Gabriele");                              // eight letters at most
    CHECK(HudText::filled(world, "Hello {playerName}") == "Hello Gabriele");
    world.input.handleKey(Key::Backspace, true);
    frame(world, canvas);
    CHECK(world.text("playerName") == "Gabriel");
    for (int i = 0; i < 30; i++) frame(world, canvas);                          // held half a second: it goes on
    world.input.handleKey(Key::Backspace, false);
    frame(world, canvas);
    CHECK(world.text("playerName").size() < 7);
    world.input.handleText('G');
    world.input.handleKey(Key::Enter, true);
    world.advance(1.0 / 60.0);
    CHECK(placed->wasSubmitted && !placed->isFocused);
    world.input.endFrame();
    world.input.handleKey(Key::Enter, false);
    // Kept between plays.
    std::string typed = world.text("playerName");
    World again;
    again.preferencesFile = preferences;
    CHECK(again.textPreference("playerName") == typed && !typed.empty());
}
