#include "ProjectWindow.hpp"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <future>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace molten::editor {

namespace {

namespace fs = std::filesystem;

const ImU32 kAccent = IM_COL32(255, 122, 47, 255);
const ImU32 kSidebar = IM_COL32(23, 25, 29, 255);
const ImU32 kCard = IM_COL32(36, 40, 47, 255);
const ImU32 kCardHover = IM_COL32(44, 49, 58, 255);
const ImU32 kCardBorder = IM_COL32(58, 64, 75, 255);
const ImU32 kText = IM_COL32(223, 227, 234, 255);
const ImU32 kDim = IM_COL32(139, 147, 161, 255);

/// "/Users/ada/Documents/Game" -> "~/Documents/Game".
std::string withTilde(const std::string& path) {
    const char* home = std::getenv("HOME");
    if (home && *home && path.compare(0, std::strlen(home), home) == 0) return "~" + path.substr(std::strlen(home));
    return path;
}

/// The text, or its beginning and end around "..." if it is wider than `width`.
std::string fitted(const std::string& text, float width) {
    if (ImGui::CalcTextSize(text.c_str()).x <= width) return text;
    size_t keep = text.size();
    while (keep > 8) {
        keep--;
        size_t head = keep / 3, tail = keep - head;
        std::string shorter = text.substr(0, head) + "..." + text.substr(text.size() - tail);
        if (ImGui::CalcTextSize(shorter.c_str()).x <= width) return shorter;
    }
    return text.substr(0, 8) + "...";
}

/// The engine's mark: a square of hot metal with an M poured into it.
void drawMark(ImDrawList* drawList, ImVec2 at, float size) {
    drawList->AddRectFilled(at, ImVec2(at.x + size, at.y + size), kAccent, size * 0.24f);
    drawList->AddRectFilledMultiColor(ImVec2(at.x + size * 0.2f, at.y + size * 0.5f), ImVec2(at.x + size * 0.8f, at.y + size * 0.86f),
                                      IM_COL32(255, 190, 90, 0), IM_COL32(255, 190, 90, 0), IM_COL32(255, 200, 110, 90), IM_COL32(255, 200, 110, 90));
    auto p = [&](float x, float y) { return ImVec2(at.x + size * x, at.y + size * y); };
    const ImVec2 m[5] = {p(0.26f, 0.72f), p(0.26f, 0.29f), p(0.5f, 0.58f), p(0.74f, 0.29f), p(0.74f, 0.72f)};
    drawList->AddPolyline(m, 5, IM_COL32(28, 22, 18, 255), ImDrawFlags_None, size * 0.095f);
}

// ---------------------------------------------------------------- pictures of the templates

/// A picture for a template, drawn rather than loaded: it is always sharp, and a template
/// needs no image file. `key` is the template's folder name; "" is the empty project.
void drawPicture(ImDrawList* drawList, ImVec2 min, ImVec2 max, const std::string& key, float rounding) {
    const float width = max.x - min.x, height = max.y - min.y;
    auto p = [&](float x, float y) { return ImVec2(min.x + width * x, min.y + height * y); };
    auto background = [&](ImU32 color) { drawList->AddRectFilled(min, max, color, rounding, ImDrawFlags_RoundCornersTop); };
    drawList->PushClipRect(min, max, true);

    if (key == "Driving") {
        background(IM_COL32(96, 140, 196, 255));
        drawList->AddRectFilled(p(0, 0.38f), p(1, 0.56f), IM_COL32(150, 180, 214, 255));
        drawList->AddRectFilled(p(0, 0.54f), p(1, 1), IM_COL32(44, 46, 52, 255));                      // the road
        for (int i = 0; i < 6; i++) drawList->AddRectFilled(p(0.02f + float(i) * 0.19f, 0.86f), p(0.12f + float(i) * 0.19f, 0.885f), IM_COL32(220, 220, 210, 255));
        // The car from the side, nose down under braking: the front spring pushed in, the rear one out.
        const float x = 0.5f, y = 0.7f;
        const ImVec2 body[6] = {p(x - 0.3f, y - 0.03f), p(x - 0.27f, y - 0.13f), p(x + 0.3f, y - 0.17f), p(x + 0.31f, y - 0.07f), p(x + 0.3f, y - 0.01f), p(x - 0.3f, y + 0.03f)};
        drawList->AddConvexPolyFilled(body, 6, IM_COL32(206, 40, 32, 255));
        const ImVec2 cabin[4] = {p(x - 0.14f, y - 0.135f), p(x - 0.08f, y - 0.25f), p(x + 0.14f, y - 0.265f), p(x + 0.22f, y - 0.165f)};
        drawList->AddConvexPolyFilled(cabin, 4, IM_COL32(30, 40, 52, 255));
        const float wheelsAt[2][2] = {{x - 0.19f, y + 0.045f}, {x + 0.2f, y + 0.035f}};
        for (const auto& wheel : wheelsAt) {
            ImVec2 center = p(wheel[0], wheel[1]);
            float radius = height * 0.085f;
            // Flattened where it meets the road.
            drawList->AddEllipseFilled(ImVec2(center.x, center.y + radius * 0.06f), ImVec2(radius * 1.04f, radius * 0.94f), IM_COL32(16, 16, 18, 255));
            drawList->AddCircleFilled(center, radius * 0.5f, IM_COL32(170, 174, 182, 255), 20);
        }
        // Smoke off the front tire.
        for (int i = 0; i < 4; i++) drawList->AddCircleFilled(p(x - 0.3f - float(i) * 0.07f, y + 0.06f - float(i) * 0.012f), height * (0.035f + 0.016f * float(i)), IM_COL32(230, 230, 235, 110 - i * 22), 16);
    } else if (key == "Car Game") {
        // A circuit through a park: the road bending away between the trees, a star over it, the car on it.
        background(IM_COL32(122, 164, 214, 255));
        drawList->AddRectFilled(p(0, 0.4f), p(1, 0.5f), IM_COL32(168, 196, 224, 255));
        const ImVec2 hills[5] = {p(0, 0.5f), p(0.22f, 0.36f), p(0.5f, 0.47f), p(0.8f, 0.33f), p(1, 0.46f)};
        for (int i = 0; i + 1 < 5; i++) drawList->AddQuadFilled(hills[i], hills[i + 1], ImVec2(hills[i + 1].x, min.y + height * 0.56f), ImVec2(hills[i].x, min.y + height * 0.56f), IM_COL32(72, 128, 70, 255));
        drawList->AddRectFilled(p(0, 0.5f), p(1, 1), IM_COL32(88, 148, 72, 255));
        const ImVec2 road[4] = {p(0.2f, 1.02f), p(0.62f, 0.5f), p(0.7f, 0.5f), p(0.98f, 1.02f)};
        drawList->AddConvexPolyFilled(road, 4, IM_COL32(46, 48, 54, 255));
        for (int i = 0; i < 4; i++) {
            float t = float(i) / 4.0f, y0 = 0.56f + 0.44f * t * t, y1 = 0.56f + 0.44f * (t + 0.11f) * (t + 0.11f);
            float x0 = 0.662f - (y0 - 0.5f) * 0.14f, x1 = 0.662f - (y1 - 0.5f) * 0.14f, half = 0.004f + 0.012f * t;
            drawList->AddQuadFilled(p(x0 - half, y0), p(x0 + half, y0), p(x1 + half, y1), p(x1 - half, y1), IM_COL32(226, 226, 216, 255));
        }
        for (int i = 0; i < 5; i++) {                                                                // pines
            float x = i < 3 ? 0.08f + 0.13f * float(i) : 0.84f + 0.1f * float(i - 3), base = 0.6f + 0.05f * float(i % 2), size = 0.1f + 0.03f * float(i % 3);
            drawList->AddRectFilled(p(x - 0.006f, base - 0.03f), p(x + 0.006f, base + 0.02f), IM_COL32(74, 50, 34, 255));
            drawList->AddTriangleFilled(p(x, base - size * 1.9f), p(x - size * 0.42f, base - 0.02f), p(x + size * 0.42f, base - 0.02f), IM_COL32(34, 92, 46, 255));
        }
        ImVec2 star = p(0.66f, 0.42f);
        float glow = height * 0.05f;
        drawList->AddCircleFilled(star, glow * 1.9f, IM_COL32(255, 210, 80, 60), 20);
        drawList->AddQuadFilled(ImVec2(star.x, star.y - glow), ImVec2(star.x + glow * 0.7f, star.y), ImVec2(star.x, star.y + glow), ImVec2(star.x - glow * 0.7f, star.y), IM_COL32(255, 196, 40, 255));
        // The car, from behind and a little to one side.
        const float x = 0.5f, y = 0.8f;
        drawList->AddEllipseFilled(p(x, y + 0.075f), ImVec2(width * 0.15f, height * 0.035f), IM_COL32(0, 0, 0, 80));
        const ImVec2 body[4] = {p(x - 0.13f, y + 0.06f), p(x - 0.12f, y - 0.03f), p(x + 0.12f, y - 0.03f), p(x + 0.13f, y + 0.06f)};
        drawList->AddConvexPolyFilled(body, 4, IM_COL32(204, 38, 28, 255));
        const ImVec2 cabin[4] = {p(x - 0.1f, y - 0.03f), p(x - 0.075f, y - 0.12f), p(x + 0.075f, y - 0.12f), p(x + 0.1f, y - 0.03f)};
        drawList->AddConvexPolyFilled(cabin, 4, IM_COL32(26, 36, 48, 255));
        drawList->AddRectFilled(p(x - 0.08f, y - 0.135f), p(x + 0.08f, y - 0.115f), IM_COL32(178, 30, 22, 255));
        drawList->AddRectFilled(p(x - 0.115f, y - 0.005f), p(x - 0.07f, y + 0.02f), IM_COL32(255, 70, 50, 255));
        drawList->AddRectFilled(p(x + 0.07f, y - 0.005f), p(x + 0.115f, y + 0.02f), IM_COL32(255, 70, 50, 255));
        drawList->AddRectFilled(p(x - 0.135f, y + 0.03f), p(x - 0.095f, y + 0.1f), IM_COL32(14, 14, 16, 255), 2);
        drawList->AddRectFilled(p(x + 0.095f, y + 0.03f), p(x + 0.135f, y + 0.1f), IM_COL32(14, 14, 16, 255), 2);
    } else if (key == "Plane Game") {
        // Above the fields: the aeroplane banking toward the next ring, more of them leading away.
        background(IM_COL32(86, 138, 206, 255));
        drawList->AddRectFilled(p(0, 0.55f), p(1, 0.78f), IM_COL32(150, 186, 224, 255));
        drawList->AddRectFilled(p(0, 0.76f), p(1, 1), IM_COL32(92, 140, 76, 255));
        const ImVec2 fields[3][4] = {{p(0.05f, 0.84f), p(0.3f, 0.8f), p(0.4f, 0.9f), p(0.1f, 0.96f)}, {p(0.5f, 0.82f), p(0.72f, 0.8f), p(0.86f, 0.9f), p(0.58f, 0.93f)}, {p(0.3f, 0.95f), p(0.52f, 0.92f), p(0.6f, 1.02f), p(0.3f, 1.02f)}};
        const ImU32 fieldColors[3] = {IM_COL32(148, 152, 70, 255), IM_COL32(70, 118, 62, 255), IM_COL32(170, 160, 84, 255)};
        for (int i = 0; i < 3; i++) drawList->AddConvexPolyFilled(fields[i], 4, fieldColors[i]);
        for (int i = 0; i < 3; i++) {                                                                // clouds
            ImVec2 at = p(0.16f + 0.34f * float(i), 0.16f + 0.1f * float(i % 2));
            for (int k = 0; k < 4; k++) drawList->AddCircleFilled(ImVec2(at.x + float(k - 1) * height * 0.07f, at.y + (k % 2 ? -1.0f : 1.0f) * height * 0.012f), height * (0.05f + 0.012f * float(k % 2)), IM_COL32(255, 255, 255, 200), 20);
        }
        for (int i = 2; i >= 0; i--) {                                                               // rings, the far ones first
            ImVec2 at = p(0.74f - 0.13f * float(i), 0.42f - 0.07f * float(i));
            float radius = height * (0.17f - 0.045f * float(i));
            drawList->AddEllipse(at, ImVec2(radius * 0.62f, radius), IM_COL32(255, 138, 30, 255), 0.25f, 32, std::max(1.5f, radius * 0.12f));
        }
        // The aeroplane, seen from behind and above, its right wing down.
        const float x = 0.36f, y = 0.6f;
        const ImVec2 wing[4] = {p(x - 0.25f, y - 0.12f), p(x - 0.23f, y - 0.15f), p(x + 0.25f, y + 0.07f), p(x + 0.24f, y + 0.105f)};
        drawList->AddConvexPolyFilled(wing, 4, IM_COL32(238, 238, 232, 255));
        const ImVec2 body[4] = {p(x - 0.03f, y - 0.11f), p(x + 0.03f, y - 0.1f), p(x + 0.022f, y + 0.19f), p(x - 0.014f, y + 0.19f)};
        drawList->AddConvexPolyFilled(body, 4, IM_COL32(222, 222, 216, 255));
        const ImVec2 tail[4] = {p(x - 0.09f, y + 0.13f), p(x - 0.08f, y + 0.11f), p(x + 0.1f, y + 0.19f), p(x + 0.095f, y + 0.21f)};
        drawList->AddConvexPolyFilled(tail, 4, IM_COL32(238, 238, 232, 255));
        const ImVec2 fin[3] = {p(x + 0.004f, y + 0.2f), p(x - 0.03f, y + 0.07f), p(x + 0.012f, y + 0.12f)};
        drawList->AddConvexPolyFilled(fin, 3, IM_COL32(204, 38, 28, 255));
        drawList->AddQuadFilled(p(x - 0.02f, y - 0.07f), p(x + 0.02f, y - 0.065f), p(x + 0.018f, y + 0.0f), p(x - 0.016f, y - 0.005f), IM_COL32(26, 36, 48, 255));
    } else if (key == "Boat Game") {
        // Open water: the boat and its wake, a buoy to round, an island with its lighthouse.
        background(IM_COL32(130, 176, 222, 255));
        drawList->AddRectFilled(p(0, 0.3f), p(1, 0.46f), IM_COL32(178, 206, 232, 255));
        drawList->AddRectFilled(p(0, 0.45f), p(1, 1), IM_COL32(24, 96, 138, 255));
        drawList->AddRectFilled(p(0, 0.45f), p(1, 0.56f), IM_COL32(40, 122, 160, 255));
        for (int i = 0; i < 7; i++) {                                                                // light on the waves
            float y = 0.52f + 0.07f * float(i), x = 0.1f + 0.13f * float((i * 5) % 7);
            drawList->AddRectFilled(p(x, y), p(x + 0.1f + 0.02f * float(i), y + 0.008f), IM_COL32(120, 190, 220, 120), 2);
        }
        const ImVec2 island[5] = {p(0.6f, 0.455f), p(0.68f, 0.39f), p(0.8f, 0.37f), p(0.9f, 0.41f), p(0.97f, 0.455f)};
        drawList->AddConvexPolyFilled(island, 5, IM_COL32(206, 188, 130, 255));
        drawList->AddTriangleFilled(p(0.7f, 0.3f), p(0.675f, 0.39f), p(0.725f, 0.39f), IM_COL32(34, 92, 46, 255));
        drawList->AddTriangleFilled(p(0.75f, 0.31f), p(0.73f, 0.38f), p(0.77f, 0.38f), IM_COL32(40, 104, 52, 255));
        drawList->AddRectFilled(p(0.845f, 0.22f), p(0.875f, 0.4f), IM_COL32(236, 236, 230, 255));     // the lighthouse
        drawList->AddRectFilled(p(0.845f, 0.28f), p(0.875f, 0.32f), IM_COL32(200, 40, 30, 255));
        drawList->AddRectFilled(p(0.838f, 0.19f), p(0.882f, 0.225f), IM_COL32(255, 226, 120, 255));
        drawList->AddTriangleFilled(p(0.86f, 0.15f), p(0.836f, 0.19f), p(0.884f, 0.19f), IM_COL32(200, 40, 30, 255));
        // A buoy, and the ring over it.
        ImVec2 buoy = p(0.22f, 0.6f);
        drawList->AddTriangleFilled(ImVec2(buoy.x, buoy.y - height * 0.12f), ImVec2(buoy.x - height * 0.04f, buoy.y), ImVec2(buoy.x + height * 0.04f, buoy.y), IM_COL32(214, 44, 30, 255));
        drawList->AddCircleFilled(ImVec2(buoy.x, buoy.y - height * 0.125f), height * 0.016f, IM_COL32(255, 220, 90, 255), 12);
        drawList->AddEllipse(ImVec2(buoy.x, buoy.y - height * 0.2f), ImVec2(height * 0.07f, height * 0.085f), IM_COL32(255, 200, 70, 255), 0, 28, 2.5f);
        // The boat, bow up, and the wake it leaves.
        const float x = 0.52f, y = 0.74f;
        const ImVec2 wakeL[3] = {p(x - 0.04f, y + 0.03f), p(x - 0.32f, y + 0.3f), p(x - 0.12f, y + 0.3f)}, wakeR[3] = {p(x + 0.05f, y + 0.03f), p(x + 0.18f, y + 0.3f), p(x + 0.38f, y + 0.3f)};
        drawList->AddConvexPolyFilled(wakeL, 3, IM_COL32(220, 238, 246, 150));
        drawList->AddConvexPolyFilled(wakeR, 3, IM_COL32(220, 238, 246, 150));
        const ImVec2 hull[5] = {p(x - 0.11f, y + 0.05f), p(x - 0.12f, y - 0.03f), p(x + 0.02f, y - 0.1f), p(x + 0.13f, y - 0.03f), p(x + 0.11f, y + 0.05f)};
        drawList->AddConvexPolyFilled(hull, 5, IM_COL32(240, 240, 234, 255));
        drawList->AddQuadFilled(p(x - 0.115f, y + 0.01f), p(x + 0.125f, y + 0.01f), p(x + 0.112f, y + 0.05f), p(x - 0.11f, y + 0.05f), IM_COL32(18, 60, 140, 255));
        drawList->AddQuadFilled(p(x - 0.06f, y - 0.05f), p(x + 0.07f, y - 0.05f), p(x + 0.055f, y - 0.1f), p(x - 0.04f, y - 0.1f), IM_COL32(26, 36, 48, 255));
    } else if (key == "Third Person Game") {
        // A garden of stone: the figure seen from behind, as the game shows it, a gem ahead, steps and a column beyond.
        background(IM_COL32(118, 164, 214, 255));
        drawList->AddRectFilled(p(0, 0.34f), p(1, 0.52f), IM_COL32(170, 200, 228, 255));
        const ImVec2 hillA[4] = {p(-0.05f, 0.52f), p(0.16f, 0.36f), p(0.36f, 0.4f), p(0.5f, 0.52f)}, hillB[4] = {p(0.5f, 0.52f), p(0.72f, 0.38f), p(0.9f, 0.4f), p(1.05f, 0.52f)};
        drawList->AddConvexPolyFilled(hillA, 4, IM_COL32(92, 148, 86, 255));
        drawList->AddConvexPolyFilled(hillB, 4, IM_COL32(80, 136, 78, 255));
        drawList->AddRectFilled(p(0, 0.5f), p(1, 1), IM_COL32(70, 128, 62, 255));
        const ImVec2 way[4] = {p(0.42f, 0.5f), p(0.5f, 0.5f), p(0.74f, 1), p(0.2f, 1)};
        drawList->AddConvexPolyFilled(way, 4, IM_COL32(196, 186, 156, 255));
        // The terrace, its steps and a column on it.
        drawList->AddRectFilled(p(0.04f, 0.4f), p(0.4f, 0.56f), IM_COL32(168, 164, 156, 255));
        drawList->AddRectFilled(p(0.04f, 0.4f), p(0.4f, 0.425f), IM_COL32(198, 194, 186, 255));
        for (int i = 0; i < 4; i++) {
            drawList->AddRectFilled(p(0.27f + 0.012f * float(i), 0.44f + 0.03f * float(i)), p(0.4f, 0.47f + 0.03f * float(i)), i % 2 ? IM_COL32(150, 146, 140, 255) : IM_COL32(128, 126, 124, 255));
        }
        drawList->AddRectFilled(p(0.09f, 0.2f), p(0.125f, 0.4f), IM_COL32(120, 120, 124, 255));
        drawList->AddRectFilled(p(0.075f, 0.185f), p(0.14f, 0.205f), IM_COL32(190, 186, 178, 255));
        // Blocks to jump across, each higher than the last.
        drawList->AddRectFilled(p(0.66f, 0.4f), p(0.76f, 0.56f), IM_COL32(140, 138, 134, 255));
        drawList->AddRectFilled(p(0.8f, 0.34f), p(0.9f, 0.56f), IM_COL32(170, 166, 158, 255));
        // The gems.
        auto gemAt = [&](float x, float y, float size) {
            ImVec2 c = p(x, y);
            float h = height * size;
            const ImVec2 top[3] = {ImVec2(c.x, c.y - h), ImVec2(c.x - h * 0.6f, c.y - h * 0.15f), ImVec2(c.x + h * 0.6f, c.y - h * 0.15f)};
            const ImVec2 bottom[3] = {ImVec2(c.x - h * 0.6f, c.y - h * 0.15f), ImVec2(c.x + h * 0.6f, c.y - h * 0.15f), ImVec2(c.x, c.y + h)};
            drawList->AddCircleFilled(c, h * 1.5f, IM_COL32(60, 240, 220, 40), 24);
            drawList->AddConvexPolyFilled(top, 3, IM_COL32(120, 250, 236, 255));
            drawList->AddConvexPolyFilled(bottom, 3, IM_COL32(30, 190, 176, 255));
        };
        gemAt(0.46f, 0.62f, 0.05f);
        gemAt(0.71f, 0.3f, 0.035f);
        gemAt(0.85f, 0.24f, 0.035f);
        // The figure, from behind: helmet, pack, arms and legs in mid stride.
        const float x = 0.5f, y = 0.9f;
        const ImU32 suit = IM_COL32(232, 232, 226, 255), dark = IM_COL32(52, 56, 64, 255), pack = IM_COL32(244, 116, 32, 255);
        drawList->AddEllipseFilled(p(x, y + 0.015f), ImVec2(width * 0.05f, height * 0.016f), IM_COL32(0, 0, 0, 70));
        drawList->AddQuadFilled(p(x - 0.028f, y - 0.2f), p(x - 0.004f, y - 0.2f), p(x - 0.03f, y - 0.01f), p(x - 0.052f, y - 0.02f), suit);       // the legs
        drawList->AddQuadFilled(p(x + 0.004f, y - 0.2f), p(x + 0.028f, y - 0.2f), p(x + 0.04f, y - 0.07f), p(x + 0.018f, y - 0.06f), suit);
        drawList->AddRectFilled(p(x - 0.056f, y - 0.03f), p(x - 0.026f, y), dark, 2);
        drawList->AddRectFilled(p(x + 0.016f, y - 0.085f), p(x + 0.044f, y - 0.05f), dark, 2);
        drawList->AddQuadFilled(p(x - 0.05f, y - 0.36f), p(x - 0.034f, y - 0.36f), p(x - 0.056f, y - 0.2f), p(x - 0.07f, y - 0.21f), suit);        // the arms
        drawList->AddQuadFilled(p(x + 0.034f, y - 0.36f), p(x + 0.05f, y - 0.36f), p(x + 0.064f, y - 0.22f), p(x + 0.05f, y - 0.21f), suit);
        drawList->AddRectFilled(p(x - 0.036f, y - 0.38f), p(x + 0.036f, y - 0.19f), suit, 4);                                                    // the body
        drawList->AddRectFilled(p(x - 0.034f, y - 0.23f), p(x + 0.034f, y - 0.19f), dark, 3);
        drawList->AddRectFilled(p(x - 0.027f, y - 0.37f), p(x + 0.027f, y - 0.25f), pack, 3);
        drawList->AddRectFilled(p(x - 0.017f, y - 0.345f), p(x + 0.017f, y - 0.285f), dark, 2);
        drawList->AddEllipseFilled(p(x, y - 0.435f), ImVec2(width * 0.026f, height * 0.052f), IM_COL32(246, 246, 242, 255));                       // the helmet
        drawList->AddRectFilled(p(x - 0.006f, y - 0.487f), p(x + 0.006f, y - 0.44f), pack);
    } else if (key == "Online Game") {
        // The garden with company: two figures, each with a pack of its own color, and the gem between them.
        background(IM_COL32(118, 164, 214, 255));
        drawList->AddRectFilled(p(0, 0.34f), p(1, 0.52f), IM_COL32(170, 200, 228, 255));
        const ImVec2 hillA[4] = {p(-0.05f, 0.52f), p(0.2f, 0.37f), p(0.4f, 0.41f), p(0.55f, 0.52f)}, hillB[4] = {p(0.45f, 0.52f), p(0.7f, 0.36f), p(0.92f, 0.4f), p(1.05f, 0.52f)};
        drawList->AddConvexPolyFilled(hillA, 4, IM_COL32(92, 148, 86, 255));
        drawList->AddConvexPolyFilled(hillB, 4, IM_COL32(80, 136, 78, 255));
        drawList->AddRectFilled(p(0, 0.5f), p(1, 1), IM_COL32(70, 128, 62, 255));
        const ImVec2 square[4] = {p(0.2f, 0.62f), p(0.8f, 0.62f), p(1.0f, 1), p(0.0f, 1)};
        drawList->AddConvexPolyFilled(square, 4, IM_COL32(196, 186, 156, 255));
        drawList->AddRectFilled(p(0.06f, 0.36f), p(0.3f, 0.56f), IM_COL32(168, 164, 156, 255));          // a terrace to one side
        drawList->AddRectFilled(p(0.06f, 0.36f), p(0.3f, 0.385f), IM_COL32(198, 194, 186, 255));
        drawList->AddRectFilled(p(0.74f, 0.4f), p(0.84f, 0.56f), IM_COL32(140, 138, 134, 255));          // blocks to the other
        drawList->AddRectFilled(p(0.87f, 0.33f), p(0.96f, 0.56f), IM_COL32(170, 166, 158, 255));
        {
            ImVec2 c = p(0.5f, 0.5f);
            float h = height * 0.06f;
            const ImVec2 top[3] = {ImVec2(c.x, c.y - h), ImVec2(c.x - h * 0.6f, c.y - h * 0.15f), ImVec2(c.x + h * 0.6f, c.y - h * 0.15f)};
            const ImVec2 bottom[3] = {ImVec2(c.x - h * 0.6f, c.y - h * 0.15f), ImVec2(c.x + h * 0.6f, c.y - h * 0.15f), ImVec2(c.x, c.y + h)};
            drawList->AddCircleFilled(c, h * 1.6f, IM_COL32(60, 240, 220, 44), 24);
            drawList->AddConvexPolyFilled(top, 3, IM_COL32(120, 250, 236, 255));
            drawList->AddConvexPolyFilled(bottom, 3, IM_COL32(30, 190, 176, 255));
        }
        auto figure = [&](float x, float y, float lean, ImU32 pack) {
            const ImU32 suit = IM_COL32(232, 232, 226, 255), dark = IM_COL32(52, 56, 64, 255);
            drawList->AddEllipseFilled(p(x, y + 0.015f), ImVec2(width * 0.05f, height * 0.016f), IM_COL32(0, 0, 0, 70));
            drawList->AddQuadFilled(p(x - 0.028f, y - 0.2f), p(x - 0.004f, y - 0.2f), p(x - 0.03f - lean, y - 0.01f), p(x - 0.052f - lean, y - 0.02f), suit);
            drawList->AddQuadFilled(p(x + 0.004f, y - 0.2f), p(x + 0.028f, y - 0.2f), p(x + 0.04f + lean, y - 0.07f), p(x + 0.018f + lean, y - 0.06f), suit);
            drawList->AddRectFilled(p(x - 0.056f - lean, y - 0.03f), p(x - 0.026f - lean, y), dark, 2);
            drawList->AddRectFilled(p(x + 0.016f + lean, y - 0.085f), p(x + 0.044f + lean, y - 0.05f), dark, 2);
            drawList->AddQuadFilled(p(x - 0.05f, y - 0.36f), p(x - 0.034f, y - 0.36f), p(x - 0.056f, y - 0.2f), p(x - 0.07f, y - 0.21f), suit);
            drawList->AddQuadFilled(p(x + 0.034f, y - 0.36f), p(x + 0.05f, y - 0.36f), p(x + 0.064f, y - 0.22f), p(x + 0.05f, y - 0.21f), suit);
            drawList->AddRectFilled(p(x - 0.036f, y - 0.38f), p(x + 0.036f, y - 0.19f), suit, 4);
            drawList->AddRectFilled(p(x - 0.034f, y - 0.23f), p(x + 0.034f, y - 0.19f), dark, 3);
            drawList->AddRectFilled(p(x - 0.027f, y - 0.37f), p(x + 0.027f, y - 0.25f), pack, 3);
            drawList->AddRectFilled(p(x - 0.017f, y - 0.345f), p(x + 0.017f, y - 0.285f), dark, 2);
            drawList->AddEllipseFilled(p(x, y - 0.435f), ImVec2(width * 0.026f, height * 0.052f), IM_COL32(246, 246, 242, 255));
            drawList->AddRectFilled(p(x - 0.006f, y - 0.487f), p(x + 0.006f, y - 0.44f), pack);
        };
        figure(0.36f, 0.92f, 0.0f, IM_COL32(244, 116, 32, 255));
        figure(0.64f, 0.9f, 0.006f, IM_COL32(52, 140, 220, 255));
    } else if (key == "Shooter Game") {
        // A training yard seen down the rifle: walls, a target with its rings, a machine in the air, the sights in the middle.
        background(IM_COL32(104, 140, 186, 255));
        drawList->AddRectFilled(p(0, 0.3f), p(1, 0.5f), IM_COL32(160, 184, 208, 255));
        drawList->AddRectFilled(p(0, 0.48f), p(1, 1), IM_COL32(150, 142, 124, 255));
        const ImVec2 floor[4] = {p(0.3f, 0.48f), p(0.7f, 0.48f), p(1.1f, 1), p(-0.1f, 1)};
        drawList->AddConvexPolyFilled(floor, 4, IM_COL32(166, 158, 138, 255));
        drawList->AddRectFilled(p(0, 0.3f), p(0.22f, 0.62f), IM_COL32(86, 92, 102, 255));                 // walls to either side
        drawList->AddRectFilled(p(0.78f, 0.26f), p(1, 0.66f), IM_COL32(98, 104, 114, 255));
        drawList->AddRectFilled(p(0.3f, 0.4f), p(0.44f, 0.56f), IM_COL32(124, 96, 60, 255));              // crates to hide behind
        drawList->AddRectFilled(p(0.33f, 0.31f), p(0.42f, 0.4f), IM_COL32(140, 110, 70, 255));
        // The target: rings on a post.
        ImVec2 target = p(0.62f, 0.4f);
        drawList->AddRectFilled(p(0.612f, 0.4f), p(0.628f, 0.6f), IM_COL32(60, 62, 68, 255));
        drawList->AddCircleFilled(target, height * 0.115f, IM_COL32(236, 236, 230, 255), 32);
        drawList->AddCircleFilled(target, height * 0.08f, IM_COL32(214, 44, 30, 255), 32);
        drawList->AddCircleFilled(target, height * 0.045f, IM_COL32(236, 236, 230, 255), 24);
        drawList->AddCircleFilled(target, height * 0.016f, IM_COL32(214, 44, 30, 255), 16);
        // A machine that hunts: a dark ball with one red eye, and what holds it up.
        ImVec2 drone = p(0.22f, 0.24f);
        drawList->AddRectFilled(ImVec2(drone.x - height * 0.12f, drone.y - height * 0.075f), ImVec2(drone.x + height * 0.12f, drone.y - height * 0.06f), IM_COL32(40, 42, 48, 255), 2);
        drawList->AddCircleFilled(drone, height * 0.062f, IM_COL32(58, 62, 70, 255), 24);
        drawList->AddCircleFilled(ImVec2(drone.x + height * 0.018f, drone.y + height * 0.008f), height * 0.022f, IM_COL32(255, 70, 50, 255), 16);
        // The rifle, from behind it, and the flash at its muzzle.
        const ImVec2 barrel[4] = {p(0.585f, 0.74f), p(0.625f, 0.72f), p(0.82f, 1.02f), p(0.7f, 1.02f)};
        drawList->AddCircleFilled(p(0.602f, 0.715f), height * 0.05f, IM_COL32(255, 214, 120, 150), 20);
        drawList->AddCircleFilled(p(0.602f, 0.715f), height * 0.026f, IM_COL32(255, 246, 210, 230), 16);
        drawList->AddConvexPolyFilled(barrel, 4, IM_COL32(44, 46, 52, 255));
        const ImVec2 stock[4] = {p(0.66f, 0.83f), p(0.72f, 0.8f), p(0.9f, 1.02f), p(0.76f, 1.02f)};
        drawList->AddConvexPolyFilled(stock, 4, IM_COL32(244, 116, 32, 255));
        // The sights.
        ImVec2 middle = p(0.5f, 0.5f);
        const ImU32 sight = IM_COL32(255, 255, 255, 235);
        float gap = height * 0.03f, arm = height * 0.05f;
        drawList->AddLine(ImVec2(middle.x - gap - arm, middle.y), ImVec2(middle.x - gap, middle.y), sight, 2.0f);
        drawList->AddLine(ImVec2(middle.x + gap, middle.y), ImVec2(middle.x + gap + arm, middle.y), sight, 2.0f);
        drawList->AddLine(ImVec2(middle.x, middle.y - gap - arm), ImVec2(middle.x, middle.y - gap), sight, 2.0f);
        drawList->AddLine(ImVec2(middle.x, middle.y + gap), ImVec2(middle.x, middle.y + gap + arm), sight, 2.0f);
        drawList->AddCircleFilled(middle, 1.6f, sight, 8);
    } else if (key == "Showcase") {
        // The plaza: a chrome ball in the middle that mirrors the sky and the ground, the stations around it.
        background(IM_COL32(110, 150, 200, 255));
        drawList->AddRectFilled(p(0, 0.4f), p(1, 0.6f), IM_COL32(170, 186, 204, 255));
        drawList->AddRectFilled(p(0, 0.58f), p(1, 1), IM_COL32(118, 116, 112, 255));
        const ImU32 pad = IM_COL32(96, 94, 92, 255);
        drawList->AddEllipseFilled(p(0.16f, 0.7f), ImVec2(width * 0.14f, height * 0.06f), pad);
        drawList->AddEllipseFilled(p(0.84f, 0.7f), ImVec2(width * 0.14f, height * 0.06f), pad);
        drawList->AddRectFilled(p(0.07f, 0.52f), p(0.25f, 0.7f), IM_COL32(58, 60, 66, 255));          // the hall of lights
        drawList->AddRectFilled(p(0.09f, 0.6f), p(0.23f, 0.61f), IM_COL32(80, 220, 255, 255));
        drawList->AddRectFilled(p(0.09f, 0.64f), p(0.23f, 0.65f), IM_COL32(255, 80, 200, 255));
        for (int i = 0; i < 4; i++) {                                                                 // a stack of crates
            drawList->AddRectFilled(p(0.76f + float(i % 2) * 0.06f, 0.6f - float(i / 2) * 0.08f), p(0.81f + float(i % 2) * 0.06f, 0.67f - float(i / 2) * 0.08f),
                                    IM_COL32(170, 118, 64, 255));
        }
        ImVec2 ball = p(0.5f, 0.55f);
        float radius = height * 0.2f;
        drawList->AddEllipseFilled(p(0.5f, 0.78f), ImVec2(width * 0.16f, height * 0.05f), IM_COL32(150, 148, 144, 255));
        drawList->AddRectFilled(p(0.47f, 0.66f), p(0.53f, 0.78f), IM_COL32(150, 148, 144, 255));
        drawList->AddCircleFilled(ball, radius, IM_COL32(120, 128, 138, 255), 40);
        drawList->PathArcTo(ball, radius, 3.4f, 6.0f, 32);                                           // the sky in its upper half
        drawList->PathFillConvex(IM_COL32(186, 210, 236, 255));
        drawList->AddCircleFilled(ImVec2(ball.x - radius * 0.35f, ball.y - radius * 0.4f), radius * 0.16f, IM_COL32(255, 255, 255, 220), 16);
        drawList->AddCircle(ball, radius, IM_COL32(60, 64, 72, 255), 40, 1.5f);
    } else if (key.empty()) {
        // The empty project: a floor that goes to the horizon and one cube on it.
        background(IM_COL32(40, 46, 56, 255));
        drawList->AddRectFilled(p(0, 0.56f), p(1, 1), IM_COL32(31, 35, 43, 255));
        const ImU32 line = IM_COL32(86, 96, 112, 150);
        for (int i = -8; i <= 8; i++) drawList->AddLine(p(0.5f + float(i) * 0.035f, 0.56f), p(0.5f + float(i) * 0.3f, 1.02f), line, 1);
        for (int i = 1; i <= 5; i++) {
            float y = 0.56f + 0.44f * float(i * i) / 25.0f;
            drawList->AddLine(p(0, y), p(1, y), line, 1);
        }
        ImVec2 center = p(0.5f, 0.6f);
        float size = height * 0.17f;
        ImVec2 top(center.x, center.y - size * 1.25f), middle(center.x, center.y - size * 0.25f), bottom(center.x, center.y + size * 0.75f);
        ImVec2 leftTop(center.x - size, center.y - size * 0.75f), rightTop(center.x + size, center.y - size * 0.75f);
        ImVec2 leftBottom(center.x - size, center.y + size * 0.25f), rightBottom(center.x + size, center.y + size * 0.25f);
        drawList->AddEllipseFilled(ImVec2(center.x + size * 0.2f, center.y + size * 0.62f), ImVec2(size * 1.5f, size * 0.36f), IM_COL32(0, 0, 0, 70));
        drawList->AddQuadFilled(top, rightTop, middle, leftTop, IM_COL32(206, 212, 222, 255));
        drawList->AddQuadFilled(leftTop, middle, bottom, leftBottom, IM_COL32(150, 157, 170, 255));
        drawList->AddQuadFilled(middle, rightTop, rightBottom, bottom, IM_COL32(108, 115, 128, 255));
    } else {
        // A template this version has no picture for: its initial.
        background(IM_COL32(46, 52, 64, 255));
        char initial[2] = {key[0], 0};
        float size = height * 0.5f;
        ImVec2 extent = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0, initial);
        drawList->AddText(ImGui::GetFont(), size, ImVec2((min.x + max.x - extent.x) * 0.5f, (min.y + max.y - extent.y) * 0.5f), IM_COL32(255, 255, 255, 70), initial);
    }
    drawList->PopClipRect();
}

