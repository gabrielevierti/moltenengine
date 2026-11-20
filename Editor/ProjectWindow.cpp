#include "ProjectWindow.hpp"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>

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

    if (key == "RollABall") {
        background(IM_COL32(52, 84, 132, 255));
        drawList->AddRectFilled(p(0, 0.3f), p(1, 0.52f), IM_COL32(86, 122, 170, 255));                 // the horizon's haze
        drawList->AddRectFilled(p(0, 0.5f), p(1, 1), IM_COL32(70, 78, 92, 255));                       // the floor
        drawList->AddRectFilled(p(0, 0.5f), p(1, 0.535f), IM_COL32(182, 188, 198, 255));               // the far wall
        const float coins[4][3] = {{0.66f, 0.63f, 0.045f}, {0.83f, 0.74f, 0.06f}, {0.56f, 0.86f, 0.08f}, {0.2f, 0.6f, 0.04f}};
        for (const auto& coin : coins) {
            ImVec2 center = p(coin[0], coin[1]);
            float radius = height * coin[2];
            drawList->AddEllipseFilled(ImVec2(center.x, center.y + radius * 1.35f), ImVec2(radius * 0.8f, radius * 0.22f), IM_COL32(0, 0, 0, 70));
            drawList->AddEllipseFilled(center, ImVec2(radius * 0.62f, radius), IM_COL32(255, 200, 60, 255));
            drawList->AddEllipseFilled(ImVec2(center.x - radius * 0.1f, center.y), ImVec2(radius * 0.34f, radius * 0.7f), IM_COL32(255, 232, 140, 255));
        }
        ImVec2 ball = p(0.36f, 0.7f);
        float radius = height * 0.19f;
        drawList->AddEllipseFilled(ImVec2(ball.x + radius * 0.1f, ball.y + radius * 0.98f), ImVec2(radius * 1.05f, radius * 0.26f), IM_COL32(0, 0, 0, 90));
        drawList->AddCircleFilled(ball, radius, IM_COL32(236, 84, 36, 255), 48);
        drawList->AddCircleFilled(ImVec2(ball.x - radius * 0.28f, ball.y - radius * 0.3f), radius * 0.5f, IM_COL32(255, 132, 78, 255), 32);
        drawList->AddCircleFilled(ImVec2(ball.x - radius * 0.38f, ball.y - radius * 0.42f), radius * 0.16f, IM_COL32(255, 222, 200, 255), 16);
    } else if (key == "SpaceShooter") {
        background(IM_COL32(10, 12, 26, 255));
        unsigned seed = 7;
        for (int i = 0; i < 46; i++) {
            seed = seed * 1664525u + 1013904223u;
            float x = float((seed >> 8) & 1023) / 1023.0f;
            seed = seed * 1664525u + 1013904223u;
            float y = float((seed >> 8) & 1023) / 1023.0f;
            drawList->AddCircleFilled(p(x, y), i % 7 == 0 ? 1.6f : 1.0f, IM_COL32(200, 215, 255, i % 3 == 0 ? 210 : 110), 6);
        }
        const float enemies[3][2] = {{0.2f, 0.2f}, {0.74f, 0.15f}, {0.86f, 0.42f}};
        for (const auto& enemy : enemies) {
            ImVec2 center = p(enemy[0], enemy[1]);
            float half = height * 0.075f;
            drawList->AddRectFilled(ImVec2(center.x - half * 1.5f, center.y - half * 1.5f), ImVec2(center.x + half * 1.5f, center.y + half * 1.5f), IM_COL32(230, 50, 60, 40), half);
            drawList->AddRectFilled(ImVec2(center.x - half, center.y - half), ImVec2(center.x + half, center.y + half), IM_COL32(226, 54, 62, 255), half * 0.3f);
            drawList->AddRectFilled(ImVec2(center.x - half * 0.45f, center.y - half * 0.45f), ImVec2(center.x + half * 0.45f, center.y + half * 0.45f), IM_COL32(255, 140, 130, 255), 2);
        }
        // An enemy that has just been hit.
        ImVec2 blast = p(0.5f, 0.3f);
        drawList->AddCircleFilled(blast, height * 0.17f, IM_COL32(255, 120, 40, 50), 32);
        drawList->AddCircleFilled(blast, height * 0.11f, IM_COL32(255, 150, 50, 220), 32);
        drawList->AddCircleFilled(blast, height * 0.06f, IM_COL32(255, 235, 170, 255), 24);
        for (int i = 0; i < 8; i++) {
            float angle = float(i) * 0.785f + 0.3f;
            ImVec2 direction(std::cos(angle), std::sin(angle));
            drawList->AddLine(ImVec2(blast.x + direction.x * height * 0.14f, blast.y + direction.y * height * 0.14f),
                              ImVec2(blast.x + direction.x * height * 0.2f, blast.y + direction.y * height * 0.2f), IM_COL32(255, 190, 90, 220), 2);
        }
        ImVec2 ship = p(0.5f, 0.8f);
        float size = height * 0.12f;
        for (float y : {0.6f, 0.5f}) drawList->AddLine(p(0.5f, y), p(0.5f, y - 0.06f), IM_COL32(120, 220, 255, 255), 3);
        drawList->AddTriangleFilled(ImVec2(ship.x - size * 0.3f, ship.y + size * 0.7f), ImVec2(ship.x + size * 0.3f, ship.y + size * 0.7f),
                                    ImVec2(ship.x, ship.y + size * 1.5f), IM_COL32(90, 170, 255, 200));
        drawList->AddTriangleFilled(ImVec2(ship.x, ship.y - size), ImVec2(ship.x - size, ship.y + size * 0.8f), ImVec2(ship.x + size, ship.y + size * 0.8f),
                                    IM_COL32(205, 214, 232, 255));
        drawList->AddTriangleFilled(ImVec2(ship.x, ship.y - size * 0.45f), ImVec2(ship.x - size * 0.3f, ship.y + size * 0.4f),
                                    ImVec2(ship.x + size * 0.3f, ship.y + size * 0.4f), IM_COL32(70, 120, 210, 255));
    } else if (key == "BrickBreaker") {
        background(IM_COL32(13, 15, 30, 255));
        const ImU32 rows[5] = {IM_COL32(240, 72, 72, 255), IM_COL32(246, 150, 50, 255), IM_COL32(238, 220, 70, 255), IM_COL32(74, 208, 110, 255),
                               IM_COL32(72, 150, 250, 255)};
        const float left = 0.08f, right = 0.92f, top = 0.1f, rowHeight = 0.085f, gap = 0.012f;
        const int columns = 8;
        float brickWidth = (right - left - gap * float(columns - 1)) / float(columns);
        for (int row = 0; row < 5; row++) {
            for (int column = 0; column < columns; column++) {
                if ((row == 4 && (column == 3 || column == 4)) || (row == 3 && column == 4)) continue;      // already broken
                float x = left + float(column) * (brickWidth + gap), y = top + float(row) * (rowHeight + gap * 1.6f);
                drawList->AddRectFilled(p(x, y), p(x + brickWidth, y + rowHeight), rows[row], 2.5f);
            }
        }
        ImVec2 ball = p(0.57f, 0.68f);
        float radius = height * 0.035f;
        drawList->AddLine(p(0.47f, 0.84f), ball, IM_COL32(255, 255, 255, 40), radius * 1.2f);
        drawList->AddCircleFilled(ball, radius * 2.6f, IM_COL32(255, 240, 200, 36), 24);
        drawList->AddCircleFilled(ball, radius, IM_COL32(255, 250, 235, 255), 24);
        drawList->AddRectFilled(p(0.36f, 0.88f), p(0.6f, 0.925f), IM_COL32(232, 236, 244, 255), height * 0.02f);
    } else if (key == "Driving") {
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
    } else if (key == "Flying") {
        background(IM_COL32(84, 138, 204, 255));
        drawList->AddRectFilledMultiColor(min, p(1, 0.75f), IM_COL32(60, 110, 190, 255), IM_COL32(60, 110, 190, 255), IM_COL32(150, 190, 226, 255), IM_COL32(150, 190, 226, 255));
        drawList->AddRectFilled(p(0, 0.78f), p(1, 1), IM_COL32(58, 104, 50, 255));
        const ImVec2 strip[4] = {p(0.42f, 0.78f), p(0.5f, 0.78f), p(0.86f, 1), p(0.5f, 1)};
        drawList->AddConvexPolyFilled(strip, 4, IM_COL32(48, 50, 56, 255));
        const float clouds[3][3] = {{0.2f, 0.22f, 0.07f}, {0.78f, 0.34f, 0.05f}, {0.62f, 0.14f, 0.04f}};
        for (const auto& cloud : clouds) {
            for (int i = -1; i <= 1; i++) drawList->AddCircleFilled(p(cloud[0] + float(i) * cloud[2] * 0.9f, cloud[1] + (i == 0 ? -0.02f : 0.0f)), height * cloud[2] * (i == 0 ? 1.2f : 0.9f), IM_COL32(255, 255, 255, 200), 20);
        }
        // The aeroplane, climbing away, seen from behind and a little to one side.
        const float x = 0.42f, y = 0.46f;
        const ImVec2 wing[4] = {p(x - 0.3f, y + 0.07f), p(x + 0.3f, y - 0.09f), p(x + 0.3f, y - 0.05f), p(x - 0.3f, y + 0.11f)};
        drawList->AddConvexPolyFilled(wing, 4, IM_COL32(236, 236, 232, 255));
        drawList->AddEllipseFilled(p(x, y), ImVec2(width * 0.035f, height * 0.085f), IM_COL32(222, 222, 218, 255));
        const ImVec2 tail[4] = {p(x - 0.1f, y + 0.115f), p(x + 0.1f, y + 0.065f), p(x + 0.1f, y + 0.09f), p(x - 0.1f, y + 0.14f)};
        drawList->AddConvexPolyFilled(tail, 4, IM_COL32(236, 236, 232, 255));
        const ImVec2 fin[3] = {p(x - 0.008f, y + 0.1f), p(x + 0.012f, y - 0.06f), p(x + 0.03f, y + 0.1f)};
        drawList->AddConvexPolyFilled(fin, 3, IM_COL32(206, 40, 32, 255));
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

/// A small heading over a group of things: "RECENT", "NAME".
void heading(const char* text) {
    ImGui::PushFont(nullptr, 11.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 128, 142, 255));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

}  // namespace

