// Layout.cpp
#include "Kit/Layout.hpp"

#include "Kit/Kit.hpp"

#include "Assets/Assets.hpp"
#include "Core/Log.hpp"
#include "Game/Component.hpp"
#include "Rendering/Canvas.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace molten::kit {

namespace {

Vec2 gLastScreen{1280, 720};            // the interface's size the last time a layout was drawn

/// Where a piece of interface keeps its place and its size, whatever kind of piece it is:
/// found by the names of its fields, as the editor finds them.
struct Placed {
    Vec2* anchor = nullptr;
    Vec2* offset = nullptr;
    Vec2* size = nullptr;               // a rectangle's
    float* radius = nullptr;            // a round one's
    float* letters = nullptr;           // a text's height of letters
    const std::string* text = nullptr;
    UILayout* layout = nullptr;
};

bool placedOf(World& world, Entity entity, Placed& out) {
    for (const ComponentType& type : ComponentRegistry::instance().types()) {
        if (!type.isInterface() || !type.get) continue;
        const Field* anchor = type.findField("anchor");
        const Field* offset = type.findField("offset");
        if (!anchor || !offset || anchor->type != FieldType::Vec2 || offset->type != FieldType::Vec2) continue;
        void* component = type.get(world.scene, entity);
        if (!component) continue;
        out.anchor = static_cast<Vec2*>(anchor->address(component));
        out.offset = static_cast<Vec2*>(offset->address(component));
        const Field* round = type.findField("isRound");
        bool isRound = round && round->type == FieldType::Bool && *static_cast<bool*>(round->address(component));
        const Field* size = type.findField("size");
        const Field* radius = type.findField("radius");
        const Field* text = type.findField("text");
        if (size && size->type == FieldType::Vec2 && !isRound) out.size = static_cast<Vec2*>(size->address(component));
        else if (radius && radius->type == FieldType::Float) out.radius = static_cast<float*>(radius->address(component));
        else if (size && size->type == FieldType::Float) out.letters = static_cast<float*>(size->address(component));
        if (text && text->type == FieldType::String) out.text = static_cast<const std::string*>(text->address(component));
        out.layout = world.scene.get<UILayout>(entity);
        return true;
    }
    return false;
}

/// How much room it takes.
Vec2 extentOf(const Placed& piece) {
    if (piece.size) return *piece.size;
    if (piece.radius) return Vec2{*piece.radius * 2, *piece.radius * 2};
    if (piece.letters) {
        float width = piece.text ? std::max(float(piece.text->size()) * *piece.letters * 0.52f, 24.0f) : 60.0f;
        return Vec2{width, *piece.letters * 1.25f};
    }
    return Vec2{40, 40};
}

/// A text is placed by the top of its line, at its left, middle or right as its anchor says:
/// everything else by its middle.
Vec2 middleOf(const Placed& piece, Vec2 screen) {
    Vec2 at = Vec2{screen.x * piece.anchor->x, screen.y * piece.anchor->y} + *piece.offset;
    if (!piece.letters) return at;
    Vec2 extent = extentOf(piece);
    float x = piece.anchor->x < 0.34f ? at.x + extent.x * 0.5f : piece.anchor->x > 0.66f ? at.x - extent.x * 0.5f : at.x;
    return Vec2{x, at.y + extent.y * 0.5f};
}

void placeAt(Placed& piece, Vec2 middle, Vec2 screen) {
    Vec2 at = middle;
    if (piece.letters) {
        Vec2 extent = extentOf(piece);
        at.y = middle.y - extent.y * 0.5f;
        if (piece.anchor->x < 0.34f) at.x = middle.x - extent.x * 0.5f;
        else if (piece.anchor->x > 0.66f) at.x = middle.x + extent.x * 0.5f;
    }
    *piece.offset = at - Vec2{screen.x * piece.anchor->x, screen.y * piece.anchor->y};
}

struct Pointer {
    bool isDown = false, hasBegun = false;
    uint64_t id = 0;
    Vec2 position{0, 0};
};

/// The first finger on the screen, or the mouse: in the interface's points.
Pointer pointerOf(const World& world) {
    float scale = world.interfaceScale > 1e-3f ? world.interfaceScale : 1.0f;
    for (const Input::Touch& touch : world.input.touches()) {
        if (!touch.hasEnded) return Pointer{true, touch.hasBegun, touch.id, touch.position / scale};
    }
    return Pointer{world.input.isDown(MouseButton::Left), world.input.wasPressed(MouseButton::Left), ~0ull, world.input.mousePosition() / scale};
}

bool isInside(Vec2 point, Vec2 min, Vec2 max) { return point.x >= min.x && point.y >= min.y && point.x <= max.x && point.y <= max.y; }

}  // namespace