const ImU32 kBackground = IM_COL32(17, 18, 22, 255);

/// A small heading over a group of things: "Recent projects", "Name".
void heading(const char* text) {
    ImGui::PushFont(nullptr, 12.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 128, 142, 255));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

/// The button for the one thing a page is for: hot metal, dark text.
bool primaryButton(const char* label, ImVec2 size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(236, 108, 36, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 132, 62, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(212, 92, 26, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(26, 20, 16, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(22, 10));
    ImGui::PushFont(nullptr, 15.0f);
    bool pressed = ImGui::Button(label, size);
    ImGui::PopFont();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    return pressed;
}

/// A quieter button beside a primary one.
bool secondaryButton(const char* label, ImVec2 size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(44, 49, 58, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(56, 62, 73, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(18, 10));
    ImGui::PushFont(nullptr, 15.0f);
    bool pressed = ImGui::Button(label, size);
    ImGui::PopFont();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    return pressed;
}

/// A row of the sidebar that is an action: an icon and a word, highlighted while it is the page shown.
bool navigationRow(const char* id, const char* label, int icon, bool isCurrent) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(at.x - 10, at.y));
    float width = ImGui::GetContentRegionAvail().x + 10;
    ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(255, 255, 255, 14));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(255, 255, 255, 22));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6);
    bool pressed = ImGui::Selectable(id, false, ImGuiSelectableFlags_None, ImVec2(width, 34));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    ImVec2 min(at.x - 10, at.y), max(at.x - 10 + width, at.y + 34);
    if (isCurrent) {
        drawList->AddRectFilled(min, max, kCardHover, 6);
        drawList->AddRectFilled(ImVec2(min.x, min.y + 7), ImVec2(min.x + 3, max.y - 7), kAccent, 2);
    }
    ImVec2 center(at.x + 9, at.y + 17);
    ImU32 color = isCurrent ? kAccent : kText;
    if (icon == 0) {            // a plus: something new
        drawList->AddLine(ImVec2(center.x - 6, center.y), ImVec2(center.x + 6, center.y), color, 1.8f);
        drawList->AddLine(ImVec2(center.x, center.y - 6), ImVec2(center.x, center.y + 6), color, 1.8f);
    } else {                    // a folder: something already on the disk
        const ImVec2 folder[6] = {ImVec2(center.x - 8, center.y + 6), ImVec2(center.x - 8, center.y - 6), ImVec2(center.x - 3, center.y - 6),
                                  ImVec2(center.x - 1, center.y - 4), ImVec2(center.x + 8, center.y - 4), ImVec2(center.x + 8, center.y + 6)};
        drawList->AddPolyline(folder, 6, color, ImDrawFlags_Closed, 1.5f);
    }
    drawList->AddText(ImGui::GetFont(), 14.0f, ImVec2(at.x + 26, at.y + 9), isCurrent ? kText : IM_COL32(200, 205, 214, 255), label);
    return pressed;
}

