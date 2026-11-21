// Blend trees in two dimensions: walking in eight directions.
#include "Test.hpp"

#include "Animation/AnimatorController.hpp"

#include <cmath>

using namespace molten;

TEST(a_2d_blend_tree_plays_each_motion_at_its_point_and_mixes_between) {
    std::string error;
    std::optional<AnimatorController> controller = AnimatorController::parse(R"(molten-animator 1
parameter float moveX 0
parameter float moveY 0
state Move blend moveX moveY loops 1
  motion Idle 0 0
  motion Forward 0 1
  motion Back 0 -1
  motion Left -1 0
  motion Right 1 0
start Move
)", &error);
    CHECK(controller.has_value());
    if (!controller) return;
    const AnimatorController::State& move = controller->states[0];
    CHECK(move.blendBy == "moveX" && move.blendByY == "moveY" && move.motions[3].at == -1 && move.motions[1].y == 1);
    // Written and read again, the same.
    std::optional<AnimatorController> again = AnimatorController::parse(controller->toText());
    CHECK(again && again->states[0].blendByY == "moveY" && again->toText() == controller->toText());

    auto weights = [&](float x, float y) { return AnimatorController::blendWeights2D(move.motions, x, y); };
    auto sum = [](const std::vector<float>& w) { float s = 0; for (float v : w) s += v; return s; };
    // At a motion's point: that one alone.
    for (size_t i = 0; i < move.motions.size(); i++) {
        std::vector<float> w = weights(move.motions[i].at, move.motions[i].y);
        CHECK(std::abs(w[i] - 1) < 1e-5f);
    }
    // Half way forward: half standing, half walking forward, nothing sideways.
    std::vector<float> half = weights(0, 0.5f);
    CHECK(std::abs(half[0] - 0.5f) < 1e-4f && std::abs(half[1] - 0.5f) < 1e-4f && half[2] < 1e-5f && half[3] < 1e-5f && half[4] < 1e-5f);
    // Forward and to the right: those two the most, the same, and never back or left.
    std::vector<float> diagonal = weights(0.7f, 0.7f);
    CHECK(std::abs(diagonal[1] - diagonal[4]) < 1e-4f && diagonal[1] > diagonal[0] && diagonal[2] < 1e-5f && diagonal[3] < 1e-5f);
    // Always adding up to one, anywhere, outside too.
    for (float x = -2; x <= 2; x += 0.37f)
        for (float y = -2; y <= 2; y += 0.41f) CHECK(std::abs(sum(weights(x, y)) - 1) < 1e-4f);
    // Far ahead: the forward walk alone.
    CHECK(weights(0, 3)[1] > 0.99f);
    // One motion: always it.
    CHECK(AnimatorController::blendWeights2D({AnimatorController::Motion{"A", 1, 1}}, -5, 4)[0] == 1);
}