// ---------------------------------------------------------------- UILayout

void UILayout::layOut(World& world, Entity self, Vec2 screen) {
    // What it holds: the pieces of interface right under it, that are switched on.
    struct Item {
        Entity entity;
        Placed placed;
        Vec2 extent, middle;
    };
    std::vector<Item> items;
    world.scene.each<Transform>([&](Entity entity, Transform& transform) {
        if (transform.parent != self || !transform.isActive) return;
        Item item{entity, {}, {}, {}};
        if (!placedOf(world, entity, item.placed)) return;
        item.extent = extentOf(item.placed);
        item.middle = middleOf(item.placed, screen);
        items.push_back(item);
    });
    // In the order they are on the screen: the way they go, then (a grid) across.
    const bool isRow = arrangement == 0, isGrid = arrangement == 2;
    std::stable_sort(items.begin(), items.end(), [&](const Item& a, const Item& b) {
        if (isGrid) {
            if (std::abs(a.middle.y - b.middle.y) > 0.5f) return a.middle.y < b.middle.y;
            return a.middle.x < b.middle.x;
        }
        return isRow ? a.middle.x < b.middle.x : a.middle.y < b.middle.y;
    });

    // Its own size: as long as what it holds, if it fits it.
    int perRow = std::max(columns, 1);
    auto lengthOf = [&](const std::vector<Item>& all) {
        float length = 0;
        if (isGrid) {
            for (size_t start = 0; start < all.size(); start += size_t(perRow)) {
                float tallest = 0;
                for (size_t k = start; k < std::min(all.size(), start + size_t(perRow)); k++) tallest = std::max(tallest, all[k].extent.y);
                length += tallest + (start > 0 ? spacing : 0);
            }
        } else {
            for (size_t k = 0; k < all.size(); k++) length += (isRow ? all[k].extent.x : all[k].extent.y) + (k > 0 ? spacing : 0);
        }
        return length;
    };
    contentLength = lengthOf(items);
    if (fitsContent) {
        if (isRow) size.x = contentLength + padding * 2;
        else size.y = contentLength + padding * 2;
    }
    Vec2 inner = simd::max(size - Vec2{padding * 2, padding * 2}, Vec2{0, 0});
    float room = isRow ? inner.x : inner.y;
    scrolled = scrolls ? std::clamp(scrolled, 0.0f, std::max(contentLength - room, 0.0f)) : 0.0f;

    Vec2 middle = placeOnScreen(screen, anchor, offset);
    Vec2 corner = middle - size * 0.5f + Vec2{padding, padding};
    auto across = [&](float start, float span, float extent) {
        return alignment <= 0 ? start + extent * 0.5f : alignment >= 2 ? start + span - extent * 0.5f : start + span * 0.5f;
    };
    float cursor = -scrolled;
    if (isGrid) {
        float cell = perRow > 0 ? (inner.x - spacing * float(perRow - 1)) / float(perRow) : inner.x;
        for (size_t start = 0; start < items.size(); start += size_t(perRow)) {
            size_t end = std::min(items.size(), start + size_t(perRow));
            float tallest = 0;
            for (size_t k = start; k < end; k++) {
                if (stretches && items[k].placed.size) {
                    items[k].placed.size->x = cell;
                    items[k].extent.x = cell;
                }
                tallest = std::max(tallest, items[k].extent.y);
            }
            for (size_t k = start; k < end; k++) {
                if (stretches && items[k].placed.size) {
                    items[k].placed.size->y = tallest;
                    items[k].extent.y = tallest;
                }
                float left = corner.x + float(k - start) * (cell + spacing);
                Vec2 at{across(left, cell, items[k].extent.x), corner.y + cursor + tallest * 0.5f};
                placeAt(items[k].placed, at, screen);
            }
            cursor += tallest + spacing;
        }
    } else {
        for (Item& item : items) {
            if (stretches && item.placed.size) {
                if (isRow) item.placed.size->y = inner.y;
                else item.placed.size->x = inner.x;
                item.extent = *item.placed.size;
            }
            float along = isRow ? item.extent.x : item.extent.y;
            Vec2 at = isRow ? Vec2{corner.x + cursor + along * 0.5f, across(corner.y, inner.y, item.extent.y)}
                            : Vec2{across(corner.x, inner.x, item.extent.x), corner.y + cursor + along * 0.5f};
            placeAt(item.placed, at, screen);
            cursor += along + spacing;
        }
    }
    // A layout inside: its place is set now, so what it holds goes after it at once.
    for (Item& item : items) {
        if (item.placed.layout) item.placed.layout->layOut(world, item.entity, screen);
    }
}