ProjectWindow::Result ProjectWindow::draw(const std::vector<std::string>& recentPaths, const std::vector<ProjectTemplate>& templates, const char* version) {
    Result result;
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    bool isOpen = ImGui::Begin("##projects", nullptr, flags);
    ImGui::PopStyleVar(3);
    if (!isOpen) {
        ImGui::End();
        return result;
    }
    const float sidebarWidth = std::clamp(viewport->WorkSize.x * 0.26f, 260.0f, 340.0f);

    // ---------------------------------------------------------------- left: the engine, and the projects opened lately
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kSidebar);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22, 26));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0);
    ImGui::BeginChild("##sidebar", ImVec2(sidebarWidth, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 at = ImGui::GetCursorScreenPos();
        std::string under = std::string("Version ") + version;
        ImTextureID logo = picture && !logoPath.empty() ? picture(logoPath) : ImTextureID(0);
        if (logo) {
            // The logo across the sidebar (it is 1200 x 268), and the version under its name.
            float width = ImGui::GetContentRegionAvail().x + 12, height = width * 268.0f / 1200.0f;
            drawList->AddImage(logo, ImVec2(at.x - 8, at.y - 6), ImVec2(at.x - 8 + width, at.y - 6 + height));
            drawList->AddText(ImGui::GetFont(), 12.0f, ImVec2(at.x + width * 0.407f - 8, at.y + height - 4), kDim, under.c_str());
            ImGui::Dummy(ImVec2(0, height + 10));
        } else {
            drawMark(drawList, at, 40);
            drawList->AddText(ImGui::GetFont(), 17.0f, ImVec2(at.x + 52, at.y + 1), kText, "Molten Engine");
            drawList->AddText(ImGui::GetFont(), 12.5f, ImVec2(at.x + 52, at.y + 23), kDim, under.c_str());
            ImGui::Dummy(ImVec2(0, 40));
        }
        ImGui::Dummy(ImVec2(0, 18));

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 8));
        if (ImGui::Button("Open a Project...", ImVec2(-FLT_MIN, 0))) result.action = Action::OpenOther;
        ImGui::PopStyleVar();
        ImGui::Dummy(ImVec2(0, 20));

        heading("RECENT PROJECTS");
        ImGui::Dummy(ImVec2(0, 2));
        if (recentPaths.empty()) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextDisabled("Nothing yet. The projects you make and open will be here.");
            ImGui::PopTextWrapPos();
        }
        const float rowHeight = 46;
        for (const std::string& path : recentPaths) {
            ImGui::PushID(path.c_str());
            std::error_code code;
            bool exists = fs::exists(path, code);
            ImVec2 min = ImGui::GetCursorScreenPos();
            // The row reaches a little past the text on both sides, like a list row does.
            ImGui::SetCursorScreenPos(ImVec2(min.x - 10, min.y));
            float rowWidth = ImGui::GetContentRegionAvail().x + 10;
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6);
            ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(0, 0, 0, 0));
            if (ImGui::Selectable("##recent", false, ImGuiSelectableFlags_None, ImVec2(rowWidth, rowHeight)) && result.action == Action::None) {
                result.action = Action::Open;
                result.path = path;
            }
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            if (ImGui::BeginPopupContextItem("##recent-menu")) {
                if (ImGui::MenuItem("Open", nullptr, false, exists)) result = {Action::Open, path};
                if (ImGui::MenuItem("Reveal in Finder", nullptr, false, exists)) result = {Action::Reveal, path};
                ImGui::Separator();
                if (ImGui::MenuItem("Remove from This List")) result = {Action::Forget, path};
                ImGui::EndPopup();
            }
            // Its picture, small, at the left of the row.
            ImTextureID shot = exists && picture ? picture(path + "/preview.png") : ImTextureID(0);
            float thumbWidth = 0;
            if (shot) {
                thumbWidth = 60;
                drawList->AddImageRounded(shot, ImVec2(min.x, min.y + 6), ImVec2(min.x + 56, min.y + 6 + 31.5f), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 4.0f);
            }
            float textWidth = rowWidth - 24 - thumbWidth;
            std::string name = fs::path(path).filename().string();
            std::string where = exists ? fitted(withTilde(fs::path(path).parent_path().string()), textWidth) : std::string("Not found: moved or deleted");
            drawList->AddText(ImGui::GetFont(), 14.0f, ImVec2(min.x + thumbWidth, min.y + 6), exists ? kText : kDim, fitted(name, textWidth).c_str());
            drawList->AddText(ImGui::GetFont(), 12.0f, ImVec2(min.x + thumbWidth, min.y + 25), exists ? kDim : IM_COL32(200, 110, 100, 255), where.c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // ---------------------------------------------------------------- right: a new project
    ImGui::SameLine(0, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(44, 34));
    ImGui::BeginChild("##new", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float contentWidth = std::min(ImGui::GetContentRegionAvail().x, 1080.0f);

        ImGui::PushFont(nullptr, 26.0f);
        ImGui::TextUnformatted("New Project");
        ImGui::PopFont();
        ImGui::TextDisabled("Start with an empty scene, or with a small game you can play and take apart.");
        ImGui::Dummy(ImVec2(0, 16));

        // ---- what to start from: a card each
        struct Card {
            int index;
            std::string key, title, description;
        };
        std::vector<Card> cards;
        cards.push_back({-1, "", "Empty", "A floor, a light and a camera. Ready for anything."});
        for (int i = 0; i < int(templates.size()); i++) {
            const ProjectTemplate& item = templates[size_t(i)];
            cards.push_back({i, fs::path(item.directory).filename().string(), item.name, item.description});
        }
        if (chosenTemplate >= int(templates.size())) chosenTemplate = -1;

        const float gap = 16;
        int columns = std::clamp(int((contentWidth + gap) / (220.0f + gap)), 1, 4);
        columns = std::min(columns, int(cards.size()));
        // A card is a card: with one or two of them it does not grow to fill the window.
        const float cardWidth = std::min(std::floor((contentWidth - gap * float(columns - 1)) / float(columns)), 300.0f);
        const float pictureHeight = std::floor(cardWidth * 0.56f);
        const float lineHeight = 13.0f * 1.35f;
        const float cardHeight = pictureHeight + 12 + 20 + lineHeight * 3 + 12;
        bool wantsCreate = false;

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
        for (size_t i = 0; i < cards.size(); i++) {
            const Card& card = cards[i];
            if (i % size_t(columns) != 0) ImGui::SameLine();
            ImGui::PushID(card.index);
            bool isChosen = chosenTemplate == card.index;
            if (ImGui::InvisibleButton("##card", ImVec2(cardWidth, cardHeight))) chosenTemplate = card.index;
            bool isHovered = ImGui::IsItemHovered();
            if (isHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) wantsCreate = true;
            if (isHovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
            const float rounding = 9;
            drawList->AddRectFilled(min, max, isHovered || isChosen ? kCardHover : kCard, rounding);
            // The scene itself, if the project has a picture of it; a drawing of the idea otherwise.
            ImTextureID shot = card.index >= 0 && picture ? picture(templates[size_t(card.index)].directory + "/preview.png") : emptyPicture;
            if (shot) {
                // The middle of the picture, cut to the card's proportions (pictures are 16 by 9).
                float wanted = cardWidth / pictureHeight, has = 16.0f / 9.0f;
                float cut = wanted < has ? (1.0f - wanted / has) * 0.5f : 0.0f;
                drawList->AddImageRounded(shot, min, ImVec2(max.x, min.y + pictureHeight), ImVec2(cut, 0), ImVec2(1.0f - cut, 1), IM_COL32_WHITE, rounding,
                                          ImDrawFlags_RoundCornersTop);
            } else {
                drawPicture(drawList, min, ImVec2(max.x, min.y + pictureHeight), card.key, rounding);
            }
            drawList->AddText(ImGui::GetFont(), 15.0f, ImVec2(min.x + 14, min.y + pictureHeight + 11), kText, card.title.c_str());
            drawList->PushClipRect(ImVec2(min.x, min.y + pictureHeight), ImVec2(max.x, max.y - 8), true);
            drawList->AddText(ImGui::GetFont(), 13.0f, ImVec2(min.x + 14, min.y + pictureHeight + 33), kDim, card.description.c_str(), nullptr, cardWidth - 28);
            drawList->PopClipRect();
            if (isChosen) {
                drawList->AddRect(min, max, kAccent, rounding, ImDrawFlags_None, 2.0f);
                // A check mark in the corner says it again, without relying on color alone.
                ImVec2 badge(max.x - 20, min.y + 20);
                drawList->AddCircleFilled(badge, 11, kAccent, 24);
                const ImVec2 check[3] = {ImVec2(badge.x - 5, badge.y), ImVec2(badge.x - 1.5f, badge.y + 3.5f), ImVec2(badge.x + 5, badge.y - 3.5f)};
                drawList->AddPolyline(check, 3, IM_COL32(28, 22, 18, 255), ImDrawFlags_None, 2.2f);
            } else {
                drawList->AddRect(min, max, isHovered ? IM_COL32(92, 100, 116, 255) : kCardBorder, rounding, ImDrawFlags_None, 1.0f);
            }
            ImGui::PopID();
        }
        ImGui::PopStyleVar();
        ImGui::Dummy(ImVec2(0, 10));

        if (templates.empty()) {
            // The engine comes with the empty project only: the example games are their own download.
            ImGui::PushStyleColor(ImGuiCol_Text, kDim);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth);
            ImGui::TextUnformatted("The example games (Roll a Ball, Space Shooter, Brick Breaker, Driving, Flying) are a separate download, "
                                   "MoltenProjects.zip. Unzip it where you keep your projects and open one with \"Open a Project...\".");
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, 8));
        }
        // ---- its name and where it goes, side by side when there is room
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 8));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6);
        const bool isWide = contentWidth >= 640;
        const float nameWidth = isWide ? std::floor(contentWidth * 0.36f) : contentWidth;
        const float chooseWidth = 96;
        const float locationWidth = isWide ? contentWidth - nameWidth - gap : contentWidth;
        bool enterPressed = false;

        ImVec2 rowStart = ImGui::GetCursorPos();
        ImGui::BeginGroup();
        heading("NAME");
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
        heading("LOCATION");
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
            ImGui::TextDisabled("It will be the folder %s", fitted(withTilde(destination), contentWidth - 150).c_str());
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 176, 96, 255));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth);
            ImGui::TextUnformatted(problem.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        ImGui::Dummy(ImVec2(0, 10));

        // ---- the one thing this window is for
        ImGui::BeginDisabled(!problem.empty());
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(236, 108, 36, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 132, 62, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(212, 92, 26, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(26, 20, 16, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(22, 10));
        ImGui::PushFont(nullptr, 15.0f);
        if (ImGui::Button("Create Project")) wantsCreate = true;
        ImGui::PopFont();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);
        ImGui::EndDisabled();
        if (problem.empty()) {
            ImGui::SameLine(0, 14);
            ImGui::AlignTextToFramePadding();
            const char* from = chosenTemplate < 0 ? "an empty project" : templates[size_t(chosenTemplate)].name.c_str();
            ImGui::TextDisabled(chosenTemplate < 0 ? "Makes %s. Press Return." : "Starts from %s. Press Return.", from);
        }
        if ((wantsCreate || enterPressed) && problem.empty() && result.action == Action::None) result.action = Action::Create;

        // ---- what went wrong the last time something was tried
        if (!error.empty()) {
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
        ImGui::Dummy(ImVec2(0, 20));
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::End();
    return result;
}

}  // namespace molten::editor
