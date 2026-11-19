// Gui.hpp
// A small immediate-mode user interface for tools: panels, buttons, number fields,
// text fields, lists, pop-up menus and icons, drawn with the Canvas.
//
// "Immediate mode" means the interface is not built once and kept: every frame the
// program says what is on screen, and each call both draws a control and reports what
// the user did with it:
//
//     gui.beginPanel("Light", Vec2{16, 16}, Vec2{280, 200});
//     gui.dragFloat("Intensity", light.intensity, 0.05f, 0.0f, 100.0f);
//     if (gui.button("Reset")) light = Light{};
//     gui.endPanel();
//
// There is no list of widgets to keep in step with the data: the data is the only
// state. The few things the Gui itself must remember between frames (which control is
// being dragged, which text field has the keyboard, how far a panel is scrolled, which
// sections are folded) are keyed by an id computed from the panel's title and the
// control's label.
//
// How it looks follows three rules. Things to fill in (fields) are darker than the
// panel they sit on, like wells; things to press (buttons) are lighter, like keys. The
// accent color is spent only on what is selected or being changed. And nothing is an
// image: icons are a few lines and curves drawn by the Canvas, sharp at any size.
//
// The engine's editor is built with this, and a game can use it for menus and debug
// panels. It only needs a Canvas and an Input, so it is tested without a GPU.
#pragma once

#include "Input/Input.hpp"
#include "Rendering/Canvas.hpp"

#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace molten {

class Gui {
public:
    /// The pictures a button or a list row can carry.
    enum class Icon : uint8_t {
        None, Move, Rotate, Scale, Play, Stop, Undo, Redo, Save, Add, Trash, Copy,
        Cube, Light, Camera, Empty, File, Image, Material, Check, ChevronRight, ChevronDown,
    };

    struct Style {
        float fontSize = 13.0f;
        float rowHeight = 24.0f;
        float spacing = 4.0f;               // between rows
        float padding = 10.0f;              // inside a panel
        float labelWidth = 0.36f;           // share of a row taken by the label of a field
        float radius = 4.0f;                // of the corners of fields and buttons
        Vec4 chrome = rgb(0x1b1d22);        // bars along the edges of the window
        Vec4 panel = rgb(0x24272d);
        Vec4 well = rgb(0x17191d);          // fields: darker than the panel
        Vec4 raised = rgb(0x343840);        // buttons: lighter than the panel
        Vec4 raisedHot = rgb(0x424752);     // a button under the pointer
        Vec4 line = rgb(0x101216);          // between panels, and around pop-ups
        Vec4 accent = rgb(0xfe8b3c);        // the accent: selection and changes only
        Vec4 text = rgb(0xe4e7ec);
        Vec4 textDim = rgb(0x9aa1ad);
        Vec4 textFaint = rgb(0x6b7280);
    };
    Style style;

    /// Call once per frame before any control, and end() after the last one.
    void begin(Canvas& canvas, const Input& input);
    void end();

    /// True if the pointer is over a panel or a control is in use: the program should then
    /// leave the mouse alone (not select or move things in the 3D view).
    bool wantsMouse() const { return mouseOverGui_ || active_ != 0 || popup_.id != 0; }
    /// True while a text field has the keyboard: the program should ignore its shortcuts.
    bool wantsKeyboard() const { return focus_ != 0; }
    /// True if any control changed a value during this frame.
    bool changed() const { return changed_; }
    /// True while a control is being dragged or edited: an edit that is not finished yet.
    bool isBusy() const { return active_ != 0 || focus_ != 0; }

    // ---- panels. Controls go between beginPanel and endPanel, one per row, top to bottom.
    // A panel scrolls with the mouse wheel when its content is taller than it is. A title
    // that starts with # is not shown: the panel has no title bar, but keeps its identity.
    void beginPanel(const std::string& title, Vec2 position, Vec2 size);
    void endPanel();