void UILayout::update(World& world, Entity self, float) {
    if (!scrolls) {
        isDragging = false;
        return;
    }
    Vec2 middle = placeOnScreen(gLastScreen, anchor, offset);
    Vec2 min = middle - size * 0.5f, max = middle + size * 0.5f;
    Vec2 clipMin, clipMax;
    if (interfaceClip(world, self, gLastScreen, clipMin, clipMax)) {
        min = simd::max(min, clipMin);
        max = simd::min(max, clipMax);
    }
    const bool isRow = arrangement == 0;
    Pointer pointer = pointerOf(world);
    float wheel = world.input.scrollDelta();
    if (wheel != 0 && isInside(world.input.mousePosition() / std::max(world.interfaceScale, 1e-3f), min, max)) scrolled -= wheel * 40.0f;
    float along = isRow ? pointer.position.x : pointer.position.y;
    if (pointer.isDown && pointer.hasBegun && isInside(pointer.position, min, max)) {
        isDragging = true;
        finger = pointer.id;
        grabbedAt = along;
        grabbedScroll = scrolled;
    }
    if (isDragging && (!pointer.isDown || pointer.id != finger)) isDragging = false;
    if (isDragging) scrolled = grabbedScroll - (along - grabbedAt);
    float room = (isRow ? size.x : size.y) - padding * 2;
    scrolled = std::clamp(scrolled, 0.0f, std::max(contentLength - room, 0.0f));
}

void UILayout::draw(World& world, Entity self, Canvas& canvas) {
    gLastScreen = canvas.size();
    // The outermost one places everything, those inside it included, before any of it is drawn.
    const Transform* transform = world.scene.get<Transform>(self);
    if (!transform || !world.scene.has<UILayout>(transform->parent)) layOut(world, self, gLastScreen);
    InterfaceClip clip(world, self, canvas);
    Vec2 corner = placeOnScreen(gLastScreen, anchor, offset) - size * 0.5f;
    if (color.w > 0) canvas.rect(corner, size, color);
    // Where it is while it is laid out: its edge, faintly, only in the editor.
    if (!world.runsComponents) canvas.rectOutline(corner, size, 1.0f, Vec4{1, 1, 1, 0.18f});
    // A thin bar says there is more, and where in it one is.
    float room = (arrangement == 0 ? size.x : size.y) - padding * 2;
    if (scrolls && contentLength > room + 0.5f && room > 0) {
        float share = room / contentLength, at = scrolled / contentLength;
        if (arrangement == 0) {
            canvas.rect(Vec2{corner.x + padding + room * at, corner.y + size.y - 5}, Vec2{room * share, 3}, Vec4{1, 1, 1, 0.35f});
        } else {
            canvas.rect(Vec2{corner.x + size.x - 5, corner.y + padding + room * at}, Vec2{3, room * share}, Vec4{1, 1, 1, 0.35f});
        }
    }
}

// ---------------------------------------------------------------- the clip of a layout that scrolls

