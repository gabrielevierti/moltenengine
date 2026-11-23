#include "UI/Splash.hpp"

#include <algorithm>
#include <cmath>

namespace molten {

void Splash::draw(Canvas& canvas, float seconds, std::string_view subtitle, const Texture* logo) {
    if (isOver(seconds)) return;
    Vec2 size = canvas.size();
    if (size.x <= 0 || size.y <= 0) return;

    // Everything on it arrives over the first third of a second; the whole screen, the
    // dark background included, leaves over the last half.
    float arrive = easeOutCubic(saturate(seconds / 0.35f));
    float leave = saturate((kDuration - seconds) / kFade);
    float alpha = arrive * leave;
    canvas.rect(Vec2{0, 0}, size, rgb(0x000000, leave));

    float titleSize = std::clamp(std::min(size.x, size.y) * 0.034f, 16.0f, 34.0f);
    float rise = (1.0f - arrive) * 10.0f;
    float under = size.y * 0.5f;
    if (logo) {
        // The logo: the mark, the name and what the engine is for, as one picture.
        float width = std::min(size.x * 0.62f, 760.0f), height = width * 536.0f / 2400.0f;
        canvas.image(logo, Vec2{(size.x - width) * 0.5f, (size.y - height) * 0.5f + rise}, Vec2{width, height}, Vec4{1, 1, 1, alpha});
        under = (size.y + height) * 0.5f + titleSize * 0.6f + rise;
    } else {
        canvas.text("MOLTEN ENGINE", Vec2{size.x * 0.5f, under - titleSize + rise}, titleSize, rgb(0xf2f4f8, alpha), TextAlign::Center);
        under += titleSize * 0.8f + rise;
    }
    // The program's own line: the name of the game.
    if (!subtitle.empty()) canvas.text(subtitle, Vec2{size.x * 0.5f, under}, titleSize * 0.5f, rgb(0x8b93a1, alpha), TextAlign::Center);
}

}  // namespace molten
