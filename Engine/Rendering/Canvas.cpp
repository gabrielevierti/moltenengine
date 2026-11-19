#include "Rendering/Canvas.hpp"

#include <algorithm>
#include <cmath>

namespace molten {

void Canvas::begin(Vec2 sizeInPoints, float pixelsPerPoint) {
    size_ = sizeInPoints;
    pixelSize_ = 1.0f / std::max(pixelsPerPoint, 0.25f);
    vertices_.clear();
    batches_.clear();
    clips_.clear();
    isClipEmpty_ = false;
}

void Canvas::setFont(const FontMetrics* metrics, const Texture* atlas) {
    font_ = metrics;
    fontAtlas_ = atlas;
}

// ---------------------------------------------------------------- clipping

void Canvas::pushClip(Vec2 position, Vec2 size) {
    Clip clip{position, position + size};
    // A clip can only make the area smaller.
    Clip outer = clips_.empty() ? Clip{Vec2{0, 0}, size_} : clips_.back();
    clip.min = simd::max(clip.min, outer.min);
    clip.max = simd::min(clip.max, outer.max);
    clips_.push_back(clip);
    isClipEmpty_ = clip.max.x <= clip.min.x || clip.max.y <= clip.min.y;
}

void Canvas::popClip() {
    if (!clips_.empty()) clips_.pop_back();
    isClipEmpty_ = !clips_.empty() && (clips_.back().max.x <= clips_.back().min.x || clips_.back().max.y <= clips_.back().min.y);
}

Canvas::Batch& Canvas::batchFor(const Texture* texture, bool isText) {
    // Consecutive shapes that use the same texture and the same clip share a draw call.
    bool isClipped = !clips_.empty();
    Vec2 clipMin = isClipped ? clips_.back().min : Vec2{0, 0}, clipMax = isClipped ? clips_.back().max : Vec2{0, 0};
    bool fits = !batches_.empty() && batches_.back().texture == texture && batches_.back().isText == isText &&
                batches_.back().isClipped == isClipped && simd::all(batches_.back().clipMin == clipMin) &&
                simd::all(batches_.back().clipMax == clipMax);
    if (!fits) {
        Batch batch;
        batch.texture = texture;
        batch.isText = isText;
        batch.firstVertex = uint32_t(vertices_.size());
        batch.isClipped = isClipped;
        batch.clipMin = clipMin;
        batch.clipMax = clipMax;
        batches_.push_back(batch);
    }
    return batches_.back();
}

// ---------------------------------------------------------------- triangles

void Canvas::quad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, Vec2 uvA, Vec2 uvB, Vec2 uvC, Vec2 uvD, Vec4 color,
                  const Texture* texture, bool isText) {
    if (isClipEmpty_) return;
    Batch& batch = batchFor(texture, isText);
    const gpu::CanvasVertex corners[6] = {{a, uvA, color}, {b, uvB, color}, {c, uvC, color},
                                          {a, uvA, color}, {c, uvC, color}, {d, uvD, color}};
    vertices_.insert(vertices_.end(), std::begin(corners), std::end(corners));
    batch.vertexCount += 6;
}

/// One corner of a solid-colored triangle. Callers add them three at a time.
void Canvas::vertex(Vec2 position, Vec4 color) {
    vertices_.push_back({position, Vec2{0.5f, 0.5f}, color});
}

