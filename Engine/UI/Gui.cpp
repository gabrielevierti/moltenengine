#include "UI/Gui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace molten {

namespace {

const float kPopupRows = 10.0f;
const float kHeaderHeight = 30.0f;          // of a panel's title
const int kTooltipDelay = 24;               // frames the pointer must rest before a tooltip shows

Vec4 withAlpha(Vec4 color, float alpha) { return Vec4{color.x, color.y, color.z, alpha}; }

/// Removes the last character of UTF-8 text, however many bytes it takes.
void removeLastCharacter(std::string& text) {
    if (text.empty()) return;
    size_t i = text.size() - 1;
    while (i > 0 && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) i--;     // skip continuation bytes
    text.erase(i);
}

std::string formatNumber(float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.3f", double(value));
    std::string text = buffer;
    // 1.500 reads better as 1.5, and 2.000 as 2.
    while (text.back() == '0') text.pop_back();
    if (text.back() == '.') text.pop_back();
    if (text == "-0") text = "0";
    return text;
}

/// An id for a control that is placed by hand: its text and where it is.
uint64_t freeId(const std::string& text, Vec2 position) {
    uint64_t id = std::hash<std::string>()(text) * 1099511628211ULL;
    id += uint64_t(int64_t(position.x * 4.0f)) * 0x9e3779b97f4a7c15ULL + uint64_t(int64_t(position.y * 4.0f));
    return id == 0 ? 1 : id;
}

}  // namespace

void Gui::begin(Canvas& canvas, const Input& input) {
    canvas_ = &canvas;
    input_ = &input;
    mouse_ = input.mousePosition();
    changed_ = false;
    panelId_ = 0;
    tooltipSeen_ = 0;

    // Whether the pointer is over the interface is known from where the panels were in
    // the previous frame: this frame's panels have not been declared yet.
    previousPanels_ = panels_;
    panels_.clear();
    mouseOverGui_ = false;
    for (const Rect& rect : previousPanels_) mouseOverGui_ = mouseOverGui_ || rect.contains(mouse_);
    mouseFree_ = popup_.id == 0;

    if (active_ != 0) dragDistance_ += std::abs(input.mouseDelta().x) + std::abs(input.mouseDelta().y);

    // Typing goes to the focused field.
    if (focus_ != 0) {
        if (selectAll_ && (!input.textInput().empty() || input.wasPressed(Key::Backspace))) {
            editBuffer_.clear();
            selectAll_ = false;
        } else if (input.wasPressed(Key::Backspace)) {
            removeLastCharacter(editBuffer_);
        }
        editBuffer_ += input.textInput();
        if (input.wasPressed(Key::Enter) || input.wasPressed(Key::Tab)) commitFocus_ = true;
        if (input.wasPressed(Key::Escape)) focus_ = 0;                  // gives up the edit
        if (input.wasPressed(MouseButton::Left)) commitFocus_ = true;   // a click anywhere ends it
    }
}