    // ---- controls. Those that edit a value return true in the frame it changed.
    /// A title that folds what follows it. Returns true while open: put the controls of
    /// the section inside `if (gui.section("Transform")) { ... }`.
    bool section(const std::string& title, bool startsOpen = true);
    void heading(const std::string& text);
    void label(const std::string& text);
    void separator();
    bool button(const std::string& text);
    /// Several buttons on one row. Returns the position of the one clicked, or -1.
    int buttonRow(const std::vector<std::string>& labels);
    /// Like buttonRow, but one stays selected.
    bool tabs(const std::vector<std::string>& labels, int& current);
    /// A row of a list: highlighted when `selected`, returns true when clicked.
    bool selectable(const std::string& text, bool selected, float indent = 0.0f, Icon icon = Icon::None);
    bool checkbox(const std::string& label, bool& value);
    /// A number changed by dragging sideways; a click without dragging lets you type it.
    /// With both a minimum and a maximum, the field also shows where the value is between them.
    bool dragFloat(const std::string& label, float& value, float speed = 0.01f,
                   float minimum = -std::numeric_limits<float>::infinity(), float maximum = std::numeric_limits<float>::infinity());
    bool dragVec3(const std::string& label, Vec3& value, float speed = 0.01f);
    /// Three numbers and a swatch. Colors are linear; values above 1 are allowed (they bloom).
    bool color(const std::string& label, Vec3& value);
    /// Returns true when the text was changed and confirmed (Enter, or a click elsewhere).
    bool textField(const std::string& label, std::string& value);
    /// A pop-up list. `index` may be -1 for "nothing chosen".
    bool combo(const std::string& label, int& index, const std::vector<std::string>& items);

    // ---- free controls: placed by the program anywhere on screen, outside any panel.
    // Tool bars and status bars are made of these.
    /// A strip of window chrome. The pointer over it belongs to the interface.
    void bar(Vec2 position, Vec2 size);
    /// A rounded surface that floats over the 3D view, with a shadow.
    void card(Vec2 position, Vec2 size);
    bool button(const std::string& text, Vec2 position, Vec2 size, bool isSelected = false, Icon icon = Icon::None);
    /// `tooltip` appears when the pointer rests on the button.
    bool iconButton(Icon icon, Vec2 position, Vec2 size, const std::string& tooltip, bool isSelected = false);
    /// Draws an icon, centered, `size` points wide.
    void icon(Icon icon, Vec2 center, float size, Vec4 color);

private:
    struct Rect {
        Vec2 position{0, 0}, size{0, 0};
        bool contains(Vec2 p) const {
            return p.x >= position.x && p.y >= position.y && p.x < position.x + size.x && p.y < position.y + size.y;
        }
    };
    struct Popup {
        uint64_t id = 0;                    // the combo that opened it; 0 when closed
        Rect anchor;
        std::vector<std::string> items;
        int current = -1;
        float scroll = 0;
    };

    uint64_t makeId(const std::string& label);
    bool nextRow(Rect& row);
    void splitRow(const Rect& row, const std::string& label, Rect& field);
    bool hovers(const Rect& rect) const;
    bool pressed(uint64_t id, const Rect& rect);
    bool numberField(uint64_t id, const Rect& rect, float& value, float speed, float minimum, float maximum, Vec4 mark,
                     const char* letter);
    bool editText(uint64_t id, const Rect& rect, std::string& value);
    void drawText(const std::string& text, const Rect& rect, Vec4 color, TextAlign align = TextAlign::Left);
    void drawWell(const Rect& rect, bool hot, bool active);
    void drawButton(const Rect& rect, bool hot, bool down, bool selected);
    void drawShadow(const Rect& rect, float radius);
    void showTooltip(uint64_t id, const Rect& rect, const std::string& text);
    std::string fitted(const std::string& text, float width) const;

    Canvas* canvas_ = nullptr;
    const Input* input_ = nullptr;
    Vec2 mouse_{0, 0};
    bool mouseOverGui_ = false;
    bool changed_ = false;
    bool mouseFree_ = true;                 // false while a pop-up is open: controls under it must not react
    uint64_t active_ = 0;                   // the control the mouse button went down on
    float dragDistance_ = 0;                // how far the pointer moved since then
    uint64_t focus_ = 0;                    // the field being typed into
    std::string editBuffer_;
    bool selectAll_ = false;                // a fresh edit: the first key replaces the whole text
    bool commitFocus_ = false;              // the edit ended: its field picks the buffer up on its next call
    uint64_t committed_ = 0;

    // The panel being filled.
    uint64_t panelId_ = 0;
    Rect content_;                          // where rows are laid out
    Rect clip_;                             // what is visible of them: the panel below its title
    float cursorY_ = 0;
    float contentTop_ = 0;
    int counter_ = 0;
    std::unordered_map<uint64_t, float> scroll_;
    std::unordered_map<uint64_t, bool> folded_;     // sections, by id: true when closed
    std::vector<Rect> panels_, previousPanels_;
    Popup popup_;
    uint64_t popupResultId_ = 0;            // set by end(), read by the combo on the next frame
    int popupResult_ = -1;

    // The tooltip: shown once the pointer has rested on the same control for a moment.
    uint64_t tooltipId_ = 0, tooltipSeen_ = 0;
    int tooltipFrames_ = 0;
    Rect tooltipAnchor_;
    std::string tooltipText_;
};

}  // namespace molten