/// Fills a convex shape. The shape is drawn half a pixel smaller than asked, and a strip
/// one pixel wide around it fades to transparent: that strip is the anti-aliasing.
void Canvas::fillConvex(const Vec2* points, int count, Vec4 color) {
    if (count < 3 || isClipEmpty_) return;
    float area = 0;
    for (int i = 0; i < count; i++) {
        Vec2 a = points[i], b = points[(i + 1) % count];
        area += a.x * b.y - b.x * a.y;
    }
    if (std::abs(area) < 1e-6f) return;
    float outward = area > 0 ? 1.0f : -1.0f;        // which way "outside" is depends on the order of the corners
    float half = pixelSize_ * 0.5f;

    inner_.resize(size_t(count));
    outer_.resize(size_t(count));
    for (int i = 0; i < count; i++) {
        Vec2 before = points[(i + count - 1) % count], here = points[i], after = points[(i + 1) % count];
        Vec2 e0 = safeNormalize(here - before, Vec2{1, 0}), e1 = safeNormalize(after - here, Vec2{1, 0});
        // The direction that moves this corner out by one unit along both of its edges.
        Vec2 miter = (Vec2{e0.y, -e0.x} + Vec2{e1.y, -e1.x}) * (0.5f * outward);
        miter = miter / std::max(simd::dot(miter, miter), 0.25f);
        inner_[size_t(i)] = here - miter * half;
        outer_[size_t(i)] = here + miter * half;
    }

    Batch& batch = batchFor(nullptr, false);
    size_t first = vertices_.size();
    Vec4 clear{color.x, color.y, color.z, 0.0f};
    for (int i = 1; i + 1 < count; i++) {
        vertex(inner_[0], color);
        vertex(inner_[size_t(i)], color);
        vertex(inner_[size_t(i) + 1], color);
    }
    for (int i = 0; i < count; i++) {
        size_t a = size_t(i), b = size_t((i + 1) % count);
        vertex(inner_[a], color);
        vertex(inner_[b], color);
        vertex(outer_[b], clear);
        vertex(inner_[a], color);
        vertex(outer_[b], clear);
        vertex(outer_[a], clear);
    }
    batch.vertexCount += uint32_t(vertices_.size() - first);
}

/// Draws a line through the points: a solid core with a fading strip on each side.
void Canvas::strokePath(const Vec2* points, int count, float thickness, Vec4 color, bool isClosed) {
    if (count < 2 || thickness <= 0 || isClipEmpty_) return;
    float soft = pixelSize_;
    // Thinner than a pixel cannot be drawn; a fainter line one pixel wide looks the same.
    if (thickness < soft) {
        color.w *= thickness / soft;
        thickness = soft;
    }
    float core = (thickness - soft) * 0.5f;
    float edge = core + soft;

    // The sideways direction at every point: across the line, averaged where two segments meet.
    inner_.resize(size_t(count));
    for (int i = 0; i < count; i++) {
        bool hasBefore = isClosed || i > 0, hasAfter = isClosed || i + 1 < count;
        Vec2 here = points[i];
        Vec2 d0 = hasBefore ? safeNormalize(here - points[(i + count - 1) % count], Vec2{1, 0}) : Vec2{0, 0};
        Vec2 d1 = hasAfter ? safeNormalize(points[(i + 1) % count] - here, Vec2{1, 0}) : d0;
        if (!hasBefore) d0 = d1;
        Vec2 side = (Vec2{-d0.y, d0.x} + Vec2{-d1.y, d1.x}) * 0.5f;
        inner_[size_t(i)] = side / std::max(simd::dot(side, side), 0.5f);
    }

    Batch& batch = batchFor(nullptr, false);
    size_t first = vertices_.size();
    Vec4 clear{color.x, color.y, color.z, 0.0f};
    int segments = isClosed ? count : count - 1;
    for (int i = 0; i < segments; i++) {
        size_t a = size_t(i), b = size_t((i + 1) % count);
        Vec2 pa = points[a], pb = points[b], na = inner_[a], nb = inner_[b];
        // Across the line: transparent, solid, solid, transparent.
        const float offsets[4] = {edge, core, -core, -edge};
        const Vec4 colors[4] = {clear, color, color, clear};
        for (int strip = 0; strip < 3; strip++) {
            if (strip == 1 && core < 1e-4f) continue;
            Vec2 a0 = pa + na * offsets[strip], a1 = pa + na * offsets[strip + 1];
            Vec2 b0 = pb + nb * offsets[strip], b1 = pb + nb * offsets[strip + 1];
            vertex(a0, colors[strip]);
            vertex(a1, colors[strip + 1]);
            vertex(b1, colors[strip + 1]);
            vertex(a0, colors[strip]);
            vertex(b1, colors[strip + 1]);
            vertex(b0, colors[strip]);
        }
    }
    batch.vertexCount += uint32_t(vertices_.size() - first);
}