void Gui::end() {
    // A control that was pressed stays active until the button comes up, wherever the pointer went.
    if (!input_->isDown(MouseButton::Left)) {
        active_ = 0;
        dragDistance_ = 0;
    }
    // An edit whose field did not come by to collect it (the field is gone) is dropped.
    if (commitFocus_ && committed_ == 0) focus_ = 0;
    commitFocus_ = false;
    committed_ = 0;

    // The pop-up is drawn last, so that it is on top of every panel.
    if (popup_.id != 0) {
        float rowHeight = style.rowHeight;
        float visible = std::min(float(popup_.items.size()), kPopupRows);
        Rect box{Vec2{popup_.anchor.position.x, popup_.anchor.position.y + popup_.anchor.size.y + 2},
                 Vec2{popup_.anchor.size.x, visible * rowHeight + 6}};
        // Open upward if there is no room below.
        if (box.position.y + box.size.y > canvas_->size().y) box.position.y = popup_.anchor.position.y - box.size.y - 2;
        panels_.push_back(box);

        float maxScroll = std::max(0.0f, float(popup_.items.size()) - kPopupRows);
        if (box.contains(mouse_)) popup_.scroll = std::clamp(popup_.scroll - input_->scrollDelta(), 0.0f, maxScroll);
        int first = int(popup_.scroll);

        drawShadow(box, 6);
        canvas_->roundedRect(box.position, box.size, 6, style.chrome);
        canvas_->roundedRectOutline(box.position, box.size, 6, 1, style.raisedHot);
        bool clicked = input_->wasPressed(MouseButton::Left);
        for (int row = 0; row < int(visible); row++) {
            int item = first + row;
            Rect rect{Vec2{box.position.x + 3, box.position.y + 3 + float(row) * rowHeight}, Vec2{box.size.x - 6, rowHeight}};
            bool hot = rect.contains(mouse_);
            if (hot) canvas_->roundedRect(rect.position, rect.size, style.radius, style.raised);
            bool isCurrent = item == popup_.current;
            drawText(popup_.items[size_t(item)], Rect{rect.position + Vec2{8, 0}, rect.size - Vec2{30, 0}},
                     isCurrent || hot ? style.text : style.textDim);
            if (isCurrent) icon(Icon::Check, rect.position + Vec2{rect.size.x - 13, rect.size.y * 0.5f}, 12, style.accent);
            if (hot && clicked) {
                popupResultId_ = popup_.id;
                popupResult_ = item;
            }
        }
        // Any click closes it: on an item (chosen above) or outside (nothing chosen).
        if (clicked && !popup_.anchor.contains(mouse_)) popup_ = Popup{};
    }

    // The tooltip, once the pointer has rested on one control for a while.
    if (tooltipSeen_ == 0) {
        tooltipId_ = 0;
        tooltipFrames_ = 0;
    } else if (tooltipSeen_ == tooltipId_) {
        tooltipFrames_++;
    } else {
        tooltipId_ = tooltipSeen_;
        tooltipFrames_ = 0;
    }
    if (tooltipFrames_ > kTooltipDelay && active_ == 0 && !tooltipText_.empty()) {
        Vec2 size = canvas_->measureText(tooltipText_, style.fontSize - 1) + Vec2{16, 10};
        Vec2 position{tooltipAnchor_.position.x + (tooltipAnchor_.size.x - size.x) * 0.5f,
                      tooltipAnchor_.position.y + tooltipAnchor_.size.y + 6};
        position.x = std::clamp(position.x, 4.0f, std::max(4.0f, canvas_->size().x - size.x - 4));
        if (position.y + size.y > canvas_->size().y) position.y = tooltipAnchor_.position.y - size.y - 6;
        drawShadow(Rect{position, size}, 4);
        canvas_->roundedRect(position, size, 4, style.chrome);
        canvas_->roundedRectOutline(position, size, 4, 1, style.raisedHot);
        canvas_->text(tooltipText_, position + Vec2{8, 4}, style.fontSize - 1, style.text);
    }
}

// ---------------------------------------------------------------- drawing helpers

/// A field: darker than the panel, with a line around it while the pointer is on it.
void Gui::drawWell(const Rect& rect, bool hot, bool active) {
    canvas_->roundedRect(rect.position, rect.size, style.radius, style.well);
    if (active) canvas_->roundedRectOutline(rect.position, rect.size, style.radius, 1, style.accent);
    else if (hot) canvas_->roundedRectOutline(rect.position, rect.size, style.radius, 1, style.raisedHot);
}

/// A button: lighter than the panel; the accent color while it is held down.
void Gui::drawButton(const Rect& rect, bool hot, bool down, bool selected) {
    if (down) {
        canvas_->roundedRect(rect.position, rect.size, style.radius, style.accent);
    } else if (selected) {
        canvas_->roundedRect(rect.position, rect.size, style.radius, withAlpha(style.accent, 0.22f));
        canvas_->roundedRectOutline(rect.position, rect.size, style.radius, 1, withAlpha(style.accent, 0.7f));
    } else {
        canvas_->roundedRect(rect.position, rect.size, style.radius, hot ? style.raisedHot : style.raised);
    }
}

/// A soft shadow below something that floats: a few wider and fainter copies of its shape.
void Gui::drawShadow(const Rect& rect, float radius) {
    for (int i = 4; i >= 1; i--) {
        float grow = float(i) * 2.0f;
        canvas_->roundedRect(rect.position + Vec2{-grow, -grow + 3}, rect.size + Vec2{grow * 2, grow * 2}, radius + grow,
                             Vec4{0, 0, 0, 0.07f});
    }
}

void Gui::showTooltip(uint64_t id, const Rect& rect, const std::string& text) {
    tooltipSeen_ = id;
    tooltipAnchor_ = rect;
    tooltipText_ = text;
}

