// Canvas.hpp
// Immediate-mode 2D drawing for HUDs, menus and 2D games.
//
// "Immediate mode" means there are no 2D objects to keep around: every frame the game
// says what to draw, in order, and the canvas turns the calls into triangles. The
// renderer draws them in as few draw calls as possible (one per change of texture).
//
// Coordinates are in points with the origin at the top left, like the window system's.
// Colors are sRGB with straight alpha, the values a color picker shows.
//
// Edges are smoothed without multisampling: every shape that is not an upright rectangle
// gets a border one pixel wide that fades from its color to transparent. The same
// triangles and the same shader do everything; only the vertices know about it.
#pragma once

#include "Assets/Font.hpp"
#include "Rendering/GPUTypes.hpp"

#include <string_view>
#include <vector>

namespace molten {

class Texture;

enum class TextAlign : uint8_t { Left, Center, Right };

/// A color from the usual hexadecimal notation: rgb(0xfe8b3c) is an orange.
inline Vec4 rgb(uint32_t hex, float alpha = 1.0f) {
    return Vec4{float((hex >> 16) & 0xFF) / 255.0f, float((hex >> 8) & 0xFF) / 255.0f, float(hex & 0xFF) / 255.0f, alpha};
}

class Canvas {
public:
    /// The drawable area, in points.
    Vec2 size() const { return size_; }

    /// One screen pixel, in points: 1 on a plain screen, 0.5 on a Retina display.
    float pixelSize() const { return pixelSize_; }

    void rect(Vec2 position, Vec2 size, Vec4 color);
    void rectOutline(Vec2 position, Vec2 size, float thickness, Vec4 color);
    void line(Vec2 from, Vec2 to, float thickness, Vec4 color);
    /// A filled circle. With `segments` 0 the canvas chooses enough of them for the radius.
    void circle(Vec2 center, float radius, Vec4 color, int segments = 0);

    // ---- more shapes, all with smooth edges
    void roundedRect(Vec2 position, Vec2 size, float radius, Vec4 color);
    void roundedRectOutline(Vec2 position, Vec2 size, float radius, float thickness, Vec4 color);
    /// The outline of a circle, drawn inside `radius`.
    void ring(Vec2 center, float radius, float thickness, Vec4 color);
    /// Part of a ring. Angles are in radians, clockwise on screen, 0 pointing right.
    void arc(Vec2 center, float radius, float fromAngle, float toAngle, float thickness, Vec4 color);
    void triangle(Vec2 a, Vec2 b, Vec2 c, Vec4 color);
    /// Any convex shape, from its corners in order (either direction).
    void polygon(const std::vector<Vec2>& points, Vec4 color);
    /// A line through several points. `isClosed` joins the last point to the first.
    void polyline(const std::vector<Vec2>& points, float thickness, Vec4 color, bool isClosed = false);

    // ---- clipping: until popClip, nothing is drawn outside the rectangle. Clips nest:
    // a clip inside another one is cut to it.
    void pushClip(Vec2 position, Vec2 size);
    void popClip();

    /// Draws a texture, or part of one: `uvMin` and `uvMax` select a region, which is
    /// how sprite sheets work.
    void image(const Texture* texture, Vec2 position, Vec2 size, Vec4 tint = Vec4{1, 1, 1, 1},
               Vec2 uvMin = Vec2{0, 0}, Vec2 uvMax = Vec2{1, 1});

    /// Draws UTF-8 text. `position` is the top left of the first line (or its top center,
    /// or top right, depending on `align`); `size` is the font size in points.
    void text(std::string_view text, Vec2 position, float size, Vec4 color, TextAlign align = TextAlign::Left);
    Vec2 measureText(std::string_view text, float size) const;

    // ---- used by the engine

    struct Batch {
        const Texture* texture = nullptr;   // nullptr draws with solid color
        bool isText = false;                // text samples a distance field instead of colors
        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;
        bool isClipped = false;             // draw only inside clipMin ... clipMax (in points)
        Vec2 clipMin{0, 0}, clipMax{0, 0};
    };

    /// `pixelsPerPoint` is 2 on a Retina display: the canvas needs it to make edges one pixel soft.
    void begin(Vec2 sizeInPoints, float pixelsPerPoint = 1.0f);
    /// Until endArea, drawing happens in a part of the screen as if it were all of it:
    /// size() is the area's, (0, 0) is its top left corner, and nothing shows outside.
    /// The editor uses it to keep a game's interface inside the scene view.
    /// With `scale`, the area is drawn larger or smaller: size() is then size / scale, so that an
    /// interface laid out for one screen fills another of a different size.
    void beginArea(Vec2 origin, Vec2 size, float scale = 1.0f);
    void endArea();
    void setFont(const FontMetrics* metrics, const Texture* atlas);
    const std::vector<gpu::CanvasVertex>& vertices() const { return vertices_; }
    const std::vector<Batch>& batches() const { return batches_; }
    /// The distance-field spread of the current font, which the text shader needs.
    float fontDistanceRange() const { return font_ ? font_->distanceRange : 1.0f; }

private:
    void quad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, Vec2 uvA, Vec2 uvB, Vec2 uvC, Vec2 uvD, Vec4 color,
              const Texture* texture, bool isText);
    void vertex(Vec2 position, Vec4 color);
    void fillConvex(const Vec2* points, int count, Vec4 color);
    void strokePath(const Vec2* points, int count, float thickness, Vec4 color, bool isClosed);
    void roundedRectPath(Vec2 position, Vec2 size, float radius);
    void arcPath(Vec2 center, float radius, float fromAngle, float toAngle, int segments);
    int segmentsFor(float radius) const;
    Batch& batchFor(const Texture* texture, bool isText);

    struct Clip {
        Vec2 min{0, 0}, max{0, 0};
    };
    Vec2 size_{0, 0};
    float pixelSize_ = 1.0f;
    Vec2 areaOrigin_{0, 0}, sizeOutsideArea_{0, 0};
    float areaScale_ = 1.0f, pixelSizeOutsideArea_ = 1.0f;
    size_t areaFirstVertex_ = 0, areaFirstBatch_ = 0;
    bool isInArea_ = false;
    bool startsNewBatch_ = false;           // the next shape does not join the batch before it
    std::vector<Clip> clips_;
    bool isClipEmpty_ = false;              // the current clip has no area: nothing can be drawn
    std::vector<Vec2> path_, inner_, outer_;    // scratch space, kept to avoid allocating per shape
    const FontMetrics* font_ = nullptr;
    const Texture* fontAtlas_ = nullptr;
    std::vector<gpu::CanvasVertex> vertices_;
    std::vector<Batch> batches_;
};

}  // namespace molten