bool interfaceClip(World& world, Entity self, Vec2 screen, Vec2& min, Vec2& max) {
    min = Vec2{-1e9f, -1e9f};
    max = Vec2{1e9f, 1e9f};
    bool isClipped = false;
    const Transform* transform = world.scene.get<Transform>(self);
    for (int depth = 0; transform && world.scene.isAlive(transform->parent) && depth < 32; depth++) {
        Entity up = transform->parent;
        if (const UILayout* layout = world.scene.get<UILayout>(up); layout && layout->scrolls) {
            Vec2 middle = placeOnScreen(screen, layout->anchor, layout->offset);
            Vec2 inner = layout->size * 0.5f - Vec2{layout->arrangement == 0 ? layout->padding : 0, layout->arrangement == 0 ? 0 : layout->padding};
            min = simd::max(min, middle - inner);
            max = simd::min(max, middle + inner);
            isClipped = true;
        }
        transform = world.scene.get<Transform>(up);
    }
    return isClipped;
}

bool isTouchableAt(World& world, Entity self, Vec2 point) {
    Vec2 min, max;
    if (!interfaceClip(world, self, gLastScreen, min, max)) return true;
    return isInside(point, min, max);
}

InterfaceClip::InterfaceClip(World& world, Entity self, Canvas& canvas) : canvas_(canvas) {
    Vec2 min, max;
    if (!interfaceClip(world, self, canvas.size(), min, max)) return;
    canvas.pushClip(min, simd::max(max - min, Vec2{0, 0}));
    isClipped_ = true;
}

InterfaceClip::~InterfaceClip() {
    if (isClipped_) canvas_.popClip();
}

// ---------------------------------------------------------------- UISlider

void UISlider::bar(Vec2 corner, Vec2& from, Vec2& to) const {
    float left = label.empty() ? 14.0f : size.x * 0.38f;
    float right = showsNumber ? 64.0f : 14.0f;
    from = Vec2{corner.x + left, corner.y + size.y * 0.5f};
    to = Vec2{corner.x + std::max(size.x - right, left + 10), corner.y + size.y * 0.5f};
}

float UISlider::current(const World& world) const {
    float shown = world.hasValue(value) ? world.value(value) : least;
    return std::clamp(shown, std::min(least, most), std::max(least, most));
}

void UISlider::start(World& world, Entity) {
    if (value.empty()) return;
    if (isSaved && world.hasPreference(value)) world.setValue(value, world.preference(value));
    else if (!world.hasValue(value)) world.setValue(value, least);
}

void UISlider::update(World& world, Entity self, float) {
    hasChanged = false;
    if (value.empty()) return;
    Vec2 corner = placeOnScreen(gLastScreen, anchor, offset) - size * 0.5f;
    Vec2 from, to;
    bar(corner, from, to);
    Pointer pointer = pointerOf(world);
    bool isOn = isInside(pointer.position, corner, corner + size) && isTouchableAt(world, self, pointer.position);
    if (pointer.isDown && pointer.hasBegun && isOn) {
        isHeld = true;
        finger = pointer.id;
    }
    if (isHeld && (!pointer.isDown || pointer.id != finger)) isHeld = false;
    if (!isHeld) return;
    float share = std::clamp((pointer.position.x - from.x) / std::max(to.x - from.x, 1.0f), 0.0f, 1.0f);
    float chosen = least + (most - least) * share;
    if (step > 0) chosen = least + std::round((chosen - least) / step) * step;
    if (std::abs(chosen - world.value(value)) > 1e-6f || !world.hasValue(value)) {
        world.setValue(value, chosen);
        if (isSaved) world.setPreference(value, chosen);
        hasChanged = true;
    }
}

void UISlider::draw(World& world, Entity self, Canvas& canvas) {
    gLastScreen = canvas.size();
    InterfaceClip clip(world, self, canvas);
    Vec2 corner = placeOnScreen(gLastScreen, anchor, offset) - size * 0.5f;
    Vec2 from, to;
    bar(corner, from, to);
    float letters = std::min(size.y * 0.45f, 22.0f);
    if (!label.empty()) canvas.text(world.translate(label), Vec2{corner.x + 10, corner.y + (size.y - letters * 1.2f) * 0.5f}, letters, textColor);
    float shown = current(world);
    float share = most != least ? (shown - least) / (most - least) : 0.0f;
    float thickness = std::max(size.y * 0.12f, 3.0f);
    canvas.roundedRect(from - Vec2{0, thickness * 0.5f}, Vec2{to.x - from.x, thickness}, thickness * 0.5f, backColor);
    canvas.roundedRect(from - Vec2{0, thickness * 0.5f}, Vec2{(to.x - from.x) * share, thickness}, thickness * 0.5f, color);
    Vec2 knob{from.x + (to.x - from.x) * share, from.y};
    canvas.circle(knob, size.y * (isHeld ? 0.3f : 0.25f), color);
    if (showsNumber) {
        char number[24];
        bool isWhole = step >= 1 || std::abs(most - least) >= 20;
        std::snprintf(number, sizeof(number), isWhole ? "%.0f" : "%.2f", double(shown));
        canvas.text(number, Vec2{corner.x + size.x - 10, corner.y + (size.y - letters * 1.2f) * 0.5f}, letters, textColor, TextAlign::Right);
    }
}