void Gui::icon(Icon which, Vec2 center, float size, Vec4 color) {
    Canvas& c = *canvas_;
    float u = size / 16.0f;                         // icons are designed on a 16 x 16 grid, origin in the middle
    float w = std::max(1.5f * u, 1.2f);             // the width of their lines
    auto at = [&](float x, float y) { return center + Vec2{x, y} * u; };
    struct Point {
        float x, y;
    };
    auto path = [&](std::initializer_list<Point> points, bool closed = false) {
        std::vector<Vec2> list;
        for (Point p : points) list.push_back(center + Vec2{p.x, p.y} * u);
        c.polyline(list, w, color, closed);
    };
    switch (which) {
        case Icon::None: break;
        case Icon::Move:
            c.line(at(-6.5f, 0), at(6.5f, 0), w, color);
            c.line(at(0, -6.5f), at(0, 6.5f), w, color);
            path({{4.5f, -2}, {6.5f, 0}, {4.5f, 2}});
            path({{-4.5f, -2}, {-6.5f, 0}, {-4.5f, 2}});
            path({{-2, 4.5f}, {0, 6.5f}, {2, 4.5f}});
            path({{-2, -4.5f}, {0, -6.5f}, {2, -4.5f}});
            break;
        case Icon::Rotate:
            c.arc(center, 6 * u + w * 0.5f, 0.3f * kPi, 1.85f * kPi, w, color);
            path({{2.2f, -4.6f}, {5.6f, -2.7f}, {6.6f, -6.4f}});
            break;
        case Icon::Scale:
            c.roundedRectOutline(at(-6.5f, 0.5f), Vec2{6, 6} * u, 1 * u, w, color);
            c.line(at(-0.5f, 0.5f), at(6, -6), w, color);
            path({{2, -6}, {6, -6}, {6, -2}});
            break;
        case Icon::Play: c.triangle(at(-4, -6), at(6, 0), at(-4, 6), color); break;
        case Icon::Stop: c.roundedRect(at(-5, -5), Vec2{10, 10} * u, 2 * u, color); break;
        case Icon::Undo:
            path({{-5.5f, -2}, {1.5f, -2}, {4, -1}, {5, 1}, {4, 3}, {1.5f, 4}, {-3, 4}});
            path({{-2.5f, -5}, {-5.5f, -2}, {-2.5f, 1}});
            break;
        case Icon::Redo:
            path({{5.5f, -2}, {-1.5f, -2}, {-4, -1}, {-5, 1}, {-4, 3}, {-1.5f, 4}, {3, 4}});
            path({{2.5f, -5}, {5.5f, -2}, {2.5f, 1}});
            break;
        case Icon::Save:
            c.roundedRectOutline(at(-6.5f, -6.5f), Vec2{13, 13} * u, 2 * u, w, color);
            path({{-3, -6}, {-3, -2.5f}, {3, -2.5f}, {3, -6}});
            c.roundedRectOutline(at(-3.5f, 1), Vec2{7, 5.5f} * u, 1 * u, w, color);
            break;
        case Icon::Add:
            c.line(at(-5.5f, 0), at(5.5f, 0), w * 1.2f, color);
            c.line(at(0, -5.5f), at(0, 5.5f), w * 1.2f, color);
            break;
        case Icon::Trash:
            c.line(at(-6, -4), at(6, -4), w, color);
            path({{-2, -4}, {-2, -6.5f}, {2, -6.5f}, {2, -4}});
            path({{-4.5f, -4}, {-3.5f, 6.5f}, {3.5f, 6.5f}, {4.5f, -4}});
            break;
        case Icon::Copy:
            c.roundedRectOutline(at(-6.5f, -6.5f), Vec2{9, 9} * u, 1.5f * u, w, color);
            c.roundedRectOutline(at(-2.5f, -2.5f), Vec2{9, 9} * u, 1.5f * u, w, color);
            break;
        case Icon::Cube:
            path({{0, -7}, {6, -3.5f}, {6, 3.5f}, {0, 7}, {-6, 3.5f}, {-6, -3.5f}}, true);
            path({{-6, -3.5f}, {0, 0}, {6, -3.5f}});
            c.line(at(0, 0), at(0, 7), w, color);
            break;
        case Icon::Light:
            c.ring(center, 3.2f * u + w * 0.5f, w, color);
            for (int i = 0; i < 8; i++) {
                float angle = float(i) * kPi * 0.25f;
                Vec2 direction{std::cos(angle), std::sin(angle)};
                c.line(center + direction * (5.2f * u), center + direction * (7.2f * u), w, color);
            }
            break;
        case Icon::Camera:
            c.roundedRectOutline(at(-7, -4), Vec2{9.5f, 8} * u, 1.5f * u, w, color);
            path({{2.5f, -1}, {7, -3.8f}, {7, 3.8f}, {2.5f, 1}});
            break;
        case Icon::Empty: path({{0, -5.5f}, {5.5f, 0}, {0, 5.5f}, {-5.5f, 0}}, true); break;
        case Icon::File:
            path({{-4.5f, -7}, {1.5f, -7}, {4.5f, -4}, {4.5f, 7}, {-4.5f, 7}}, true);
            path({{1.5f, -7}, {1.5f, -4}, {4.5f, -4}});
            break;
        case Icon::Image:
            c.roundedRectOutline(at(-7, -5.5f), Vec2{14, 11} * u, 1.5f * u, w, color);
            path({{-5.5f, 3.5f}, {-2, -0.5f}, {0.5f, 2}, {2.5f, 0.2f}, {5.5f, 3.5f}});
            c.circle(at(3, -2.3f), 1.3f * u, color);
            break;
        case Icon::Material:
            c.ring(center, 6.5f * u, w, color);
            c.arc(center, 3.8f * u, 1.05f * kPi, 1.55f * kPi, w, color);
            break;
        case Icon::Check: c.polyline({at(-4.5f, 0.3f), at(-1.3f, 3.6f), at(4.8f, -3.6f)}, w * 1.25f, color); break;
        case Icon::ChevronRight: path({{-2, -4}, {2, 0}, {-2, 4}}); break;
        case Icon::ChevronDown: path({{-4, -2}, {0, 2}, {4, -2}}); break;
    }
}