/// A button for what is not taken back lightly: quiet until the pointer is on it, then red.
bool dangerButton(const char* label, ImVec2 size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(44, 49, 58, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(150, 52, 48, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(124, 40, 38, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(242, 152, 142, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(18, 10));
    ImGui::PushFont(nullptr, 15.0f);
    bool pressed = ImGui::Button(label, size);
    ImGui::PopFont();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    return pressed;
}

/// "1175" -> "1,175".
std::string withThousands(long long number) {
    std::string digits = std::to_string(number < 0 ? -number : number), out;
    for (size_t i = 0; i < digits.size(); i++) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return number < 0 ? "-" + out : out;
}

/// A small picture of what a file is, about 16 points across, centred at `c`.
void drawFileIcon(ImDrawList* d, ImVec2 c, AssetType type, bool isOpen) {
    auto P = [&](float x, float y) { return ImVec2(c.x + x, c.y + y); };
    const float t = 1.3f;
    switch (type) {
        case AssetType::Folder: {
            const ImU32 back = IM_COL32(96, 118, 152, 255), front = isOpen ? IM_COL32(150, 174, 210, 255) : IM_COL32(124, 148, 186, 255);
            d->AddRectFilled(P(-7, -5.5f), P(-0.5f, -2), back, 1.5f);          // its tab
            d->AddRectFilled(P(-7, -4), P(7, 5.5f), back, 2);
            d->AddRectFilled(P(-7, isOpen ? -0.5f : -2.5f), P(7, 5.5f), front, 2);
            break;
        }
        case AssetType::Scene: {
            // A horizon with a sun over it: a place.
            d->AddRect(P(-7, -5.5f), P(7, 5.5f), kAccent, 2, 0, t);
            d->AddCircleFilled(P(3.2f, -2), 1.7f, kAccent, 10);
            const ImVec2 hill[3] = {P(-5.5f, 4), P(-1.5f, -1), P(2.5f, 4)};
            d->AddConvexPolyFilled(hill, 3, kAccent);
            break;
        }
        case AssetType::Script: {
            ImFont* font = ImGui::GetFont();
            ImVec2 extent = font->CalcTextSizeA(12.5f, FLT_MAX, 0, "{}");
            d->AddText(font, 12.5f, ImVec2(c.x - extent.x * 0.5f, c.y - extent.y * 0.5f - 0.5f), IM_COL32(110, 204, 190, 255), "{}");
            break;
        }
        case AssetType::Mesh: case AssetType::Prefab: {
            const ImU32 color = type == AssetType::Prefab ? IM_COL32(120, 190, 255, 255) : IM_COL32(128, 170, 250, 255);
            const ImVec2 top[4] = {P(0, -6.5f), P(6, -3.2f), P(0, 0), P(-6, -3.2f)};
            if (type == AssetType::Prefab) d->AddConvexPolyFilled(top, 4, IM_COL32(120, 190, 255, 110));
            d->AddPolyline(top, 4, color, ImDrawFlags_Closed, t);
            d->AddLine(P(-6, -3.2f), P(-6, 3.2f), color, t);
            d->AddLine(P(6, -3.2f), P(6, 3.2f), color, t);
            d->AddLine(P(0, 0), P(0, 6.5f), color, t);
            d->AddLine(P(-6, 3.2f), P(0, 6.5f), color, t);
            d->AddLine(P(6, 3.2f), P(0, 6.5f), color, t);
            break;
        }
        case AssetType::Texture: {
            const ImU32 color = IM_COL32(190, 150, 240, 255);
            d->AddRect(P(-6.5f, -6.5f), P(6.5f, 6.5f), color, 2, 0, t);
            d->AddRectFilled(P(-4, -4), P(0, 0), color);
            d->AddRectFilled(P(0, 0), P(4, 4), color);
            break;
        }
        case AssetType::Material: {
            d->AddCircleFilled(c, 6.5f, IM_COL32(226, 178, 84, 255), 18);
            d->AddCircleFilled(P(-2.2f, -2.2f), 1.9f, IM_COL32(255, 240, 205, 220), 10);
            break;
        }
        case AssetType::Audio: {
            const ImU32 color = IM_COL32(120, 210, 150, 255);
            const float heights[5] = {2.5f, 5.5f, 3.5f, 6.5f, 2.0f};
            for (int i = 0; i < 5; i++) d->AddLine(P(-6 + float(i) * 3, -heights[i]), P(-6 + float(i) * 3, heights[i]), color, 1.6f);
            break;
        }
        case AssetType::Graph: {
            const ImU32 color = IM_COL32(200, 205, 214, 255);
            d->AddRect(P(-7, -6), P(-1.5f, -1.5f), color, 1, 0, t);
            d->AddRect(P(1.5f, 1.5f), P(7, 6), color, 1, 0, t);
            d->AddBezierCubic(P(-1.5f, -3.7f), P(1.5f, -3.7f), P(-1.5f, 3.7f), P(1.5f, 3.7f), kAccent, t);
            break;
        }
        case AssetType::Animation: case AssetType::Controller: case AssetType::Timeline: {
            const ImVec2 diamond[4] = {P(0, -6), P(6, 0), P(0, 6), P(-6, 0)};
            d->AddConvexPolyFilled(diamond, 4, IM_COL32(240, 150, 100, 255));
            break;
        }
        default: {
            // A sheet of paper with its corner folded.
            const ImU32 color = IM_COL32(128, 136, 150, 255);
            const ImVec2 sheet[5] = {P(-5, -6.5f), P(1.5f, -6.5f), P(5, -3), P(5, 6.5f), P(-5, 6.5f)};
            d->AddPolyline(sheet, 5, color, ImDrawFlags_Closed, t);
            d->AddLine(P(1.5f, -6.5f), P(1.5f, -3), color, t);
            d->AddLine(P(1.5f, -3), P(5, -3), color, t);
            break;
        }
    }
}

/// A card with a title: what follows is drawn inside it, until endCard.
void beginCard(const char* id, const char* title, ImVec2 size, const std::string& beside = {}) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kCard);
    ImGui::PushStyleColor(ImGuiCol_Border, kCardBorder);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 12));
    ImGui::BeginChild(id, size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 at = ImGui::GetCursorScreenPos();
    ImFont* font = ImGui::GetFont();
    drawList->AddText(font, 15.0f, ImVec2(at.x + 6, at.y), kText, title);
    if (!beside.empty()) {
        ImVec2 extent = font->CalcTextSizeA(12.5f, FLT_MAX, 0, beside.c_str());
        drawList->AddText(font, 12.5f, ImVec2(at.x + ImGui::GetContentRegionAvail().x - extent.x - 6, at.y + 2), kDim, beside.c_str());
    }
    ImGui::Dummy(ImVec2(0, 26));
    // The rows scroll under the title, which stays.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 1));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::BeginChild("##rows", ImVec2(0, 0));
}