/// Enough segments that a circle of this radius looks round, and no more.
int Canvas::segmentsFor(float radius) const {
    return std::clamp(int(8.0f + radius / pixelSize_ * 0.9f), 12, 96);
}

void Canvas::arcPath(Vec2 center, float radius, float fromAngle, float toAngle, int segments) {
    for (int i = 0; i <= segments; i++) {
        float angle = fromAngle + (toAngle - fromAngle) * float(i) / float(segments);
        path_.push_back(center + Vec2{std::cos(angle), std::sin(angle)} * radius);
    }
}

void Canvas::roundedRectPath(Vec2 position, Vec2 size, float radius) {
    path_.clear();
    radius = std::clamp(radius, 0.0f, std::min(size.x, size.y) * 0.5f);
    Vec2 a = position, c = position + size;
    if (radius < 0.01f) {
        path_ = {a, Vec2{c.x, a.y}, c, Vec2{a.x, c.y}};
        return;
    }
    int steps = std::clamp(int(std::ceil(radius / pixelSize_ * 0.4f)) + 2, 3, 16);
    float quarter = kPi * 0.5f;
    arcPath(Vec2{a.x + radius, a.y + radius}, radius, 2 * quarter, 3 * quarter, steps);     // top left
    arcPath(Vec2{c.x - radius, a.y + radius}, radius, 3 * quarter, 4 * quarter, steps);     // top right
    arcPath(Vec2{c.x - radius, c.y - radius}, radius, 0, quarter, steps);                   // bottom right
    arcPath(Vec2{a.x + radius, c.y - radius}, radius, quarter, 2 * quarter, steps);         // bottom left
}

// ---------------------------------------------------------------- shapes

void Canvas::rect(Vec2 position, Vec2 size, Vec4 color) {
    image(nullptr, position, size, color);
}

void Canvas::rectOutline(Vec2 position, Vec2 size, float thickness, Vec4 color) {
    rect(position, Vec2{size.x, thickness}, color);
    rect(Vec2{position.x, position.y + size.y - thickness}, Vec2{size.x, thickness}, color);
    rect(Vec2{position.x, position.y + thickness}, Vec2{thickness, size.y - 2 * thickness}, color);
    rect(Vec2{position.x + size.x - thickness, position.y + thickness}, Vec2{thickness, size.y - 2 * thickness}, color);
}

void Canvas::line(Vec2 from, Vec2 to, float thickness, Vec4 color) {
    if (simd::length(to - from) < 1e-5f) return;
    const Vec2 points[2] = {from, to};
    strokePath(points, 2, thickness, color, false);
}

void Canvas::circle(Vec2 center, float radius, Vec4 color, int segments) {
    if (radius <= 0) return;
    path_.clear();
    arcPath(center, radius, 0, 2.0f * kPi, segments >= 3 ? segments : segmentsFor(radius));
    path_.pop_back();                               // the last point repeats the first
    fillConvex(path_.data(), int(path_.size()), color);
}

void Canvas::roundedRect(Vec2 position, Vec2 size, float radius, Vec4 color) {
    if (size.x <= 0 || size.y <= 0) return;
    roundedRectPath(position, size, radius);
    fillConvex(path_.data(), int(path_.size()), color);
}

void Canvas::roundedRectOutline(Vec2 position, Vec2 size, float radius, float thickness, Vec4 color) {
    if (size.x <= thickness || size.y <= thickness) return;
    // The line is centered on its path: move the path in by half the thickness to stay inside.
    float inset = thickness * 0.5f;
    roundedRectPath(position + Vec2{inset, inset}, size - Vec2{thickness, thickness}, std::max(radius - inset, 0.0f));
    strokePath(path_.data(), int(path_.size()), thickness, color, true);
}