// ---------------------------------------------------------------- panels

void Gui::beginPanel(const std::string& title, Vec2 position, Vec2 size) {
    panelId_ = std::hash<std::string>()(title);
    if (panelId_ == 0) panelId_ = 1;
    counter_ = 0;
    Rect whole{position, size};
    panels_.push_back(whole);

    // A title that starts with # names the panel without being shown.
    float header = title.empty() || title[0] == '#' ? 0.0f : kHeaderHeight;
    canvas_->rect(position, size, style.panel);
    if (header > 0) {
        canvas_->text(title, position + Vec2{style.padding, (header - style.fontSize) * 0.5f - 1}, style.fontSize, style.text);
        canvas_->rect(Vec2{position.x, position.y + header - 1}, Vec2{size.x, 1}, style.line);
    }
    canvas_->rectOutline(position, size, 1, style.line);

    content_ = Rect{position + Vec2{style.padding, header + style.padding},
                    size - Vec2{style.padding * 2, header + style.padding * 2}};
    clip_ = Rect{position + Vec2{1, header}, size - Vec2{2, header + 1}};
    float& scroll = scroll_[panelId_];
    if (whole.contains(mouse_) && mouseFree_) scroll -= input_->scrollDelta() * (style.rowHeight + style.spacing) * 3;    // three rows per notch
    scroll = std::max(scroll, 0.0f);
    contentTop_ = content_.position.y - scroll;
    cursorY_ = contentTop_;
    // Rows scroll under the title and the bottom edge: the canvas cuts off what sticks out.
    canvas_->pushClip(clip_.position, clip_.size);
}

void Gui::endPanel() {
    canvas_->popClip();
    // Now the height of the content is known: do not let the panel scroll past its end.
    float contentHeight = cursorY_ - contentTop_ - style.spacing;
    float maxScroll = std::max(0.0f, contentHeight - content_.size.y);
    float& scroll = scroll_[panelId_];
    scroll = std::min(scroll, maxScroll);
    if (maxScroll > 0) {
        // A thin bar on the right edge shows where we are.
        float track = clip_.size.y - 8;
        float bar = std::max(24.0f, track * content_.size.y / contentHeight);
        float offset = (track - bar) * (scroll / maxScroll);
        canvas_->roundedRect(Vec2{clip_.position.x + clip_.size.x - 6, clip_.position.y + 4 + offset}, Vec2{4, bar}, 2,
                             withAlpha(style.textFaint, 0.7f));
    }
    panelId_ = 0;
}

uint64_t Gui::makeId(const std::string& label) {
    // The label alone is not enough: an inspector may show two fields called "Radius".
    // The position of the control in its panel tells them apart.
    uint64_t id = panelId_ * 1099511628211ULL + std::hash<std::string>()(label);
    id = id * 1099511628211ULL + uint64_t(++counter_);
    return id == 0 ? 1 : id;
}

/// Reserves the next row. Returns false if no part of it shows (scrolled away): the
/// control then draws nothing and does not react.
bool Gui::nextRow(Rect& row) {
    row = Rect{Vec2{content_.position.x, cursorY_}, Vec2{content_.size.x, style.rowHeight}};
    cursorY_ += style.rowHeight + style.spacing;
    return row.position.y + row.size.y > clip_.position.y && row.position.y < clip_.position.y + clip_.size.y;
}

void Gui::splitRow(const Rect& row, const std::string& label, Rect& field) {
    float labelWidth = std::floor(row.size.x * style.labelWidth);
    drawText(label, Rect{row.position, Vec2{labelWidth - 6, row.size.y}}, style.textDim);
    field = Rect{row.position + Vec2{labelWidth, 0}, Vec2{row.size.x - labelWidth, row.size.y}};
}

