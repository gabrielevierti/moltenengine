// Layout.hpp
// Pieces of interface arranged for you: a column of buttons, a row of icons, a grid of
// levels, a list longer than the screen that scrolls. And a slider.
//
// A UILayout places the pieces of interface under it in the Hierarchy (its children), one
// after the other, in the order they are on the screen: drag one past another in the Game
// view and they change places. It does this by setting their offsets, so everything that
// knows where a piece is (its drawing, the clicks on it, the editor) needs nothing else.
#pragma once

#include "Assets/Font.hpp"
#include "Core/World.hpp"

#include <string>

namespace molten::kit {

struct UILayout {
    int arrangement = 1;                // 0 a row, 1 a column, 2 a grid of `columns`
    int columns = 3;
    Vec2 anchor{0.5f, 0.5f};
    Vec2 offset{0, 0};
    Vec2 size{360, 420};
    float padding = 16.0f;              // between its edge and what it holds
    float spacing = 12.0f;              // between two of them
    int alignment = 1;                  // across the way they go: 0 the start (left, top), 1 the middle, 2 the end
    bool stretches = false;             // the pieces as wide as the column (as tall as the row, as the cell of the grid)
    bool fitsContent = false;           // as long as what it holds, instead of `size` that way
    bool scrolls = false;               // what does not fit is dragged, or scrolled with the wheel, into view
    Vec4 color{0, 0, 0, 0};             // behind them; clear for none

    // State.
    float scrolled = 0;                 // points the content is moved back by
    float contentLength = 0;            // the length of what it holds, the way they go
    bool isDragging = false;
    uint64_t finger = 0;
    float grabbedAt = 0, grabbedScroll = 0;

    void update(World& world, Entity self, float dt);
    void draw(World& world, Entity self, Canvas& canvas);
    /// Puts what it holds in place on a screen of that size (and what layouts inside it hold).
    void layOut(World& world, Entity self, Vec2 screen);
};

/// A value set by dragging along a bar: volume, sensitivity, the number of laps.
struct UISlider {
    std::string label = "Volume";
    std::string value = "volume";       // the shared value it shows and sets
    bool isSaved = false;               // also kept as a preference of that name, between plays
    float least = 0.0f, most = 1.0f;
    float step = 0.0f;                  // it moves by this much (0: smoothly): 1 for whole numbers
    bool showsNumber = true;
    Vec2 anchor{0.5f, 0.5f};
    Vec2 offset{0, 0};
    Vec2 size{300, 48};
    Vec4 color{0.95f, 0.47f, 0.18f, 1};
    Vec4 backColor{1, 1, 1, 0.15f};
    Vec4 textColor{1, 1, 1, 1};

    // State.
    bool isHeld = false;
    uint64_t finger = 0;
    bool hasChanged = false;            // true for the frame it was moved in

    void start(World& world, Entity self);
    void update(World& world, Entity self, float dt);
    void draw(World& world, Entity self, Canvas& canvas);
    /// Where the bar is, inside its rectangle (the label takes the left part).
    void bar(Vec2 corner, Vec2& from, Vec2& to) const;
    float current(const World& world) const;
};

/// A box to type in: a name, a code, a message. Click or tap it, type, press Return. What is
/// typed is a shared text (`world.text(value)`), shown by any text as "{playerName}".
struct UITextField {
    std::string value = "playerName";   // the shared text it shows and sets
    std::string placeholder = "Your name";  // shown, faintly, while it is empty
    int maxLength = 24;                 // letters at most; 0 for no limit
    bool isSaved = false;               // kept between plays, as a preference of that name
    Vec2 anchor{0.5f, 0.5f};
    Vec2 offset{0, 0};
    Vec2 size{320, 48};
    Vec4 color{0.08f, 0.09f, 0.11f, 0.9f};
    Vec4 textColor{1, 1, 1, 1};
    Vec4 focusColor{0.95f, 0.47f, 0.18f, 1};
    std::string font;                   // a font of Assets; empty for the game's own

    // State.
    bool isFocused = false;             // taking what is typed
    bool wasSubmitted = false;          // true for the frame Return was pressed in
    float caretTime = 0;
    float heldBack = 0;                 // how long Backspace has been held
    std::shared_ptr<struct Font> loadedFont;
    std::string loadedFontName = "\n";

    void start(World& world, Entity self);
    void update(World& world, Entity self, float dt);
    void draw(World& world, Entity self, Canvas& canvas);
    /// `typed` added to `text`, as a text field takes it: no new lines, at most `maxLength` letters.
    static std::string typedInto(const std::string& text, const std::string& typed, int maxLength);
    /// `text` without its last letter (a whole letter, not a byte of one).
    static std::string withoutLast(const std::string& text);
};

/// Draws with a font of Assets until it goes out of scope; the game's own again after.
class InterfaceFont {
public:
    InterfaceFont(World& world, Canvas& canvas, const std::string& name, std::shared_ptr<struct Font>& loaded, std::string& loadedName);
    ~InterfaceFont();

private:
    Canvas& canvas_;
    const FontMetrics* metrics_ = nullptr;
    const Texture* atlas_ = nullptr;
    bool isChanged_ = false;
};

/// A piece of interface inside a layout that scrolls is seen, and touched, only inside it.
/// False when it is in none: `min` and `max` are then the whole screen.
bool interfaceClip(World& world, Entity self, Vec2 screen, Vec2& min, Vec2& max);
/// Is `point` (in the interface's points) where the piece can be touched?
bool isTouchableAt(World& world, Entity self, Vec2 point);

/// Drawing a piece inside the clip of the layouts it is in.
class InterfaceClip {
public:
    InterfaceClip(World& world, Entity self, Canvas& canvas);
    ~InterfaceClip();

private:
    Canvas& canvas_;
    bool isClipped_ = false;
};

}  // namespace molten::kit
