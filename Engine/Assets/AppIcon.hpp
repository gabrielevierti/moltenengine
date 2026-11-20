// AppIcon.hpp
// An application's icon as a phone wants it: a full square with no transparency, which the
// system then rounds itself. Pictures drawn as an icon already (a rounded square in a
// transparent margin, as a Mac icon is) would come out with a light frame: the margin is
// cut away, the rounded corners filled with the color along the square's edge, and the
// rest laid on that color instead of on white.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace molten {

/// `pixels` is RGBA, `width` by `height`. Returns `size` by `size` RGBA, every pixel opaque.
inline std::vector<uint8_t> fullBleedIcon(const std::vector<uint8_t>& pixels, int width, int height, int size) {
    auto alpha = [&](int x, int y) { return pixels[(size_t(y) * size_t(width) + size_t(x)) * 4 + 3]; };
    // What is drawn: the box of the pixels that are not see-through.
    int left = width, top = height, right = -1, bottom = -1;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (alpha(x, y) <= 16) continue;
            left = std::min(left, x), right = std::max(right, x);
            top = std::min(top, y), bottom = std::max(bottom, y);
        }
    }
    if (right < left) left = 0, top = 0, right = width - 1, bottom = height - 1;      // nothing drawn: the whole picture
    // A square around it, centered: a picture that is not square keeps its proportions.
    // A little inside it: the edge an icon is drawn with (a rim, a bevel) is the phone's to draw.
    int side = int(float(std::max(right - left, bottom - top) + 1) * 0.94f);
    float cx = float(left + right + 1) * 0.5f, cy = float(top + bottom + 1) * 0.5f;
    float x0 = cx - float(side) * 0.5f, y0 = cy - float(side) * 0.5f;

    // The background: the color along the middle of each side of the drawn box, where a
    // rounded square is flat, averaged over what is opaque there.
    float sum[3] = {0, 0, 0}, count = 0;
    auto sample = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        const uint8_t* p = &pixels[(size_t(y) * size_t(width) + size_t(x)) * 4];
        if (p[3] < 250) return;
        for (int c = 0; c < 3; c++) sum[c] += float(p[c]);
        count += 1;
    };
    int inset = std::max(2, (right - left) / 80);
    for (int k = -side / 8; k <= side / 8; k++) {
        int mx = (left + right) / 2 + k, my = (top + bottom) / 2 + k;
        sample(mx, top + inset), sample(mx, bottom - inset), sample(left + inset, my), sample(right - inset, my);
    }
    float back[3] = {0, 0, 0};
    if (count > 0) for (int c = 0; c < 3; c++) back[c] = sum[c] / count;

    // Each pixel of the icon: the picture there (bilinear), laid over the background.
    std::vector<uint8_t> out(size_t(size) * size_t(size) * 4);
    auto at = [&](int x, int y, int c) -> float {
        x = std::clamp(x, 0, width - 1), y = std::clamp(y, 0, height - 1);
        return float(pixels[(size_t(y) * size_t(width) + size_t(x)) * 4 + size_t(c)]);
    };
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float sx = x0 + (float(x) + 0.5f) * float(side) / float(size) - 0.5f;
            float sy = y0 + (float(y) + 0.5f) * float(side) / float(size) - 0.5f;
            int ix = int(std::floor(sx)), iy = int(std::floor(sy));
            float fx = sx - float(ix), fy = sy - float(iy);
            float color[4];
            for (int c = 0; c < 4; c++) {
                // Premultiplied, so that the see-through edge does not bleed its own (black) color.
                auto premultiplied = [&](int px, int py) { return c == 3 ? at(px, py, 3) : at(px, py, c) * at(px, py, 3) / 255.0f; };
                float top0 = premultiplied(ix, iy) * (1 - fx) + premultiplied(ix + 1, iy) * fx;
                float bottom0 = premultiplied(ix, iy + 1) * (1 - fx) + premultiplied(ix + 1, iy + 1) * fx;
                color[c] = top0 * (1 - fy) + bottom0 * fy;
            }
            float a = color[3] / 255.0f;
            uint8_t* o = &out[(size_t(y) * size_t(size) + size_t(x)) * 4];
            for (int c = 0; c < 3; c++) o[c] = uint8_t(std::clamp(color[c] + back[c] * (1 - a) + 0.5f, 0.0f, 255.0f));
            o[3] = 255;
        }
    }
    return out;
}

}  // namespace molten