/// The pointer is on this rectangle, and on a part of it that shows.
bool Gui::hovers(const Rect& rect) const {
    return mouseFree_ && rect.contains(mouse_) && (panelId_ == 0 || clip_.contains(mouse_));
}

/// The mouse button went down on this rectangle in this frame.
bool Gui::pressed(uint64_t id, const Rect& rect) {
    if (active_ != 0 || !hovers(rect) || !input_->wasPressed(MouseButton::Left)) return false;
    active_ = id;
    dragDistance_ = 0;
    return true;
}

std::string Gui::fitted(const std::string& text, float width) const {
    if (canvas_->measureText(text, style.fontSize).x <= width) return text;
    std::string shorter = text;
    while (!shorter.empty() && canvas_->measureText(shorter + "..", style.fontSize).x > width) removeLastCharacter(shorter);
    return shorter + "..";
}

void Gui::drawText(const std::string& text, const Rect& rect, Vec4 color, TextAlign align) {
    float y = rect.position.y + (rect.size.y - style.fontSize) * 0.5f - 1;
    float x = align == TextAlign::Left ? rect.position.x : align == TextAlign::Center ? rect.position.x + rect.size.x * 0.5f
                                                                                      : rect.position.x + rect.size.x;
    canvas_->text(fitted(text, rect.size.x), Vec2{x, y}, style.fontSize, color, align);
}

// ---------------------------------------------------------------- simple controls

bool Gui::section(const std::string& title, bool startsOpen) {
    // Not makeId: folding a section changes how many controls come before the next one,
    // and a section must keep its state all the same. Its title is its identity.
    uint64_t id = panelId_ * 1099511628211ULL + std::hash<std::string>()(title) + 0x5ec7;
    if (id == 0) id = 1;
    auto found = folded_.find(id);
    if (found == folded_.end()) found = folded_.emplace(id, !startsOpen).first;

    if (cursorY_ > contentTop_) cursorY_ += style.spacing;          // a little air above
    Rect row;
    if (!nextRow(row)) return !found->second;
    // The band runs the whole width of the panel, edge to edge.
    Rect band{Vec2{clip_.position.x, row.position.y}, Vec2{clip_.size.x, row.size.y}};
    bool hot = hovers(band);
    if (pressed(id, band)) found->second = !found->second;
    bool isOpen = !found->second;
    canvas_->rect(band.position, band.size, hot ? style.raised : withAlpha(style.raised, 0.55f));
    icon(isOpen ? Icon::ChevronDown : Icon::ChevronRight, row.position + Vec2{6, row.size.y * 0.5f}, 11, style.textDim);
    drawText(title, Rect{row.position + Vec2{18, 0}, row.size - Vec2{18, 0}}, style.text);
    return isOpen;
}

void Gui::heading(const std::string& text) {
    Rect row;
    if (!nextRow(row)) return;
    drawText(text, row, style.text);
    canvas_->rect(Vec2{row.position.x, row.position.y + row.size.y - 1}, Vec2{row.size.x, 1}, style.raised);
}

void Gui::label(const std::string& text) {
    Rect row;
    if (nextRow(row)) drawText(text, row, style.textDim);
}

void Gui::separator() {
    cursorY_ += style.spacing;
    canvas_->rect(Vec2{content_.position.x, cursorY_}, Vec2{content_.size.x, 1}, style.raised);
    cursorY_ += style.spacing * 2 + 1;
}

bool Gui::button(const std::string& text) {
    return buttonRow({text}) == 0;
}

int Gui::buttonRow(const std::vector<std::string>& labels) {
    Rect row;
    bool visible = nextRow(row);
    int clicked = -1;
    float gap = 6;
    float width = (row.size.x - gap * float(labels.size() - 1)) / float(labels.size());
    for (size_t i = 0; i < labels.size(); i++) {
        uint64_t id = makeId(labels[i]);
        if (!visible) continue;
        Rect rect{Vec2{row.position.x + float(i) * (width + gap), row.position.y}, Vec2{width, row.size.y}};
        if (pressed(id, rect)) clicked = int(i);
        drawButton(rect, hovers(rect), active_ == id, false);
        drawText(labels[i], Rect{rect.position + Vec2{4, 0}, rect.size - Vec2{8, 0}}, active_ == id ? style.chrome : style.text,
                 TextAlign::Center);
    }
    return clicked;
}