// ---------------------------------------------------------------- UIToggle

void UIToggle::start(World& world, Entity) {
    if (value.empty()) return;
    if (isSaved && world.hasPreference(value)) world.setValue(value, world.preference(value));
    else if (!world.hasValue(value)) world.setValue(value, isOn ? 1.0f : 0.0f);
}

void UIToggle::update(World& world, Entity self, float) {
    hasChanged = false;
    if (value.empty()) return;
    Vec2 corner = placeOnScreen(gLastScreen, anchor, offset) - size * 0.5f;
    Pointer pointer = pointerOf(world);
    bool isOver = isInside(pointer.position, corner, corner + size) && isTouchableAt(world, self, pointer.position);
    if (pointer.isDown && pointer.hasBegun && isOver) {
        float now = world.value(value) > 0.5f ? 0.0f : 1.0f;
        world.setValue(value, now);
        if (isSaved) world.setPreference(value, now);
        hasChanged = true;
    }
}

void UIToggle::draw(World& world, Entity self, Canvas& canvas) {
    gLastScreen = canvas.size();
    InterfaceClip clip(world, self, canvas);
    Vec2 corner = placeOnScreen(gLastScreen, anchor, offset) - size * 0.5f;
    float letters = std::min(size.y * 0.45f, 22.0f);
    bool ticked = value.empty() ? isOn : world.value(value) > 0.5f;
    // The box on the left, the words beside it.
    float side = size.y * 0.62f;
    Vec2 box{corner.x + 4, corner.y + (size.y - side) * 0.5f};
    canvas.roundedRect(box, Vec2{side, side}, side * 0.22f, ticked ? color : backColor);
    if (ticked) {
        Vec2 a{box.x + side * 0.24f, box.y + side * 0.52f}, b{box.x + side * 0.43f, box.y + side * 0.72f}, c{box.x + side * 0.78f, box.y + side * 0.3f};
        canvas.line(a, b, std::max(side * 0.12f, 2.0f), textColor);
        canvas.line(b, c, std::max(side * 0.12f, 2.0f), textColor);
    }
    if (!label.empty()) canvas.text(world.translate(label), Vec2{box.x + side + 12, corner.y + (size.y - letters * 1.2f) * 0.5f}, letters, textColor);
}

// ---------------------------------------------------------------- fonts

InterfaceFont::InterfaceFont(World& world, Canvas& canvas, const std::string& name, std::shared_ptr<Font>& loaded, std::string& loadedName)
    : canvas_(canvas) {
    if (name.empty()) return;
    if (loadedName != name) {
        loadedName = name;
        loaded = world.loadFont(name);
        if (!loaded && world.runsComponents) logWarning("there is no font %s (made by Tools/bake_font.py): the game's own is used", name.c_str());
    }
    if (!loaded || !loaded->atlas) return;
    metrics_ = canvas.font();
    atlas_ = canvas.fontAtlas();
    canvas.setFont(&loaded->metrics, loaded->atlas.get());
    isChanged_ = true;
}

InterfaceFont::~InterfaceFont() {
    if (isChanged_) canvas_.setFont(metrics_, atlas_);
}

// ---------------------------------------------------------------- UITextField