void endCard() {
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

/// A row of a card's list: lit under the pointer. Returns true when it is clicked.
bool cardRow(const char* id, float height, bool allowsOverlap = false) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6);
    ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(255, 255, 255, 14));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(255, 255, 255, 22));
    ImGuiSelectableFlags flags = ImGuiSelectableFlags_AllowDoubleClick | (allowsOverlap ? ImGuiSelectableFlags_AllowOverlap : 0);
    bool pressed = ImGui::Selectable(id, false, flags, ImVec2(ImGui::GetContentRegionAvail().x, height));
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    return pressed;
}

}  // namespace

// ---------------------------------------------------------------- the window

ProjectWindow::Result ProjectWindow::draw(const std::vector<std::string>& recentPaths, const std::vector<ProjectTemplate>& templates, const char* version) {
    Result result;
    if (!hasChosenPage_) {
        page_ = recentPaths.empty() ? Page::New : Page::Project;
        hasChosenPage_ = true;
    }
    // Nothing chosen yet, or the project shown was taken off the list: the one opened last.
    // With none left, the form for a new one.
    if (page_ == Page::Project && std::find(recentPaths.begin(), recentPaths.end(), chosenPath_) == recentPaths.end()) {
        if (recentPaths.empty()) page_ = Page::New;
        else choose(recentPaths.front());
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kBackground);
    bool isOpen = ImGui::Begin("##projects", nullptr, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    if (!isOpen) {
        ImGui::End();
        return result;
    }

    drawSidebar(result, recentPaths, version);
    ImGui::SameLine(0, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(40, 34));
    ImGui::BeginChild("##page", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    switch (page_) {
        case Page::Project: drawProject(result); break;
        case Page::New: drawNewProject(result, templates); break;
    }
    drawError();
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::End();
    return result;
}

void ProjectWindow::choose(const std::string& path) {
    chosenPath_ = path;
    page_ = Page::Project;
    error.clear();
}

// ---------------------------------------------------------------- left: the engine, the actions, the projects opened lately

void ProjectWindow::drawSidebar(Result& result, const std::vector<std::string>& recentPaths, const char* version) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float sidebarWidth = std::clamp(viewport->WorkSize.x * 0.25f, 270.0f, 340.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kSidebar);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 24));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0);
    ImGui::BeginChild("##sidebar", ImVec2(sidebarWidth, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    ImVec2 at = ImGui::GetCursorScreenPos();
    std::string under = std::string("Version ") + version;
    ImTextureID logo = picture && !logoPath.empty() ? picture(logoPath) : ImTextureID(0);
    if (logo) {
        // Assets/Branding/logo.png is 2400 x 536, its words starting 1010 across: the version goes under them.
        float width = ImGui::GetContentRegionAvail().x + 12, height = width * 536.0f / 2400.0f;
        drawList->AddImage(logo, ImVec2(at.x - 8, at.y - 6), ImVec2(at.x - 8 + width, at.y - 6 + height));
        drawList->AddText(ImGui::GetFont(), 12.0f, ImVec2(at.x + width * (1010.0f / 2400.0f) - 8, at.y + height - 4), kDim, under.c_str());
        ImGui::Dummy(ImVec2(0, height + 10));
    } else {
        drawMark(drawList, at, 40);
        drawList->AddText(ImGui::GetFont(), 17.0f, ImVec2(at.x + 52, at.y + 1), kText, "Molten Metal Engine");
        drawList->AddText(ImGui::GetFont(), 12.5f, ImVec2(at.x + 52, at.y + 23), kDim, under.c_str());
        ImGui::Dummy(ImVec2(0, 40));
    }
    ImGui::Dummy(ImVec2(0, 14));

    if (navigationRow("##new-project", "New Project", 0, page_ == Page::New)) {
        page_ = Page::New;
        focusesName_ = true;
        error.clear();
    }
    if (navigationRow("##open-other", "Open Other...", 1, false)) result.action = Action::OpenOther;
    ImGui::Dummy(ImVec2(0, 14));

    heading("Recent projects");
    ImGui::Dummy(ImVec2(0, 2));
    if (recentPaths.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Nothing yet. The projects you make and open will be here.");
        ImGui::PopTextWrapPos();
    }
    // Up and down the list with the arrows, while nothing is being typed.
    if (page_ == Page::Project && !recentPaths.empty() && !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        auto found = std::find(recentPaths.begin(), recentPaths.end(), chosenPath_);
        if (found != recentPaths.end()) {
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && found + 1 != recentPaths.end()) choose(*(found + 1));
            else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && found != recentPaths.begin()) choose(*(found - 1));
        }
    }
    const float rowHeight = 50;
    for (const std::string& path : recentPaths) {
        ImGui::PushID(path.c_str());
        std::error_code code;
        bool exists = fs::exists(path, code);
        bool isChosen = page_ == Page::Project && chosenPath_ == path;
        ImVec2 min = ImGui::GetCursorScreenPos();
        // The row reaches a little past the text on both sides, like a list row does.
        ImGui::SetCursorScreenPos(ImVec2(min.x - 10, min.y));
        float rowWidth = ImGui::GetContentRegionAvail().x + 10;
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6);
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(255, 255, 255, 14));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(255, 255, 255, 22));
        if (ImGui::Selectable("##recent", false, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(rowWidth, rowHeight))) {
            choose(path);
            // Twice: opened at once.
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && exists && result.action == Action::None) result = {Action::Open, path, {}};
        }
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", withTilde(path).c_str());
        if (ImGui::BeginPopupContextItem("##recent-menu")) {
            if (ImGui::MenuItem("Open", nullptr, false, exists)) result = {Action::Open, path, {}};
            if (ImGui::MenuItem("Show in Finder", nullptr, false, exists)) result = {Action::Reveal, path, {}};
            ImGui::Separator();
            if (ImGui::MenuItem("Remove from This List")) result = {Action::Forget, path, {}};
            ImGui::EndPopup();
        }
        if (isChosen) {
            drawList->AddRectFilled(ImVec2(min.x - 10, min.y), ImVec2(min.x - 10 + rowWidth, min.y + rowHeight), kCardHover, 6);
            drawList->AddRectFilled(ImVec2(min.x - 10, min.y + 9), ImVec2(min.x - 7, min.y + rowHeight - 9), kAccent, 2);
        }
        // Its picture, small, at the left of the row.
        ImTextureID shot = exists && picture ? picture(path + "/preview.png") : ImTextureID(0);
        float thumbWidth = 0;
        if (shot) {
            thumbWidth = 62;
            drawList->AddImageRounded(shot, ImVec2(min.x, min.y + 9), ImVec2(min.x + 56, min.y + 9 + 31.5f), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 4.0f);
        }
        float textWidth = rowWidth - 24 - thumbWidth;
        std::string projectName = fs::path(path).filename().string();
        std::string where = exists ? fitted(withTilde(fs::path(path).parent_path().string()), textWidth) : std::string("Not found: moved or deleted");
        drawList->AddText(ImGui::GetFont(), 14.0f, ImVec2(min.x + thumbWidth, min.y + 8), exists ? kText : kDim, fitted(projectName, textWidth).c_str());
        drawList->AddText(ImGui::GetFont(), 12.0f, ImVec2(min.x + thumbWidth, min.y + 27), exists ? kDim : IM_COL32(200, 110, 100, 255), where.c_str());
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------- right: the project chosen