bool Gui::tabs(const std::vector<std::string>& labels, int& current) {
    Rect row;
    bool visible = nextRow(row);
    bool switched = false;
    // One sunken track; the chosen tab is a key standing on it.
    if (visible) canvas_->roundedRect(row.position, row.size, style.radius + 2, style.well);
    float width = row.size.x / float(labels.size());
    for (size_t i = 0; i < labels.size(); i++) {
        uint64_t id = makeId(labels[i]);
        if (!visible) continue;
        Rect rect{Vec2{row.position.x + float(i) * width, row.position.y}, Vec2{width, row.size.y}};
        if (pressed(id, rect) && current != int(i)) {
            current = int(i);
            switched = true;
        }
        bool selected = current == int(i);
        if (selected) canvas_->roundedRect(rect.position + Vec2{2, 2}, rect.size - Vec2{4, 4}, style.radius, style.raised);
        drawText(labels[i], rect, selected || hovers(rect) ? style.text : style.textDim, TextAlign::Center);
    }
    return switched;
}

bool Gui::selectable(const std::string& text, bool selected, float indent, Icon picture) {
    uint64_t id = makeId(text);
    Rect row;
    if (!nextRow(row)) return false;
    bool hot = hovers(row);
    bool clicked = pressed(id, row);
    if (selected) canvas_->roundedRect(row.position, row.size, style.radius, withAlpha(style.accent, 0.24f));
    else if (hot) canvas_->roundedRect(row.position, row.size, style.radius, withAlpha(style.raised, 0.7f));
    float left = 8 + indent;
    if (picture != Icon::None) {
        icon(picture, row.position + Vec2{left + 7, row.size.y * 0.5f}, 13, selected ? style.accent : style.textFaint);
        left += 21;
    }
    drawText(text, Rect{row.position + Vec2{left, 0}, row.size - Vec2{left + 6, 0}}, selected || hot ? style.text : style.textDim);
    return clicked;
}

bool Gui::checkbox(const std::string& label, bool& value) {
    uint64_t id = makeId(label);
    Rect row, field;
    if (!nextRow(row)) return false;
    splitRow(row, label, field);
    float side = 16;
    Rect box{field.position + Vec2{0, (row.size.y - side) * 0.5f}, Vec2{side, side}};
    bool clicked = pressed(id, box);
    if (clicked) {
        value = !value;
        changed_ = true;
    }
    if (value) {
        canvas_->roundedRect(box.position, box.size, 4, style.accent);
        icon(Icon::Check, box.position + box.size * 0.5f, 12, style.chrome);
    } else {
        canvas_->roundedRect(box.position, box.size, 4, style.well);
        canvas_->roundedRectOutline(box.position, box.size, 4, 1, hovers(box) ? style.textDim : style.raisedHot);
    }
    return clicked;
}

// ---------------------------------------------------------------- numbers and text

/// The text being typed into the focused field. Returns true when the edit was confirmed
/// in this frame, with `value` replaced by what was typed.
bool Gui::editText(uint64_t id, const Rect& rect, std::string& value) {
    if (focus_ != id) return false;
    if (commitFocus_ && !(rect.contains(mouse_) && input_->wasPressed(MouseButton::Left))) {
        // Confirmed by Enter or Tab, or by a click somewhere else.
        bool differs = editBuffer_ != value;
        value = editBuffer_;
        focus_ = 0;
        committed_ = id;
        return differs;
    }
    committed_ = id;                    // still editing: tells end() the field is alive
    drawWell(rect, true, true);
    Rect inner{rect.position + Vec2{7, 0}, rect.size - Vec2{14, 0}};
    if (selectAll_) {
        float width = std::min(canvas_->measureText(editBuffer_, style.fontSize).x, inner.size.x);
        canvas_->roundedRect(Vec2{inner.position.x - 2, rect.position.y + 4}, Vec2{width + 4, rect.size.y - 8}, 2,
                             withAlpha(style.accent, 0.4f));
    }
    drawText(editBuffer_, inner, style.text);
    // The insertion mark, after the last character.
    float end = std::min(canvas_->measureText(editBuffer_, style.fontSize).x, inner.size.x);
    canvas_->rect(Vec2{inner.position.x + end + 1, rect.position.y + 5}, Vec2{1, rect.size.y - 10}, style.accent);
    return false;
}