std::string UITextField::typedInto(const std::string& text, const std::string& typed, int maxLength) {
    auto letters = [](const std::string& s) {
        size_t count = 0;
        for (unsigned char c : s) count += (c & 0xC0) != 0x80;              // a letter starts each UTF-8 sequence
        return count;
    };
    std::string out = text;
    size_t count = letters(out);
    for (size_t i = 0; i < typed.size();) {
        unsigned char lead = static_cast<unsigned char>(typed[i]);
        size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : 4;
        std::string letter = typed.substr(i, length);
        i += length;
        if (lead < 0x20 || lead == 0x7F) continue;                      // Return, Tab, Backspace: not letters
        if (maxLength > 0 && count >= size_t(maxLength)) break;
        out += letter;
        count++;
    }
    return out;
}

std::string UITextField::withoutLast(const std::string& text) {
    if (text.empty()) return text;
    size_t end = text.size() - 1;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) end--;
    return text.substr(0, end);
}

void UITextField::start(World& world, Entity) {
    if (value.empty()) return;
    if (isSaved && world.hasTextPreference(value)) world.setText(value, world.textPreference(value));
    else if (!world.hasText(value)) world.setText(value, "");
}

void UITextField::update(World& world, Entity self, float dt) {
    wasSubmitted = false;
    caretTime += dt;
    Vec2 corner = placeOnScreen(gLastScreen, anchor, offset) - size * 0.5f;
    Pointer pointer = pointerOf(world);
    if (pointer.isDown && pointer.hasBegun) {
        // A click on it takes what is typed; a click anywhere else lets it go.
        bool isOn = isInside(pointer.position, corner, corner + size) && isTouchableAt(world, self, pointer.position);
        if (isOn && !isFocused) caretTime = 0;
        isFocused = isOn;
    }
    if (!isFocused || value.empty()) return;
    world.wantsTextInput = true;
    Input& input = world.input;
    std::string text = world.text(value);
    std::string changed = typedInto(text, input.textInput(), maxLength);
    // Backspace: once when pressed, then again and again while held.
    if (input.wasPressed(Key::Backspace)) {
        changed = withoutLast(changed);
        heldBack = 0;
    } else if (input.isDown(Key::Backspace)) {
        const float wait = 0.45f, every = 0.04f;
        auto repeats = [&](float held) { return held <= wait ? 0 : int((held - wait) / every) + 1; };
        float before = heldBack;
        heldBack += dt;
        for (int k = repeats(before); k < repeats(heldBack); k++) changed = withoutLast(changed);
    }
    if (input.wasPressed(Key::Enter)) {
        wasSubmitted = true;
        isFocused = false;
    }
    if (input.wasPressed(Key::Escape)) isFocused = false;
    if (changed != text) {
        world.setText(value, changed);
        caretTime = 0;
    }
    if (isSaved && (changed != text || wasSubmitted)) world.setTextPreference(value, changed);
}

void UITextField::draw(World& world, Entity self, Canvas& canvas) {
    gLastScreen = canvas.size();
    InterfaceClip clip(world, self, canvas);
    InterfaceFont typeface(world, canvas, font, loadedFont, loadedFontName);
    Vec2 corner = placeOnScreen(gLastScreen, anchor, offset) - size * 0.5f;
    canvas.roundedRect(corner, size, 6, color);
    canvas.roundedRectOutline(corner, size, 6, isFocused ? 2.0f : 1.0f, isFocused ? focusColor : Vec4{1, 1, 1, 0.25f});
    float letters = std::min(size.y * 0.5f, 26.0f);
    const std::string& text = world.text(value);
    bool isEmpty = text.empty();
    std::string shown = isEmpty ? world.translate(placeholder) : text;
    float width = isEmpty ? 0.0f : canvas.measureText(text, letters).x;
    float room = std::max(size.x - 24, 1.0f);
    // A text longer than the box shows its end, where one types.
    float x = corner.x + 12 - (isFocused ? std::max(width - room, 0.0f) : 0.0f);
    float y = corner.y + (size.y - letters * 1.2f) * 0.5f;
    canvas.pushClip(corner + Vec2{6, 0}, size - Vec2{12, 0});
    canvas.text(shown, Vec2{x, y}, letters, isEmpty ? textColor * Vec4{1, 1, 1, 0.4f} : textColor);
    if (isFocused && std::fmod(caretTime, 1.0f) < 0.6f) {
        float at = x + width + 1;
        canvas.rect(Vec2{at, y}, Vec2{2, letters * 1.15f}, focusColor);
    }
    canvas.popClip();
}

}  // namespace molten::kit