void ProjectWindow::readChosenProject() {
    const double now = ImGui::GetTime();
    // Not on screen for a while (a project was open, or the form for a new one was): what is
    // on the disk may have changed since.
    if (now - lastDrawn_ > 1.0) summaryPath_.clear();
    lastDrawn_ = now;

    if (reading_.valid()) {
        if (reading_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        ProjectSummary read = reading_.get();
        if (read.directory == chosenPath_ && summaryPath_ == chosenPath_) summary_ = std::move(read);
    }
    if (summaryPath_ == chosenPath_) return;
    summaryPath_ = chosenPath_;
    readingSince_ = now;
    // Another project: its Assets open, the rest closed. The same one read again keeps what was opened.
    if (summary_.directory != chosenPath_) openFolders_ = {"Assets"};
    if (readsInBackground) {
        reading_ = std::async(std::launch::async, [path = chosenPath_] { return summarizeProject(path); });
    } else {
        summary_ = summarizeProject(chosenPath_);
    }
}

void ProjectWindow::drawProject(Result& result) {
    readChosenProject();
    const float width = std::min(ImGui::GetContentRegionAvail().x, 1080.0f);
    drawProjectHeader(result, width);
    ImGui::Dummy(ImVec2(0, 16));
    drawNumbers(width);
    ImGui::Dummy(ImVec2(0, 16));

    // Its scenes and its files, side by side when there is room, down to the bottom of the window.
    const float gap = 16;
    const float room = ImGui::GetContentRegionAvail().y - (error.empty() ? 0.0f : 76.0f);
    if (width >= 700) {
        const float height = std::max(room, 220.0f);
        const float scenesWidth = std::floor((width - gap) * 0.42f);
        drawScenes(result, ImVec2(scenesWidth, height));
        ImGui::SameLine(0, gap);
        drawFiles(result, ImVec2(width - gap - scenesWidth, height));
    } else {
        drawScenes(result, ImVec2(width, 200));
        ImGui::Dummy(ImVec2(0, gap - 1));
        drawFiles(result, ImVec2(width, std::max(room - 200 - gap, 260.0f)));
    }
    drawDeleteQuestion(result);
}

void ProjectWindow::drawProjectHeader(Result& result, float width) {
    const bool isRead = summary_.directory == chosenPath_;
    const bool canOpen = !isRead || summary_.isValid;
    const std::string title = isRead && summary_.isValid ? summary_.project.name : fs::path(chosenPath_).filename().string();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    // The picture of its scene, the size of a card: enough to tell it from the others.
    const float thumbWidth = 224, thumbHeight = 126;
    const ImVec2 thumbMax(origin.x + thumbWidth, origin.y + thumbHeight);
    ImTextureID shot = picture ? picture(chosenPath_ + "/preview.png") : ImTextureID(0);
    if (shot) drawList->AddImageRounded(shot, origin, thumbMax, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 8.0f);
    else if (emptyPicture) drawList->AddImageRounded(emptyPicture, origin, thumbMax, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 150), 8.0f);
    else drawPicture(drawList, origin, thumbMax, "", 8.0f);
    drawList->AddRect(origin, thumbMax, kCardBorder, 8.0f);

    const float left = origin.x + thumbWidth + 24, textWidth = width - thumbWidth - 24;
    ImGui::SetCursorScreenPos(ImVec2(left, origin.y - 4));
    ImGui::PushFont(nullptr, 27.0f);
    ImGui::TextUnformatted(fitted(title, textWidth).c_str());
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(left, origin.y + 32));
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextUnformatted(fitted(withTilde(chosenPath_), textWidth).c_str());
    ImGui::PopStyleColor();

    // A few facts on one line, a thin rule between them.
    std::vector<std::string> facts;
    if (isRead && summary_.isValid) {
        if (!summary_.project.version.empty()) facts.push_back("Game version " + summary_.project.version);
        if (!summary_.project.identifier.empty()) facts.push_back(summary_.project.identifier);
        if (summary_.secondsAgo >= 0) facts.push_back("Changed " + agoText(summary_.secondsAgo));
    }
    ImVec2 fact(left, origin.y + 54);
    ImFont* font = ImGui::GetFont();
    for (size_t i = 0; i < facts.size(); i++) {
        ImVec2 extent = font->CalcTextSizeA(13.0f, FLT_MAX, 0, facts[i].c_str());
        if (fact.x + extent.x > left + textWidth) break;
        if (i > 0) drawList->AddLine(ImVec2(fact.x - 10, fact.y + 2), ImVec2(fact.x - 10, fact.y + 14), kCardBorder, 1);
        drawList->AddText(font, 13.0f, fact, IM_COL32(170, 177, 190, 255), facts[i].c_str());
        fact.x += extent.x + 20;
    }
    if (isRead && !summary_.isValid) {
        drawList->AddText(font, 13.0f, fact, IM_COL32(240, 176, 96, 255), fitted(summary_.problem, textWidth).c_str());
    }

    // What can be done with it: along the picture's foot, or under it in a narrow window.
    const bool isBeside = textWidth >= 560;
    float bottom = origin.y + thumbHeight;
    if (isBeside) {
        ImGui::SetCursorScreenPos(ImVec2(left, origin.y + thumbHeight - 37));
    } else {
        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + thumbHeight + 14));
        bottom += 14 + 37;
    }
    ImGui::BeginDisabled(!canOpen);
    bool opens = primaryButton("Open Project");
    ImGui::EndDisabled();
    ImGui::SameLine(0, 10);
    if (secondaryButton("Show in Finder")) result = {Action::Reveal, chosenPath_, {}};
    ImGui::SameLine(0, 10);
    if (secondaryButton("Remove from List")) result = {Action::Forget, chosenPath_, {}};
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Takes it off this list. Its folder stays where it is.");
    ImGui::SameLine(0, 10);
    if (dangerButton("Delete...")) asksDelete_ = true;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Moves the project's folder to the Trash.");
    // Return opens it, as a double click on its row does.
    if (canOpen && !ImGui::IsAnyItemActive() && !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
        (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) {
        opens = true;
    }
    if (opens && result.action == Action::None) result = {Action::Open, chosenPath_, {}};

    ImGui::SetCursorScreenPos(ImVec2(origin.x, bottom));
    ImGui::Dummy(ImVec2(width, 0));
}