bool Gui::numberField(uint64_t id, const Rect& rect, float& value, float speed, float minimum, float maximum, Vec4 mark,
                      const char* letter) {
    // Being typed into?
    if (focus_ == id) {
        std::string text = formatNumber(value);
        if (editText(id, rect, text)) {
            char* parsedEnd = nullptr;
            float parsed = std::strtof(text.c_str(), &parsedEnd);
            if (parsedEnd != text.c_str()) {            // keep the old value if this is not a number
                value = std::clamp(parsed, minimum, maximum);
                changed_ = true;
                return true;
            }
        }
        return false;
    }

    bool changedNow = false;
    pressed(id, rect);
    if (active_ == id) {
        if (input_->isDown(MouseButton::Left)) {
            // Dragging: so many units per point of pointer travel.
            float delta = input_->mouseDelta().x * speed;
            if (delta != 0 && dragDistance_ > 3) {
                value = std::clamp(value + delta, minimum, maximum);
                changedNow = true;
            }
        } else if (dragDistance_ <= 3) {
            // Pressed and released without moving: a click. Switch to typing.
            focus_ = id;
            editBuffer_ = formatNumber(value);
            selectAll_ = true;
        }
    }
    drawWell(rect, hovers(rect), active_ == id);
    // Between two limits, a line along the bottom shows how far along the value is.
    if (std::isfinite(minimum) && std::isfinite(maximum) && maximum > minimum) {
        float share = saturate((value - minimum) / (maximum - minimum));
        canvas_->rect(rect.position + Vec2{style.radius, rect.size.y - 3}, Vec2{(rect.size.x - style.radius * 2) * share, 2},
                      withAlpha(style.accent, active_ == id ? 1.0f : 0.75f));
    }
    float left = 7;
    if (letter) {
        canvas_->text(letter, rect.position + Vec2{6, (rect.size.y - (style.fontSize - 2)) * 0.5f - 1}, style.fontSize - 2, mark);
        left = 19;
    }
    drawText(formatNumber(value), Rect{rect.position + Vec2{left, 0}, rect.size - Vec2{left + 4, 0}}, style.text);
    if (changedNow) changed_ = true;
    return changedNow;
}

bool Gui::dragFloat(const std::string& label, float& value, float speed, float minimum, float maximum) {
    uint64_t id = makeId(label);
    Rect row, field;
    if (!nextRow(row)) return false;
    splitRow(row, label, field);
    return numberField(id, field, value, speed, minimum, maximum, style.text, nullptr);
}

bool Gui::dragVec3(const std::string& label, Vec3& value, float speed) {
    const Vec4 marks[3] = {rgb(0xf0666b), rgb(0x5cc470), rgb(0x5b93ff)};       // x red, y green, z blue, as on the gizmo
    const char* letters[3] = {"X", "Y", "Z"};
    uint64_t ids[3] = {makeId(label), makeId(label), makeId(label)};
    Rect row, field;
    if (!nextRow(row)) return false;
    splitRow(row, label, field);
    float gap = 4, width = (field.size.x - gap * 2) / 3;
    bool any = false;
    for (int i = 0; i < 3; i++) {
        float component = value[i];
        Rect rect{Vec2{field.position.x + float(i) * (width + gap), field.position.y}, Vec2{width, field.size.y}};
        if (numberField(ids[i], rect, component, speed, -std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::infinity(), marks[i], letters[i])) {
            value[i] = component;
            any = true;
        }
    }
    return any;
}

bool Gui::color(const std::string& label, Vec3& value) {
    uint64_t ids[3] = {makeId(label), makeId(label), makeId(label)};
    Rect row, field;
    if (!nextRow(row)) return false;
    splitRow(row, label, field);
    // The swatch shows the color as the screen will: linear values encoded to sRGB, and
    // clipped at 1 (the canvas takes sRGB colors).
    float side = field.size.y;
    Vec3 shown{std::pow(saturate(value.x), 1 / 2.2f), std::pow(saturate(value.y), 1 / 2.2f), std::pow(saturate(value.z), 1 / 2.2f)};
    canvas_->roundedRect(field.position, Vec2{side, side}, style.radius, Vec4{shown.x, shown.y, shown.z, 1});
    canvas_->roundedRectOutline(field.position, Vec2{side, side}, style.radius, 1, withAlpha(style.text, 0.25f));

    float gap = 4, left = field.position.x + side + gap, width = (field.size.x - side - gap * 3) / 3;
    bool any = false;
    for (int i = 0; i < 3; i++) {
        float component = value[i];
        Rect rect{Vec2{left + float(i) * (width + gap), field.position.y}, Vec2{width, field.size.y}};
        if (numberField(ids[i], rect, component, 0.005f, 0.0f, 100.0f, style.text, nullptr)) {
            value[i] = component;
            any = true;
        }
    }
    return any;
}

bool Gui::textField(const std::string& label, std::string& value) {
    uint64_t id = makeId(label);
    Rect row, field;
    if (!nextRow(row)) return false;
    splitRow(row, label, field);
    if (focus_ == id) {
        if (editText(id, field, value)) {
            changed_ = true;
            return true;
        }
        return false;
    }
    if (pressed(id, field)) {
        focus_ = id;
        editBuffer_ = value;
        selectAll_ = true;
        commitFocus_ = false;           // this very click must not end the edit it starts
    }
    drawWell(field, hovers(field), false);
    drawText(value, Rect{field.position + Vec2{7, 0}, field.size - Vec2{14, 0}}, style.text);
    return false;
}