void Canvas::ring(Vec2 center, float radius, float thickness, Vec4 color) {
    if (radius <= 0) return;
    path_.clear();
    arcPath(center, radius - thickness * 0.5f, 0, 2.0f * kPi, segmentsFor(radius));
    path_.pop_back();
    strokePath(path_.data(), int(path_.size()), thickness, color, true);
}

void Canvas::arc(Vec2 center, float radius, float fromAngle, float toAngle, float thickness, Vec4 color) {
    if (radius <= 0) return;
    float share = std::min(std::abs(toAngle - fromAngle) / (2.0f * kPi), 1.0f);
    path_.clear();
    arcPath(center, radius - thickness * 0.5f, fromAngle, toAngle, std::max(int(float(segmentsFor(radius)) * share), 2));
    strokePath(path_.data(), int(path_.size()), thickness, color, false);
}

void Canvas::triangle(Vec2 a, Vec2 b, Vec2 c, Vec4 color) {
    const Vec2 points[3] = {a, b, c};
    fillConvex(points, 3, color);
}

void Canvas::polygon(const std::vector<Vec2>& points, Vec4 color) {
    fillConvex(points.data(), int(points.size()), color);
}

void Canvas::polyline(const std::vector<Vec2>& points, float thickness, Vec4 color, bool isClosed) {
    strokePath(points.data(), int(points.size()), thickness, color, isClosed);
}

void Canvas::image(const Texture* texture, Vec2 position, Vec2 size, Vec4 tint, Vec2 uvMin, Vec2 uvMax) {
    Vec2 a = position, c = position + size;
    quad(a, Vec2{a.x, c.y}, c, Vec2{c.x, a.y},
         uvMin, Vec2{uvMin.x, uvMax.y}, uvMax, Vec2{uvMax.x, uvMin.y}, tint, texture, false);
}

Vec2 Canvas::measureText(std::string_view text, float size) const {
    return font_ ? font_->measure(text, size) : Vec2{0, 0};
}

void Canvas::text(std::string_view text, Vec2 position, float size, Vec4 color, TextAlign align) {
    if (!font_ || !fontAtlas_) return;
    float scale = size / font_->bakedSize;
    Vec2 atlasSize{font_->atlasWidth, font_->atlasHeight};
    float penY = position.y;

    // Lay the text out one line at a time, because alignment needs each line's width.
    size_t lineStart = 0;
    while (lineStart <= text.size()) {
        size_t lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string_view::npos) lineEnd = text.size();
        std::string_view lineText = text.substr(lineStart, lineEnd - lineStart);

        float penX = position.x;
        if (align != TextAlign::Left) {
            float width = font_->measure(lineText, size).x;
            penX -= align == TextAlign::Center ? width * 0.5f : width;
        }
        for (size_t i = 0; i < lineText.size();) {
            const Glyph* glyph = font_->glyph(decodeUTF8(lineText, i));
            if (!glyph) continue;
            if (glyph->width > 0 && glyph->height > 0) {
                Vec2 a{penX + glyph->offsetX * scale, penY + glyph->offsetY * scale};
                Vec2 c = a + Vec2{glyph->width, glyph->height} * scale;
                Vec2 uvA = Vec2{glyph->x, glyph->y} / atlasSize;
                Vec2 uvC = Vec2{glyph->x + glyph->width, glyph->y + glyph->height} / atlasSize;
                quad(a, Vec2{a.x, c.y}, c, Vec2{c.x, a.y},
                     uvA, Vec2{uvA.x, uvC.y}, uvC, Vec2{uvC.x, uvA.y}, color, fontAtlas_, true);
            }
            penX += glyph->advance * scale;
        }
        penY += font_->lineHeight * scale;
        lineStart = lineEnd + 1;
    }
}

}  // namespace molten