void ProjectWindow::drawNumbers(float width) {
    const bool isRead = summary_.directory == chosenPath_;
    struct Tile {
        std::string number;
        const char* label;
    };
    std::vector<Tile> tiles;
    if (isRead) {
        const ProjectSummary& project = summary_;
        auto add = [&](int count, const char* one, const char* many, bool always) {
            if (count > 0 || always) tiles.push_back({withThousands(count), count == 1 ? one : many});
        };
        add(int(project.scenes.size()), "Scene", "Scenes", true);
        add(project.scripts, "Script", "Scripts", true);
        add(project.graphs, "Visual script", "Visual scripts", false);
        add(project.prefabs, "Prefab", "Prefabs", false);
        add(project.models, "Model", "Models", false);
        add(project.materials, "Material", "Materials", false);
        add(project.textures, "Texture", "Textures", false);
        add(project.sounds, "Sound", "Sounds", false);
        add(project.animations, "Animation", "Animations", false);
        tiles.push_back({sizeText(project.assetBytes), "Assets on disk"});
    }
    const float gap = 10, height = 60;
    // As many as fit, the weight of it all always last. Each as wide as the next, from the
    // left: a project with little in it has few of them, not wide ones.
    size_t fits = size_t(std::max(1.0f, std::floor((width + gap) / (104.0f + gap))));
    if (tiles.size() > fits) {
        Tile last = tiles.back();
        tiles.resize(fits - 1);
        tiles.push_back(last);
    }
    size_t count = isRead ? tiles.size() : std::min<size_t>(fits, 5);
    const float tileWidth = std::min(std::floor((width - gap * float(count - 1)) / float(count)), 150.0f);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    for (size_t i = 0; i < count; i++) {
        ImVec2 min(origin.x + float(i) * (tileWidth + gap), origin.y), max(min.x + tileWidth, min.y + height);
        drawList->AddRectFilled(min, max, kCard, 9);
        drawList->AddRect(min, max, kCardBorder, 9);
        if (!isRead) continue;
        drawList->AddText(font, 20.0f, ImVec2(min.x + 14, min.y + 9), kText, tiles[i].number.c_str());
        drawList->AddText(font, 12.5f, ImVec2(min.x + 14, min.y + 35), kDim, tiles[i].label);
    }
    ImGui::Dummy(ImVec2(width, height));
}