bool Gui::combo(const std::string& label, int& index, const std::vector<std::string>& items) {
    uint64_t id = makeId(label);
    // A choice made in the pop-up during the previous frame arrives here.
    bool chosen = false;
    if (popupResultId_ == id) {
        if (popupResult_ != index && popupResult_ < int(items.size())) {
            index = popupResult_;
            chosen = true;
            changed_ = true;
        }
        popupResultId_ = 0;
        popup_ = Popup{};
    }

    Rect row, field;
    if (!nextRow(row)) return chosen;
    splitRow(row, label, field);
    bool isOpen = popup_.id == id;
    // The combo itself must react while its own pop-up is open (to close it), so it
    // looks at the mouse directly instead of going through pressed().
    bool onField = field.contains(mouse_) && clip_.contains(mouse_);
    if (onField && input_->wasPressed(MouseButton::Left) && active_ == 0 && (mouseFree_ || isOpen)) {
        active_ = id;
        if (isOpen) {
            popup_ = Popup{};
        } else if (!items.empty()) {
            popup_ = Popup{};
            popup_.id = id;
            popup_.items = items;
            popup_.current = index;
            popup_.scroll = std::max(0.0f, std::min(float(index) - 2, float(items.size()) - kPopupRows));
        }
    }
    if (popup_.id == id) popup_.anchor = field;         // follows the panel if it scrolls

    drawWell(field, hovers(field), isOpen);
    std::string shown = index >= 0 && index < int(items.size()) ? items[size_t(index)] : std::string("Choose...");
    drawText(shown, Rect{field.position + Vec2{7, 0}, field.size - Vec2{28, 0}}, index >= 0 ? style.text : style.textFaint);
    icon(Icon::ChevronDown, field.position + Vec2{field.size.x - 12, field.size.y * 0.5f}, 10, style.textDim);
    return chosen;
}

// ---------------------------------------------------------------- free controls

void Gui::bar(Vec2 position, Vec2 size) {
    panels_.push_back(Rect{position, size});
    canvas_->rect(position, size, style.chrome);
    canvas_->rectOutline(position, size, 1, style.line);
}

void Gui::card(Vec2 position, Vec2 size) {
    Rect rect{position, size};
    panels_.push_back(rect);
    drawShadow(rect, 8);
    canvas_->roundedRect(position, size, 8, withAlpha(style.panel, 0.96f));
    canvas_->roundedRectOutline(position, size, 8, 1, withAlpha(style.text, 0.08f));
}

bool Gui::button(const std::string& text, Vec2 position, Vec2 size, bool isSelected, Icon picture) {
    uint64_t id = freeId(text, position);
    Rect rect{position, size};
    bool clicked = pressed(id, rect);
    drawButton(rect, hovers(rect), active_ == id, isSelected);
    Vec4 ink = active_ == id ? style.chrome : style.text;
    if (picture == Icon::None) {
        drawText(text, Rect{position + Vec2{6, 0}, size - Vec2{12, 0}}, ink, TextAlign::Center);
    } else {
        // The icon and the text are centered together.
        float width = 12 + 7 + canvas_->measureText(text, style.fontSize).x;
        float left = position.x + (size.x - width) * 0.5f;
        icon(picture, Vec2{left + 6, position.y + size.y * 0.5f}, 12, ink);
        drawText(text, Rect{Vec2{left + 19, position.y}, Vec2{size.x, size.y}}, ink);
    }
    return clicked;
}

bool Gui::iconButton(Icon picture, Vec2 position, Vec2 size, const std::string& tooltip, bool isSelected) {
    uint64_t id = freeId(tooltip, position);
    Rect rect{position, size};
    bool hot = hovers(rect);
    bool clicked = pressed(id, rect);
    bool down = active_ == id;
    // Quiet until touched: no key shape at rest, so a row of icons reads as one strip.
    if (down) canvas_->roundedRect(position, size, style.radius + 1, style.accent);
    else if (isSelected) canvas_->roundedRect(position, size, style.radius + 1, withAlpha(style.accent, 0.24f));
    else if (hot) canvas_->roundedRect(position, size, style.radius + 1, style.raised);
    icon(picture, position + size * 0.5f, std::min(size.x, size.y) * 0.56f,
         down ? style.chrome : isSelected ? style.accent : hot ? style.text : style.textDim);
    if (hot && !tooltip.empty()) showTooltip(id, rect, tooltip);
    return clicked;
}

}  // namespace molten