void ProjectWindow::drawScenes(Result& result, ImVec2 size) {
    const bool isRead = summary_.directory == chosenPath_;
    beginCard("##scenes", "Scenes", size);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    if (!isRead) {
        if (ImGui::GetTime() - readingSince_ > 0.2) {
            ImGui::SetCursorPosX(6);
            ImGui::TextDisabled("Reading the project...");
        }
    } else if (summary_.scenes.empty()) {
        ImGui::SetCursorPosX(6);
        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x - 6);
        ImGui::TextDisabled(summary_.isValid ? "No scene saved yet. Open the project and save its first one." : "No scenes here.");
        ImGui::PopTextWrapPos();
    } else {
        const float rowHeight = 50;
        for (const ProjectSummary::Scene& scene : summary_.scenes) {
            ImGui::PushID(scene.path.c_str());
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const float rowWidth = ImGui::GetContentRegionAvail().x;
            bool opens = cardRow("##scene", rowHeight, true) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Assets/%s", scene.path.c_str());
            if (ImGui::BeginPopupContextItem("##scene-menu")) {
                if (ImGui::MenuItem("Open the Project on This Scene", nullptr, false, summary_.isValid)) opens = true;
                if (ImGui::MenuItem("Show in Finder")) result = {Action::Reveal, chosenPath_ + "/Assets/" + scene.path, {}};
                ImGui::EndPopup();
            }
            const float buttonWidth = 62;
            drawFileIcon(drawList, ImVec2(min.x + 18, min.y + rowHeight * 0.5f), AssetType::Scene, false);
            float textLeft = min.x + 38, textWidth = rowWidth - 38 - buttonWidth - 14;
            ImGui::PushFont(nullptr, 14.5f);
            std::string name = fitted(scene.name, textWidth - (scene.isFirst ? 108.0f : 0.0f));
            ImVec2 nameSize = ImGui::CalcTextSize(name.c_str());
            drawList->AddText(font, 14.5f, ImVec2(textLeft, min.y + 8), kText, name.c_str());
            ImGui::PopFont();
            if (scene.isFirst) {
                // The one the game starts with says so.
                const char* label = "starts the game";
                ImVec2 extent = font->CalcTextSizeA(11.5f, FLT_MAX, 0, label);
                ImVec2 pillMin(textLeft + nameSize.x + 9, min.y + 8), pillMax(pillMin.x + extent.x + 14, pillMin.y + 18);
                drawList->AddRectFilled(pillMin, pillMax, IM_COL32(255, 122, 47, 40), 9);
                drawList->AddText(font, 11.5f, ImVec2(pillMin.x + 7, pillMin.y + 2), IM_COL32(255, 160, 104, 255), label);
            }
            std::string about = countText(scene.entities, "entity", "entities");
            if (scene.entities >= 1000) about = withThousands(scene.entities) + " entities";
            about += ", " + sizeText(scene.bytes);
            if (scene.secondsAgo >= 0) about += ", saved " + agoText(scene.secondsAgo);
            ImGui::PushFont(nullptr, 12.5f);
            drawList->AddText(font, 12.5f, ImVec2(textLeft, min.y + 28), kDim, fitted(about, textWidth).c_str());
            ImGui::PopFont();

            // Its own button, over the row.
            ImGui::SetCursorScreenPos(ImVec2(min.x + rowWidth - buttonWidth - 6, min.y + (rowHeight - 28) * 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(56, 62, 73, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(236, 108, 36, 255));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6);
            ImGui::BeginDisabled(!summary_.isValid);
            if (ImGui::Button("Open", ImVec2(buttonWidth, 28))) opens = true;
            ImGui::EndDisabled();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            ImGui::SetCursorScreenPos(ImVec2(min.x, min.y + rowHeight + 1));
            ImGui::Dummy(ImVec2(0, 0));
            if (opens && summary_.isValid && result.action == Action::None) result = {Action::Open, chosenPath_, scene.path};
            ImGui::PopID();
        }
    }
    endCard();
}

void ProjectWindow::drawFiles(Result& result, ImVec2 size) {
    const bool isRead = summary_.directory == chosenPath_;
    std::string beside;
    if (isRead) beside = withThousands(summary_.files) + (summary_.files == 1 ? " asset" : " assets");
    beginCard("##files", "Files", size, beside);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    if (!isRead) {
        if (ImGui::GetTime() - readingSince_ > 0.2) {
            ImGui::SetCursorPosX(6);
            ImGui::TextDisabled("Reading the project...");
        }
        endCard();
        return;
    }
    const float rowHeight = 25;
    int closedDepth = -1;               // what is deeper than this is inside a folder that is closed
    for (size_t i = 0; i < summary_.entries.size(); i++) {
        const ProjectSummary::Entry& entry = summary_.entries[i];
        if (closedDepth >= 0 && entry.depth > closedDepth) continue;
        closedDepth = -1;
        const bool isOpenFolder = entry.isFolder && openFolders_.count(entry.path) > 0;
        if (entry.isFolder && !isOpenFolder) closedDepth = entry.depth;
        const float rowWidth = ImGui::GetContentRegionAvail().x;
        // Rows out of sight take their room and nothing more.
        if (!ImGui::IsRectVisible(ImVec2(rowWidth, rowHeight))) {
            ImGui::Dummy(ImVec2(rowWidth, rowHeight));
            continue;
        }
        ImGui::PushID(int(i));
        const ImVec2 min = ImGui::GetCursorScreenPos();
        // A folder with something in it opens: what follows it in the list is deeper.
        const bool hasContents = entry.isFolder && i + 1 < summary_.entries.size() && summary_.entries[i + 1].depth > entry.depth;
        if (cardRow("##entry", rowHeight)) {
            if (hasContents) {
                if (isOpenFolder) openFolders_.erase(entry.path);
                else openFolders_.insert(entry.path);
            } else if (entry.type == AssetType::Scene && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && summary_.isValid &&
                       entry.path.compare(0, 7, "Assets/") == 0 && result.action == Action::None) {
                result = {Action::Open, chosenPath_, entry.path.substr(7)};
            }
        }
        if (ImGui::BeginPopupContextItem("##entry-menu")) {
            if (ImGui::MenuItem("Show in Finder")) result = {Action::Reveal, chosenPath_ + "/" + entry.path, {}};
            ImGui::EndPopup();
        }
        const float middle = min.y + rowHeight * 0.5f;
        float x = min.x + 8 + float(entry.depth) * 18;
        if (hasContents) {
            // The arrow: to the right while closed, down while open.
            const ImU32 arrow = IM_COL32(150, 158, 172, 255);
            if (isOpenFolder) drawList->AddTriangleFilled(ImVec2(x - 1, middle - 2.5f), ImVec2(x + 8, middle - 2.5f), ImVec2(x + 3.5f, middle + 3.5f), arrow);
            else drawList->AddTriangleFilled(ImVec2(x + 1, middle - 4.5f), ImVec2(x + 7, middle), ImVec2(x + 1, middle + 4.5f), arrow);
        }
        drawFileIcon(drawList, ImVec2(x + 23, middle), entry.type, isOpenFolder);
        // What it weighs, at the right; its name takes what is left.
        std::string weight = !entry.note.empty() ? entry.note
                           : entry.isFolder ? (entry.files == 0 ? std::string("empty") : countText(entry.files, "file", "files") + ", " + sizeText(entry.bytes))
                           : sizeText(entry.bytes);
        ImVec2 weightSize = font->CalcTextSizeA(12.0f, FLT_MAX, 0, weight.c_str());
        drawList->AddText(font, 12.0f, ImVec2(min.x + rowWidth - weightSize.x - 10, middle - weightSize.y * 0.5f), IM_COL32(118, 126, 140, 255), weight.c_str());
        float nameWidth = min.x + rowWidth - weightSize.x - 24 - (x + 36);
        ImU32 nameColor = entry.isFolder ? kText : IM_COL32(204, 209, 218, 255);
        drawList->AddText(font, 13.5f, ImVec2(x + 36, middle - 8.5f), nameColor, fitted(entry.name, std::max(nameWidth, 40.0f)).c_str());
        ImGui::PopID();
    }
    if (summary_.isCutShort) {
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::SetCursorPosX(8);
        ImGui::TextDisabled("There are more: the list stops at %s.", withThousands(static_cast<long long>(summary_.entries.size())).c_str());
    }
    endCard();
}

/// Before the Trash: what goes, and that it can come back.
void ProjectWindow::drawDeleteQuestion(Result& result) {
    if (asksDelete_) {
        ImGui::OpenPopup("##delete-project");
        asksDelete_ = false;
    }
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460, 0));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, kCard);
    ImGui::PushStyleColor(ImGuiCol_Border, kCardBorder);
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, IM_COL32(8, 9, 12, 130));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 22));
    if (ImGui::BeginPopupModal("##delete-project", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
        const std::string projectName = summary_.directory == chosenPath_ && summary_.isValid ? summary_.project.name : fs::path(chosenPath_).filename().string();
        ImGui::PushFont(nullptr, 18.0f);
        ImGui::PushTextWrapPos(0);
        ImGui::Text("Move \"%s\" to the Trash?", projectName.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(170, 177, 190, 255));
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted("The project's whole folder goes there: its scenes, its assets, its code. Nothing is erased, and it can be put back from the Trash.");
        ImGui::Dummy(ImVec2(0, 2));
        ImGui::TextDisabled("%s", fitted(withTilde(chosenPath_), 412).c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 14));

        // Cancel first: it is what Return and Escape do.
        const float cancelWidth = 100, trashWidth = 150;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - cancelWidth - trashWidth - 10);
        bool cancels = secondaryButton("Cancel", ImVec2(cancelWidth, 0));
        ImGui::SameLine(0, 10);
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(196, 62, 56, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(222, 80, 72, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(170, 50, 46, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(18, 10));
        ImGui::PushFont(nullptr, 15.0f);
        bool trashes = ImGui::Button("Move to Trash", ImVec2(trashWidth, 0));
        ImGui::PopFont();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) cancels = true;
        if (trashes) result = {Action::Delete, chosenPath_, {}};
        if (trashes || cancels) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
}

// ---------------------------------------------------------------- right: a new project

void ProjectWindow::drawNewProject(Result& result, const std::vector<ProjectTemplate>& templates) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    const float contentWidth = std::min(ImGui::GetContentRegionAvail().x, 1080.0f);
    const float gap = 16;

    ImGui::PushFont(nullptr, 27.0f);
    ImGui::TextUnformatted("New Project");
    ImGui::PopFont();
    ImGui::TextDisabled(templates.empty() ? "It starts with an empty scene." : "Start with an empty scene, or with a small game to take apart.");
    ImGui::Dummy(ImVec2(0, 12));

    // What it starts as: a card each, the chosen one outlined.
    if (chosenTemplate >= int(templates.size())) chosenTemplate = -1;
    {
        const int count = 1 + int(templates.size());
        const int perRow = std::clamp(int(std::floor((contentWidth + gap) / (196.0f + gap))), 1, std::min(count, 5));
        const float cardWidth = std::min(std::floor((contentWidth - gap * float(perRow - 1)) / float(perRow)), 250.0f);
        const float pictureHeight = std::floor(cardWidth * 9.0f / 16.0f), cardHeight = pictureHeight + 80;       // a name and three lines under the picture
        for (int index = -1; index < int(templates.size()); index++) {
            const int place = index + 1;
            if (place % perRow != 0) ImGui::SameLine(0, gap);
            ImGui::PushID(index);
            const ImVec2 min = ImGui::GetCursorScreenPos(), max(min.x + cardWidth, min.y + cardHeight);
            if (ImGui::InvisibleButton("##template", ImVec2(cardWidth, cardHeight))) {
                chosenTemplate = index;
                error.clear();
            }
            const bool isHovered = ImGui::IsItemHovered(), isChosen = chosenTemplate == index;
            drawList->AddRectFilled(min, max, isHovered || isChosen ? kCardHover : kCard, 10);
            const ImVec2 pictureMax(max.x, min.y + pictureHeight);
            const std::string directory = index >= 0 ? templates[size_t(index)].directory : std::string();
            ImTextureID shot = index >= 0 ? (picture ? picture(directory + "/preview.png") : ImTextureID(0)) : emptyPicture;
            if (shot) drawList->AddImageRounded(shot, min, pictureMax, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 10.0f, ImDrawFlags_RoundCornersTop);
            else drawPicture(drawList, min, pictureMax, index >= 0 ? fs::path(directory).filename().string() : std::string(), 10.0f);
            const char* title = index >= 0 ? templates[size_t(index)].name.c_str() : "Empty";
            const std::string about = index >= 0 ? templates[size_t(index)].description : std::string("A floor, a light and a camera.");
            drawList->AddText(font, 14.5f, ImVec2(min.x + 12, pictureMax.y + 9), kText, title);
            // Two lines of what it is; the rest is under the cards when it is chosen.
            drawList->PushClipRect(ImVec2(min.x, pictureMax.y + 28), ImVec2(max.x, max.y - 6), true);
            drawList->AddText(font, 12.0f, ImVec2(min.x + 12, pictureMax.y + 29), kDim, about.c_str(), nullptr, cardWidth - 24);
            drawList->PopClipRect();
            drawList->AddRect(min, max, isChosen ? kAccent : kCardBorder, 10, 0, isChosen ? 2.0f : 1.0f);
            ImGui::PopID();
        }
    }
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(170, 177, 190, 255));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::min(contentWidth, 860.0f));
    ImGui::TextUnformatted(chosenTemplate < 0 ? "An empty project: a floor, a light and a camera. Ready for anything." : templates[size_t(chosenTemplate)].description.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    // A template is a project like any other: it can be opened and made better. The engine's own are not
    // changed: the first time, a copy goes into your templates folder, and that copy is the template from then on.
    if (chosenTemplate >= 0) {
        const ProjectTemplate& chosen = templates[size_t(chosenTemplate)];
        ImGui::Dummy(ImVec2(0, 4));
        if (secondaryButton("Edit Template")) result = {Action::EditTemplate, chosen.directory, {}};
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip(chosen.isYours ? "Opens it to change: what you save is what new projects start from"
                                             : "Copies it into your templates folder (~/Documents/Molten Templates) and opens the copy.\n"
                                               "What you save there is what new projects start from; the engine's own stays as it came.");
        }
        ImGui::SameLine(0, 8);
        if (secondaryButton("Show in Finder##template")) result = {Action::Reveal, chosen.directory, {}};
        if (chosen.isYours && !chosen.original.empty()) {
            ImGui::SameLine(0, 8);
            if (secondaryButton("Back to the Original")) result = {Action::ResetTemplate, chosen.directory, {}};
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Your changed copy goes to the Trash, and the engine's own is the template again");
        }
        ImGui::SameLine(0, 14);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", chosen.isYours ? (chosen.original.empty() ? "Yours" : "Yours, changed from the engine's") : "The engine's own");
    }
    ImGui::Dummy(ImVec2(0, 14));

    // ---- its name and where it goes, side by side when there is room
    const float formWidth = std::min(contentWidth, 860.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6);
    const bool isWide = formWidth >= 600;
    const float nameWidth = isWide ? std::floor(formWidth * 0.38f) : formWidth;
    const float chooseWidth = 96;
    const float locationWidth = isWide ? formWidth - nameWidth - gap : formWidth;
    bool enterPressed = false;

    ImVec2 rowStart = ImGui::GetCursorPos();
    ImGui::BeginGroup();
    heading("Name");
    ImGui::SetNextItemWidth(nameWidth);
    if (focusesName_) {
        ImGui::SetKeyboardFocusHere();
        focusesName_ = false;
    }
    if (ImGui::InputText("##name", &name, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) enterPressed = true;
    if (ImGui::IsItemEdited()) error.clear();
    ImGui::EndGroup();
    if (isWide) ImGui::SetCursorPos(ImVec2(rowStart.x + nameWidth + gap, rowStart.y));
    else ImGui::Dummy(ImVec2(0, 6));
    ImGui::BeginGroup();
    heading("Location");
    ImGui::SetNextItemWidth(locationWidth - chooseWidth - 8);
    ImGui::InputText("##location", &location);
    if (ImGui::IsItemEdited()) error.clear();
    ImGui::SameLine(0, 8);
    if (ImGui::Button("Choose...", ImVec2(chooseWidth, 0))) result.action = Action::ChooseLocation;
    ImGui::EndGroup();
    ImGui::PopStyleVar(2);
    ImGui::Dummy(ImVec2(0, 4));

    // ---- where exactly, or why not
    std::string folderName = Project::fileNameFor(name);
    std::string destination = location + "/" + folderName;
    std::error_code code;
    std::string problem;
    if (folderName.empty()) problem = "Give the project a name.";
    else if (location.empty()) problem = "Choose where to put it.";
    else if (!fs::is_directory(location, code)) problem = "There is no folder " + withTilde(location) + ".";
    else if (fs::exists(destination, code) && !fs::is_empty(destination, code)) problem = withTilde(destination) + " already exists. Choose another name or place.";
    if (problem.empty()) {
        ImGui::TextDisabled("It will be the folder %s", fitted(withTilde(destination), formWidth - 150).c_str());
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 176, 96, 255));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + formWidth);
        ImGui::TextUnformatted(problem.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0, 10));

    ImGui::BeginDisabled(!problem.empty());
    bool wantsCreate = primaryButton("Create Project");
    ImGui::EndDisabled();
    if (problem.empty()) {
        ImGui::SameLine(0, 14);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Or press Return.");
    }
    if ((wantsCreate || enterPressed) && problem.empty() && result.action == Action::None) result.action = Action::Create;
}

// ---------------------------------------------------------------- what went wrong the last time something was tried

void ProjectWindow::drawError() {
    if (error.empty()) return;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float contentWidth = std::min(ImGui::GetContentRegionAvail().x, 860.0f);
    ImGui::Dummy(ImVec2(0, 14));
    ImVec2 min = ImGui::GetCursorScreenPos();
    float textWidth = contentWidth - 28 - 34;
    ImVec2 textSize = ImGui::CalcTextSize(error.c_str(), nullptr, false, textWidth);
    ImVec2 max(min.x + contentWidth, min.y + textSize.y + 22);
    drawList->AddRectFilled(min, max, IM_COL32(120, 40, 40, 70), 7);
    drawList->AddRect(min, max, IM_COL32(210, 84, 84, 150), 7);
    drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(min.x + 14, min.y + 11), IM_COL32(255, 176, 170, 255), error.c_str(), nullptr, textWidth);
    ImGui::SetCursorScreenPos(ImVec2(max.x - 32, min.y + (max.y - min.y - 22) * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    bool dismissed = ImGui::Button("x##dismiss", ImVec2(22, 22));
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos(ImVec2(min.x, max.y));
    ImGui::Dummy(ImVec2(0, 0));
    if (dismissed) error.clear();
}

}  // namespace molten::editor
