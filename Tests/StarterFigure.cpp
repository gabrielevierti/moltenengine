// StarterFigure.cpp
// See StarterFigure.hpp.
#include "StarterFigure.hpp"

#include "Assets/Gltf.hpp"
#include "Test.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

namespace starter {

namespace {

// ---------------------------------------------------------------- the bones

enum Bone {
    Hips, Spine, Chest, Neck, Head,
    LeftArm, LeftForeArm, LeftHand, RightArm, RightForeArm, RightHand,
    LeftUpLeg, LeftLeg, LeftFoot, RightUpLeg, RightLeg, RightFoot,
    kBones
};

const char* const kNames[kBones] = {"Hips", "Spine", "Chest", "Neck", "Head", "LeftArm", "LeftForeArm", "LeftHand", "RightArm",
                                    "RightForeArm", "RightHand", "LeftUpLeg", "LeftLeg", "LeftFoot", "RightUpLeg", "RightLeg", "RightFoot"};
const int kParents[kBones] = {-1, Hips, Spine, Chest, Neck, Chest, LeftArm, LeftForeArm, Chest, RightArm, RightForeArm, Hips, LeftUpLeg, LeftLeg, Hips, RightUpLeg, RightLeg};

const float kHipsHeight = 0.95f;        // of the Hips joint, standing
const float kAnkle = 0.085f;            // of the ankle above the sole
const float kHeelBack = 0.06f;          // the heel's edge behind the ankle
const float kBallAhead = 0.185f;        // where it stands when the heel is up, ahead of the ankle: the toes of a boot that does not bend
const float kLegsApart = 0.09f;         // each hip joint from the middle

/// Where each joint is, standing at rest: the figure faces +z and its left is +x. Every bone
/// hangs straight from its parent, so that at rest no bone is turned.
Vec3 jointOf(int bone) {
    switch (bone) {
        case Hips: return Vec3{0, kHipsHeight, 0};
        case Spine: return Vec3{0, 1.07f, 0};
        case Chest: return Vec3{0, 1.23f, 0};
        case Neck: return Vec3{0, 1.49f, 0};
        case Head: return Vec3{0, 1.57f, 0};
        case LeftArm: return Vec3{0.215f, 1.43f, 0};
        case LeftForeArm: return Vec3{0.215f, 1.15f, 0};
        case LeftHand: return Vec3{0.215f, 0.89f, 0};
        case LeftUpLeg: return Vec3{kLegsApart, 0.90f, 0};
        case LeftLeg: return Vec3{kLegsApart, 0.49f, 0};
        case LeftFoot: return Vec3{kLegsApart, kAnkle, 0};
        default: break;
    }
    // The right side is the left's, mirrored.
    Vec3 left = jointOf(bone - 3);
    return Vec3{-left.x, left.y, left.z};
}

const float kThigh = 0.41f, kShin = 0.405f;

Quat aboutX(float degrees) { return Quat(radians(degrees), Vec3{1, 0, 0}); }
Quat aboutY(float degrees) { return Quat(radians(degrees), Vec3{0, 1, 0}); }
Quat aboutZ(float degrees) { return Quat(radians(degrees), Vec3{0, 0, 1}); }
float smooth(float t) {
    t = saturate(t);
    return t * t * (3 - 2 * t);
}
float wave(float turns) { return std::cos(turns * 2 * kPi); }
float cycle(float phase) { return phase - std::floor(phase); }

// ---------------------------------------------------------------- the faces

// The colors, as squares of the palette picture (four across).
enum Color { Suit, Joint, Accent, Visor, Boot, Helmet, Stripe, Sole, kColors };
const Vec3 kPalette[kColors] = {{0.90f, 0.90f, 0.88f}, {0.17f, 0.18f, 0.21f}, {0.96f, 0.45f, 0.12f}, {0.04f, 0.06f, 0.09f},
                                {0.24f, 0.26f, 0.30f}, {0.97f, 0.97f, 0.96f}, {0.20f, 0.55f, 0.86f}, {0.10f, 0.10f, 0.11f}};

struct Faces {
    struct Corner {
        Vec3 at;
        int bone;
    };
    struct Triangle {
        Corner a, b, c;
        int color;
    };
    std::vector<Triangle> triangles;

    /// A flat face of any number of corners, turned to look away from `inside`.
    void face(std::vector<Corner> corners, int color, Vec3 inside) {
        if (corners.size() < 3) return;
        Vec3 middle{0, 0, 0};
        for (const Corner& corner : corners) middle += corner.at;
        middle /= float(corners.size());
        Vec3 normal = simd::cross(corners[1].at - corners[0].at, corners[2].at - corners[0].at);
        if (simd::dot(normal, middle - inside) < 0) std::reverse(corners.begin(), corners.end());
        for (size_t i = 1; i + 1 < corners.size(); i++) triangles.push_back(Triangle{corners[0], corners[i], corners[i + 1], color});
    }

    /// One level of something that stands: an ellipse of corners about a center.
    struct Ring {
        Vec3 center;
        float wide, deep;               // half its size along x and along z
        int bone;
    };
    /// A skin over rings, one above the other, with both ends closed. `colorOf` says the color
    /// of the band above ring `band`, on its side `side` (side 0 looks toward +z).
    void loft(const std::vector<Ring>& rings, int sides, const std::function<int(int band, int side)>& colorOf) {
        auto corner = [&](const Ring& ring, int k) {
            float angle = 2 * kPi * (float(k) - 0.5f) / float(sides);
            return Corner{ring.center + Vec3{ring.wide * std::sin(angle), 0, ring.deep * std::cos(angle)}, ring.bone};
        };
        for (size_t r = 0; r + 1 < rings.size(); r++) {
            Vec3 inside = (rings[r].center + rings[r + 1].center) * 0.5f;
            for (int k = 0; k < sides; k++) {
                face({corner(rings[r], k), corner(rings[r], k + 1), corner(rings[r + 1], k + 1), corner(rings[r + 1], k)}, colorOf(int(r), k), inside);
            }
        }
        auto cap = [&](const Ring& ring, const Ring& toward, int color) {
            std::vector<Corner> corners;
            for (int k = 0; k < sides; k++) corners.push_back(corner(ring, k));
            face(corners, color, toward.center);
        };
        cap(rings.front(), rings[1], colorOf(0, 0));
        cap(rings.back(), rings[rings.size() - 2], colorOf(int(rings.size()) - 2, 0));
    }
    void loft(const std::vector<Ring>& rings, int sides, int color) {
        loft(rings, sides, [color](int, int) { return color; });
    }

    /// A ball of twenty faces: what a joint is.
    void ball(Vec3 center, float radius, int bone, int color) {
        const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
        const Vec3 points[12] = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
        const int faces[20][3] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                                  {3, 9, 4},  {3, 4, 2}, {3, 2, 6}, {3, 6, 8},  {3, 8, 9},   {4, 9, 5}, {2, 4, 11}, {6, 2, 10},  {8, 6, 7},  {9, 8, 1}};
        for (const auto& corners : faces) {
            auto at = [&](int i) { return Corner{center + simd::normalize(points[i]) * radius, bone}; };
            face({at(corners[0]), at(corners[1]), at(corners[2])}, color, center);
        }
    }

    /// A box between two corners.
    void box(Vec3 low, Vec3 high, int bone, int color) {
        Vec3 inside = (low + high) * 0.5f;
        auto at = [&](int x, int y, int z) { return Corner{Vec3{x ? high.x : low.x, y ? high.y : low.y, z ? high.z : low.z}, bone}; };
        face({at(0, 0, 0), at(1, 0, 0), at(1, 1, 0), at(0, 1, 0)}, color, inside);
        face({at(0, 0, 1), at(1, 0, 1), at(1, 1, 1), at(0, 1, 1)}, color, inside);
        face({at(0, 0, 0), at(0, 0, 1), at(0, 1, 1), at(0, 1, 0)}, color, inside);
        face({at(1, 0, 0), at(1, 0, 1), at(1, 1, 1), at(1, 1, 0)}, color, inside);
        face({at(0, 0, 0), at(1, 0, 0), at(1, 0, 1), at(0, 0, 1)}, color, inside);
        face({at(0, 1, 0), at(1, 1, 0), at(1, 1, 1), at(0, 1, 1)}, color, inside);
    }
};

/// The figure's body, standing at rest: a suit with a pack on its back, a helmet with a dark
/// visor, and a ball at every joint so that a limb can bend without opening a gap.
Faces body() {
    Faces out;
    // The trunk, from the hips to where the neck begins: it bends between the rings that follow different bones.
    out.loft({{Vec3{0, 0.84f, 0}, 0.115f, 0.095f, Hips},  {Vec3{0, 0.90f, 0}, 0.160f, 0.118f, Hips},  {Vec3{0, 1.01f, 0}, 0.155f, 0.112f, Hips},
              {Vec3{0, 1.10f, 0}, 0.128f, 0.098f, Spine}, {Vec3{0, 1.22f, 0}, 0.150f, 0.110f, Chest}, {Vec3{0, 1.37f, 0.005f}, 0.190f, 0.128f, Chest},
              {Vec3{0, 1.455f, 0}, 0.175f, 0.115f, Chest}, {Vec3{0, 1.50f, 0}, 0.075f, 0.070f, Chest}},
             8, [](int band, int side) {
                 if (band < 2) return int(Boot);                                // the hips: darker, like the boots
                 if (band == 4 && (side == 0 || side == 4)) return int(Accent); // a band across the chest and the back
                 return int(Suit);
             });
    // The pack on its back: what is seen most of it, from behind.
    out.box(Vec3{-0.105f, 1.20f, -0.215f}, Vec3{0.105f, 1.45f, -0.105f}, Chest, Accent);
    out.box(Vec3{-0.075f, 1.26f, -0.235f}, Vec3{0.075f, 1.40f, -0.213f}, Chest, Joint);
    out.loft({{Vec3{0, 1.47f, 0}, 0.052f, 0.052f, Neck}, {Vec3{0, 1.60f, 0}, 0.052f, 0.052f, Neck}}, 6, Joint);
    // The helmet, a little deeper than it is wide, with the visor across its front.
    out.loft({{Vec3{0, 1.565f, 0.005f}, 0.070f, 0.078f, Head}, {Vec3{0, 1.615f, 0.008f}, 0.112f, 0.124f, Head}, {Vec3{0, 1.685f, 0.008f}, 0.124f, 0.136f, Head},
              {Vec3{0, 1.745f, 0.004f}, 0.110f, 0.122f, Head}, {Vec3{0, 1.80f, 0}, 0.062f, 0.070f, Head}},
             8, [](int band, int side) {
                 if (band == 1 && (side == 0 || side == 1 || side == 7)) return int(Visor);
                 if (band == 3 && (side == 0 || side == 4)) return int(Accent);  // a stripe over the top
                 return int(Helmet);
             });
    for (int side = 0; side < 2; side++) {
        float s = side ? -1.0f : 1.0f;                                           // the left first
        int arm = side ? RightArm : LeftArm, leg = side ? RightUpLeg : LeftUpLeg;
        Vec3 shoulder = jointOf(arm), elbow = jointOf(arm + 1), wrist = jointOf(arm + 2);
        out.ball(shoulder, 0.068f, arm, Joint);
        out.loft({{elbow + Vec3{0, 0.02f, 0}, 0.043f, 0.043f, arm}, {shoulder - Vec3{0, 0.03f, 0}, 0.052f, 0.052f, arm}}, 6, Suit);
        out.ball(elbow, 0.047f, arm + 1, Joint);
        out.loft({{wrist + Vec3{0, 0.085f, 0}, 0.037f, 0.037f, arm + 1}, {elbow - Vec3{0, 0.02f, 0}, 0.043f, 0.043f, arm + 1}}, 6, Suit);
        out.loft({{wrist + Vec3{0, 0.005f, 0}, 0.040f, 0.040f, arm + 1}, {wrist + Vec3{0, 0.085f, 0}, 0.042f, 0.042f, arm + 1}}, 6, Boot);    // the glove's cuff
        // The hand: a mitt, its palm toward the body.
        out.loft({{wrist - Vec3{0, 0.115f, -0.01f}, 0.020f, 0.036f, arm + 2}, {wrist - Vec3{0, 0.06f, -0.008f}, 0.026f, 0.046f, arm + 2},
                  {wrist + Vec3{0, 0.01f, 0}, 0.028f, 0.036f, arm + 2}},
                 6, Boot);
        Vec3 hip = jointOf(leg), knee = jointOf(leg + 1), ankle = jointOf(leg + 2);
        out.ball(hip, 0.082f, leg, Boot);
        out.loft({{knee + Vec3{0, 0.03f, 0}, 0.056f, 0.058f, leg}, {hip - Vec3{0, 0.04f, 0}, 0.078f, 0.082f, leg}}, 6, Suit);
        out.ball(knee, 0.058f, leg + 1, Joint);
        out.loft({{knee - Vec3{0, 0.22f, 0}, 0.050f, 0.054f, leg + 1}, {knee - Vec3{0, 0.03f, 0}, 0.056f, 0.058f, leg + 1}}, 6, Suit);
        out.loft({{ankle + Vec3{0, 0.02f, 0}, 0.046f, 0.050f, leg + 1}, {knee - Vec3{0, 0.22f, 0}, 0.054f, 0.058f, leg + 1}}, 6, Boot);        // the boot's leg
        out.ball(ankle, 0.050f, leg + 2, Boot);
        // The boot: its sole on the ground, higher at the ankle than at the toes.
        auto at = [&](float x, float y, float z) { return Faces::Corner{Vec3{ankle.x + s * x, y, z}, leg + 2}; };
        Vec3 inside{ankle.x, 0.04f, 0.04f};
        const float heel = -kHeelBack - 0.012f, toe = 0.195f, bridge = 0.07f;
        out.face({at(-0.046f, 0, heel), at(0.046f, 0, heel), at(0.052f, 0, bridge), at(0.044f, 0, toe), at(-0.044f, 0, toe), at(-0.052f, 0, bridge)}, Sole, inside);
        out.face({at(-0.046f, 0, heel), at(0.046f, 0, heel), at(0.042f, 0.10f, heel + 0.012f), at(-0.042f, 0.10f, heel + 0.012f)}, Boot, inside);
        out.face({at(-0.042f, 0.10f, heel + 0.012f), at(0.042f, 0.10f, heel + 0.012f), at(0.046f, 0.105f, bridge), at(-0.046f, 0.105f, bridge)}, Boot, inside);
        out.face({at(-0.046f, 0.105f, bridge), at(0.046f, 0.105f, bridge), at(0.040f, 0.045f, toe), at(-0.040f, 0.045f, toe)}, Boot, inside);
        out.face({at(-0.040f, 0.045f, toe), at(0.040f, 0.045f, toe), at(0.044f, 0, toe), at(-0.044f, 0, toe)}, Boot, inside);
        for (float x : {-1.0f, 1.0f}) {
            out.face({at(x * 0.046f, 0, heel), at(x * 0.052f, 0, bridge), at(x * 0.046f, 0.105f, bridge), at(x * 0.042f, 0.10f, heel + 0.012f)}, Boot, inside);
            out.face({at(x * 0.052f, 0, bridge), at(x * 0.044f, 0, toe), at(x * 0.040f, 0.045f, toe), at(x * 0.046f, 0.105f, bridge)}, Boot, inside);
        }
    }
    return out;
}

// ---------------------------------------------------------------- how it stands and moves

/// Where the figure is at one moment: its hips, and how every bone is turned against its parent.
struct Pose {
    Vec3 hips{0, kHipsHeight, 0};
    std::array<Quat, kBones> turn;
    Pose() { turn.fill(Quat(0, 0, 0, 1)); }
};

Quat worldTurn(const Pose& pose, int bone) {
    Quat out = pose.turn[size_t(bone)];
    for (int at = kParents[bone]; at >= 0; at = kParents[at]) out = pose.turn[size_t(at)] * out;
    return out;
}

Vec3 worldJoint(const Pose& pose, int bone) {
    if (kParents[bone] < 0) return pose.hips;
    int parent = kParents[bone];
    return worldJoint(pose, parent) + rotate(worldTurn(pose, parent), jointOf(bone) - jointOf(parent));
}

/// The turn that leaves a bone hanging toward `down`, its front as near `forward` as that allows.
Quat hanging(Vec3 down, Vec3 forward) {
    Vec3 up = -simd::normalize(down);
    Vec3 front = safeNormalize(forward - up * simd::dot(forward, up), Vec3{0, 0, 1});
    return lookRotation(-front, up);
}

/// Bends a leg so that its ankle is at `ankle`, its knee toward `forward`, and sets its foot:
/// `footTurn` is how the foot is turned in the world. `leg` is LeftUpLeg or RightUpLeg; the
/// hips must be set already.
void placeLeg(Pose& pose, int leg, Vec3 ankle, Vec3 forward, Quat footTurn) {
    Vec3 hip = worldJoint(pose, leg);
    Vec3 to = ankle - hip;
    float reach = std::clamp(simd::length(to), 0.12f, (kThigh + kShin) * 0.9995f);
    Vec3 along = safeNormalize(to, Vec3{0, -1, 0});
    Vec3 across = safeNormalize(forward - along * simd::dot(forward, along), Vec3{0, 0, 1});
    float alongThigh = (kThigh * kThigh - kShin * kShin + reach * reach) / (2 * reach);
    float out = std::sqrt(std::max(kThigh * kThigh - alongThigh * alongThigh, 0.0f));
    Vec3 knee = hip + along * alongThigh + across * out;
    Vec3 end = hip + along * reach;
    Quat hips = pose.turn[Hips];
    Quat thigh = hanging(knee - hip, forward), shin = hanging(end - knee, forward);
    pose.turn[size_t(leg)] = simd::normalize(simd::conjugate(hips) * thigh);
    pose.turn[size_t(leg + 1)] = simd::normalize(simd::conjugate(thigh) * shin);
    pose.turn[size_t(leg + 2)] = simd::normalize(simd::conjugate(shin) * footTurn);
}

/// A way of going on two feet: a walk, a run.
struct Gait {
    float speed;                        // meters per second over the ground
    float period;                       // seconds for two steps
    float duty;                         // of that, how much a foot is on the ground: above a half is a walk, below it a run
    float reach;                        // where the middle of a foot's time on the ground is against the hips, meters: behind is below zero
    float strikeAngle, pushAngle;       // degrees: toes up as the heel comes down, heel up as the toes leave
    float flatBy, heelOffAt;            // through its time on the ground: flat by here, the heel rising from here
    float clearance;                    // how high the ankle is swung, meters
    float swingEarly;                   // below 1: the foot is highest early in its swing (a runner's heel comes up behind)
    float bounce, sway;                 // the hips: up and down, side to side, meters
    float pelvisYaw, pelvisRoll;        // degrees
    float chestYaw;                     // the shoulders against the hips
    float lean;                         // degrees forward
    float armSwing, armForward;         // degrees: the swing, and how far forward of straight down its middle is
    float elbow, elbowSwing;            // how bent, and how much more as the arm comes forward
    float armOut, armTwist;             // away from the body; the forearm turned in across it
    float feetApart, toesOut;           // each foot from the middle line, meters; degrees
    float soft;                         // the legs are kept this much short of straight, meters
};

const Gait kWalk{Figure::kWalkSpeed, 0.88f, 0.58f, -0.085f, 17, 56, 0.16f, 0.50f, 0.050f, 0.8f, 0.016f, 0.020f, 6, 2.5f, 7, 3, 23, 3, 12, 16, 6, 0, 0.070f, 7, 0.006f};
const Gait kRun{Figure::kRunSpeed, 0.62f, 0.36f, -0.25f, 3, 50, 0.10f, 0.30f, 0.26f, 0.55f, 0.035f, 0.008f, 10, 3, 15, 12, 44, 10, 84, 12, 9, 20, 0.045f, 3, 0.02f};

struct FootAt {
    float height, ahead;                // of the ankle: above the ground, ahead of the hips
    float pitch;                        // degrees: toes down above zero
};

/// A foot on the ground, `through` (0 to 1) its time there. The ground passes under the body
/// at the gait's speed, and the foot goes with it: on its heel at first, toes up; then flat;
/// then up on the ball as the heel rises.
FootAt planted(const Gait& gait, float through) {
    float travel = gait.speed * gait.duty * gait.period;
    float flat = gait.reach + travel * (0.5f - through);            // where the ankle would be were the foot flat all the way
    float pitch = 0;
    if (through < gait.flatBy) pitch = -gait.strikeAngle * (1 - through / gait.flatBy);
    else if (through > gait.heelOffAt) pitch = gait.pushAngle * std::pow(smooth((through - gait.heelOffAt) / (1 - gait.heelOffAt)), 1.3f);
    // It turns about what it stands on: the heel's edge while the toes are up, the ball while the heel is.
    Vec3 pivot = pitch < 0 ? Vec3{0, -kAnkle, -kHeelBack} : Vec3{0, -kAnkle, kBallAhead};
    Vec3 turned = rotate(aboutX(pitch), pivot);
    return FootAt{-turned.y, flat + pivot.z - turned.z, pitch};
}

/// A foot at `phase` (0 to 1) of the gait: on the ground for the first `duty` of it, swung
/// forward for the rest, leaving and arriving as fast as the ground goes.
FootAt footAt(const Gait& gait, float phase) {
    phase = cycle(phase);
    if (phase < gait.duty) return planted(gait, phase / gait.duty);
    float u = (phase - gait.duty) / (1 - gait.duty);
    FootAt from = planted(gait, 1), to = planted(gait, 0);
    float slope = -gait.speed * gait.period * (1 - gait.duty);
    float u2 = u * u, u3 = u2 * u;
    FootAt out;
    out.ahead = (2 * u3 - 3 * u2 + 1) * from.ahead + (u3 - 2 * u2 + u) * slope + (-2 * u3 + 3 * u2) * to.ahead + (u3 - u2) * slope;
    out.height = lerp(from.height, to.height, smooth(u)) + gait.clearance * std::sin(kPi * std::pow(u, gait.swingEarly));
    out.pitch = lerp(from.pitch, to.pitch, smooth((u - 0.12f) / 0.76f));
    return out;
}

/// The hips go up and down twice in a cycle: a walker is highest over the foot it stands on,
/// a runner lowest there and highest in the air between steps.
float bounceAt(const Gait& gait, float phase) {
    return (gait.duty > 0.5f ? 1.0f : -1.0f) * gait.bounce * wave(2 * (phase - gait.duty * 0.5f));
}

/// How high the hips can be carried through the gait with both legs able to reach their feet.
float hipsHeightFor(const Gait& gait) {
    float longest = kThigh + kShin - gait.soft;
    float height = kHipsHeight - 0.008f;
    for (int i = 0; i < 240; i++) {
        float phase = float(i) / 240.0f;
        for (float offset : {0.0f, 0.5f}) {
            float mine = cycle(phase + offset);
            // Pushing off, a leg may be at its full length and the foot leave a little early: it does not hold the hips down.
            if (mine >= gait.duty * 0.8f) continue;
            FootAt foot = planted(gait, mine / gait.duty);
            float side = gait.feetApart - kLegsApart;
            float room = longest * longest - foot.ahead * foot.ahead - side * side;
            float most = foot.height + std::sqrt(std::max(room, 0.01f)) + (kHipsHeight - jointOf(LeftUpLeg).y);
            height = std::min(height, most - bounceAt(gait, phase));
        }
    }
    return height;
}

/// The arms and the upper body of a gait, on a pose whose hips are set.
void carryUpperBody(Pose& pose, const Gait& gait, float phase, float pelvisYaw, float pelvisRoll) {
    float chestYaw = gait.chestYaw * wave(phase);           // the right shoulder comes forward with the left foot
    float between = (chestYaw - pelvisYaw) * 0.5f;
    pose.turn[Spine] = aboutY(between) * aboutX(gait.lean * 0.35f) * aboutZ(-pelvisRoll * 0.6f);
    pose.turn[Chest] = aboutY(between) * aboutX(gait.lean * 0.35f) * aboutZ(-pelvisRoll * 0.4f);
    // The head looks where the body is going, whatever the shoulders do.
    pose.turn[Neck] = aboutY(-chestYaw * 0.5f) * aboutX(-gait.lean * 0.45f);
    pose.turn[Head] = aboutY(-chestYaw * 0.5f) * aboutX(-gait.lean * 0.4f);
    for (int side = 0; side < 2; side++) {
        float s = side ? -1.0f : 1.0f;
        int arm = side ? RightArm : LeftArm;
        // An arm is forward when the leg of the other side is: the left arm at half way through.
        float forward = wave(phase - (side ? 0.0f : 0.5f));
        float swing = -gait.armForward - gait.armSwing * forward;                       // forward is below zero
        pose.turn[size_t(arm)] = aboutZ(s * gait.armOut) * aboutX(swing) * aboutY(-s * gait.armTwist);
        pose.turn[size_t(arm + 1)] = aboutX(-(gait.elbow + gait.elbowSwing * 0.5f * (1 + wave(phase - (side ? 0.0f : 0.5f) - 0.04f))));
        pose.turn[size_t(arm + 2)] = aboutX(-6) * aboutZ(-s * 4);
    }
}

/// The figure at `phase` (0 to 1) of a gait. The left heel comes down at 0, the right at a half.
Pose stride(const Gait& gait, float phase, float hipsHeight) {
    Pose pose;
    float pelvisYaw = -gait.pelvisYaw * wave(phase);                        // the left hip forward as the left foot lands
    float pelvisRoll = gait.pelvisRoll * wave(phase - gait.duty * 0.5f);    // the hip of the leg in the air a little lower
    pose.hips = Vec3{gait.sway * wave(phase - gait.duty * 0.5f), hipsHeight + bounceAt(gait, phase), 0};
    pose.turn[Hips] = aboutY(pelvisYaw) * aboutX(gait.lean * 0.3f) * aboutZ(pelvisRoll);
    carryUpperBody(pose, gait, phase, pelvisYaw, pelvisRoll);
    for (int side = 0; side < 2; side++) {
        float s = side ? -1.0f : 1.0f;
        FootAt foot = footAt(gait, phase + (side ? 0.5f : 0.0f));
        Vec3 ankle{s * gait.feetApart, foot.height, foot.ahead};
        Vec3 knee = simd::normalize(Vec3{s * 0.10f, 0, 1});
        placeLeg(pose, side ? RightUpLeg : LeftUpLeg, ankle, knee, aboutY(s * gait.toesOut) * aboutX(foot.pitch));
    }
    return pose;
}

/// Standing: both feet where they are, the body breathing and shifting its weight over them.
Pose standing(float through) {
    Pose pose;
    float breath = wave(through * 2 - 0.25f), shift = wave(through - 0.25f);    // two breaths while the weight goes over and back
    pose.hips = Vec3{0.012f * shift, kHipsHeight - 0.014f + 0.003f * breath, -0.004f};
    pose.turn[Hips] = aboutZ(1.2f * shift) * aboutX(1.0f);
    pose.turn[Spine] = aboutZ(-0.8f * shift) * aboutX(1.0f + 0.7f * breath);
    pose.turn[Chest] = aboutZ(-0.6f * shift) * aboutX(0.5f + 0.9f * breath) * aboutY(1.2f * wave(through + 0.1f));
    pose.turn[Neck] = aboutX(-1.0f - 0.6f * breath);
    pose.turn[Head] = aboutX(-1.5f - 0.5f * breath) * aboutY(4.0f * wave(through - 0.1f));
    for (int side = 0; side < 2; side++) {
        float s = side ? -1.0f : 1.0f;
        int arm = side ? RightArm : LeftArm;
        pose.turn[size_t(arm)] = aboutZ(s * (7.0f + 0.8f * breath)) * aboutX(2.0f + 1.5f * wave(through + (side ? 0.3f : 0.0f)));
        pose.turn[size_t(arm + 1)] = aboutX(-14.0f - 1.5f * breath);
        pose.turn[size_t(arm + 2)] = aboutX(-6) * aboutZ(-s * 5);
        placeLeg(pose, side ? RightUpLeg : LeftUpLeg, Vec3{s * 0.105f, kAnkle, 0.0f}, simd::normalize(Vec3{s * 0.18f, 0, 1}), aboutY(s * 10));
    }
    return pose;
}

/// A body off the ground, by the angles of its limbs (degrees).
struct InAir {
    float thigh[2], knee[2], foot[2];   // left, right: the thigh forward, the knee bent, the toes down
    float arm[2], armOut[2], elbow[2];  // the arm forward (up, past 90), away from the body, bent
    float lean;                         // the body forward
    float rise;                         // the hips against standing, meters
};

InAir mix(const InAir& a, const InAir& b, float t) {
    InAir out;
    for (int i = 0; i < 2; i++) {
        out.thigh[i] = lerp(a.thigh[i], b.thigh[i], t), out.knee[i] = lerp(a.knee[i], b.knee[i], t), out.foot[i] = lerp(a.foot[i], b.foot[i], t);
        out.arm[i] = lerp(a.arm[i], b.arm[i], t), out.armOut[i] = lerp(a.armOut[i], b.armOut[i], t), out.elbow[i] = lerp(a.elbow[i], b.elbow[i], t);
    }
    out.lean = lerp(a.lean, b.lean, t), out.rise = lerp(a.rise, b.rise, t);
    return out;
}

Pose inAir(const InAir& air) {
    Pose pose;
    pose.hips = Vec3{0, kHipsHeight + air.rise, 0};
    pose.turn[Hips] = aboutX(air.lean * 0.4f);
    pose.turn[Spine] = aboutX(air.lean * 0.3f);
    pose.turn[Chest] = aboutX(air.lean * 0.3f);
    pose.turn[Neck] = aboutX(-air.lean * 0.4f);
    pose.turn[Head] = aboutX(-air.lean * 0.4f);
    for (int side = 0; side < 2; side++) {
        float s = side ? -1.0f : 1.0f;
        int arm = side ? RightArm : LeftArm, leg = side ? RightUpLeg : LeftUpLeg;
        pose.turn[size_t(leg)] = aboutX(-air.thigh[side] - air.lean * 0.4f) * aboutZ(s * 3);
        pose.turn[size_t(leg + 1)] = aboutX(air.knee[side]);
        pose.turn[size_t(leg + 2)] = aboutX(air.foot[side]);
        pose.turn[size_t(arm)] = aboutZ(s * air.armOut[side]) * aboutX(-air.arm[side]);
        pose.turn[size_t(arm + 1)] = aboutX(-air.elbow[side]);
        pose.turn[size_t(arm + 2)] = aboutX(-8);
    }
    return pose;
}

// Leaving the ground: one knee driven up and the arms thrown forward, then opening into the fall.
const InAir kPushOff{{18, -12}, {48, 20}, {22, 48}, {-28, -22}, {8, 8}, {38, 34}, 9, 0};
const InAir kReach{{46, -16}, {78, 14}, {18, 44}, {72, 58}, {14, 16}, {46, 52}, 4, 0.02f};
const InAir kFalling{{24, 8}, {44, 30}, {26, 30}, {26, 20}, {44, 46}, {40, 44}, 5, 0};

struct Clip {
    std::string name;
    float length = 1;
    std::vector<Pose> keys;             // evenly through `length`, the first at 0 and the last at its end
};

Clip clipOf(const std::string& name, float length, int keys, const std::function<Pose(float through)>& at) {
    Clip clip{name, length, {}};
    for (int i = 0; i <= keys; i++) clip.keys.push_back(at(float(i) / float(keys)));
    return clip;
}

std::vector<Clip> clips() {
    std::vector<Clip> out;
    out.push_back(clipOf("Idle", 4.0f, 48, [](float through) { return standing(through); }));
    float walkHips = hipsHeightFor(kWalk), runHips = hipsHeightFor(kRun);
    out.push_back(clipOf("Walk", kWalk.period, 28, [&](float through) { return stride(kWalk, through, walkHips); }));
    out.push_back(clipOf("Run", kRun.period, 20, [&](float through) { return stride(kRun, through, runHips); }));
    out.push_back(clipOf("Jump", 0.45f, 14, [](float through) {
        return through < 0.4f ? inAir(mix(kPushOff, kReach, smooth(through / 0.4f))) : inAir(mix(kReach, kFalling, smooth((through - 0.4f) / 0.6f)));
    }));
    out.push_back(clipOf("Fall", 1.2f, 18, [](float through) {
        // Falling, with the arms and legs never quite still.
        InAir air = kFalling;
        float flutter = wave(through), other = wave(through * 2 + 0.2f);
        for (int side = 0; side < 2; side++) {
            float s = side ? -1.0f : 1.0f;
            air.thigh[side] += 4 * s * flutter, air.knee[side] += 5 * other * s;
            air.arm[side] += 6 * s * other, air.armOut[side] += 5 * flutter, air.elbow[side] += 4 * s * flutter;
        }
        return inAir(air);
    }));
    return out;
}

// ---------------------------------------------------------------- the file

/// A .glb file's numbers: every list of them gets a view and an accessor of its own.
struct Packed {
    std::vector<uint8_t> bytes;
    std::ostringstream views, accessors;
    int count = 0;

    int add(const void* data, size_t size, int componentType, const char* type, size_t items, const std::string& extra = "") {
        while (bytes.size() % 4) bytes.push_back(0);
        if (count) views << ",", accessors << ",";
        views << "{\"buffer\":0,\"byteOffset\":" << bytes.size() << ",\"byteLength\":" << size << "}";
        accessors << "{\"bufferView\":" << count << ",\"componentType\":" << componentType << ",\"count\":" << items << ",\"type\":\"" << type << "\"" << extra << "}";
        const uint8_t* from = static_cast<const uint8_t*>(data);
        bytes.insert(bytes.end(), from, from + size);
        return count++;
    }
    int floats(const std::vector<float>& values, const char* type, size_t items, const std::string& extra = "") {
        return add(values.data(), values.size() * sizeof(float), 5126, type, items, extra);
    }
};

std::string number(float value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

}  // namespace

std::vector<uint8_t> pngOf(int width, int height, const std::vector<uint8_t>& rgb) {
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    auto crcOf = [](const uint8_t* data, size_t size) {
        uint32_t crc = 0xFFFFFFFFu;
        for (size_t i = 0; i < size; i++) {
            crc ^= data[i];
            for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
        return ~crc;
    };
    auto big = [](std::vector<uint8_t>& to, uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8) to.push_back(uint8_t(value >> shift));
    };
    auto chunk = [&](const char* kind, const std::vector<uint8_t>& data) {
        big(out, uint32_t(data.size()));
        std::vector<uint8_t> body(kind, kind + 4);
        body.insert(body.end(), data.begin(), data.end());
        out.insert(out.end(), body.begin(), body.end());
        big(out, crcOf(body.data(), body.size()));
    };
    std::vector<uint8_t> header;
    big(header, uint32_t(width)), big(header, uint32_t(height));
    header.insert(header.end(), {8, 2, 0, 0, 0});                   // 8 bits, RGB, nothing interlaced
    chunk("IHDR", header);
    // Every row starts with the byte that says it is as written; the rows go in "stored" blocks, which pack nothing.
    std::vector<uint8_t> rows;
    for (int y = 0; y < height; y++) {
        rows.push_back(0);
        rows.insert(rows.end(), rgb.begin() + long(y) * width * 3, rgb.begin() + long(y + 1) * width * 3);
    }
    std::vector<uint8_t> packed = {0x78, 0x01};
    for (size_t at = 0; at < rows.size();) {
        size_t size = std::min<size_t>(rows.size() - at, 65535);
        packed.push_back(at + size >= rows.size() ? 1 : 0);
        packed.push_back(uint8_t(size & 0xFF)), packed.push_back(uint8_t(size >> 8));
        packed.push_back(uint8_t(~size & 0xFF)), packed.push_back(uint8_t((~size >> 8) & 0xFF));
        packed.insert(packed.end(), rows.begin() + long(at), rows.begin() + long(at + size));
        at += size;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t byte : rows) a = (a + byte) % 65521u, b = (b + a) % 65521u;
    big(packed, (b << 16) | a);
    chunk("IDAT", packed);
    chunk("IEND", {});
    return out;
}

Figure makeFigure() {
    Figure figure;

    // Its colors: a square each, four across. A face's corners all point at the middle of its square.
    const int kSquare = 8, kAcross = 4;
    std::vector<uint8_t> pixels(size_t(kSquare * kAcross) * size_t(kSquare * kAcross) * 3);
    for (int y = 0; y < kSquare * kAcross; y++) {
        for (int x = 0; x < kSquare * kAcross; x++) {
            int color = std::min((y / kSquare) * kAcross + x / kSquare, int(kColors) - 1);
            for (int c = 0; c < 3; c++) {
                float value = kPalette[color][c];
                // The picture is read as sRGB: store what shows as the color meant.
                float stored = value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1 / 2.4f) - 0.055f;
                pixels[(size_t(y) * size_t(kSquare * kAcross) + size_t(x)) * 3 + size_t(c)] = uint8_t(std::lround(saturate(stored) * 255));
            }
        }
    }
    figure.palette = pngOf(kSquare * kAcross, kSquare * kAcross, pixels);

    Faces faces = body();
    std::vector<float> positions, normals, uvs, weights;
    std::vector<uint8_t> joints;
    std::vector<uint16_t> indices;
    Vec3 low{1e9f, 1e9f, 1e9f}, high{-1e9f, -1e9f, -1e9f};
    for (const Faces::Triangle& triangle : faces.triangles) {
        Vec3 normal = safeNormalize(simd::cross(triangle.b.at - triangle.a.at, triangle.c.at - triangle.a.at));
        Vec2 uv{(float(triangle.color % kAcross) + 0.5f) / float(kAcross), (float(triangle.color / kAcross) + 0.5f) / float(kAcross)};
        for (const Faces::Corner* corner : {&triangle.a, &triangle.b, &triangle.c}) {
            indices.push_back(uint16_t(positions.size() / 3));
            positions.insert(positions.end(), {corner->at.x, corner->at.y, corner->at.z});
            normals.insert(normals.end(), {normal.x, normal.y, normal.z});
            uvs.insert(uvs.end(), {uv.x, uv.y});
            joints.insert(joints.end(), {uint8_t(corner->bone), 0, 0, 0});
            weights.insert(weights.end(), {1, 0, 0, 0});
            low = simd::min(low, corner->at), high = simd::max(high, corner->at);
        }
    }
    size_t corners = positions.size() / 3;

    Packed packed;
    std::string bounds = ",\"min\":[" + number(low.x) + "," + number(low.y) + "," + number(low.z) + "],\"max\":[" + number(high.x) + "," + number(high.y) + "," + number(high.z) + "]";
    int positionsAt = packed.floats(positions, "VEC3", corners, bounds);
    int normalsAt = packed.floats(normals, "VEC3", corners);
    int uvsAt = packed.floats(uvs, "VEC2", corners);
    int jointsAt = packed.add(joints.data(), joints.size(), 5121, "VEC4", corners);
    int weightsAt = packed.floats(weights, "VEC4", corners);
    int indicesAt = packed.add(indices.data(), indices.size() * sizeof(uint16_t), 5123, "SCALAR", indices.size());
    // Each bone's matrix from the body as it was made to the bone's own space: at rest no bone is turned, so it only moves.
    std::vector<float> inverseBind;
    for (int bone = 0; bone < kBones; bone++) {
        Vec3 joint = jointOf(bone);
        inverseBind.insert(inverseBind.end(), {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -joint.x, -joint.y, -joint.z, 1});
    }
    int inverseBindAt = packed.floats(inverseBind, "MAT4", kBones);

    std::ostringstream animations;
    std::vector<Clip> all = clips();
    for (size_t c = 0; c < all.size(); c++) {
        const Clip& clip = all[c];
        std::vector<float> times;
        for (size_t k = 0; k < clip.keys.size(); k++) times.push_back(clip.length * float(k) / float(clip.keys.size() - 1));
        int timesAt = packed.floats(times, "SCALAR", times.size(), ",\"min\":[0],\"max\":[" + number(clip.length) + "]");
        animations << (c ? "," : "") << "{\"name\":\"" << clip.name << "\",\"channels\":[";
        std::ostringstream samplers;
        for (int bone = 0; bone < kBones; bone++) {
            std::vector<float> turns;
            Quat last(0, 0, 0, 1);
            for (const Pose& key : clip.keys) {
                Quat turn = simd::normalize(key.turn[size_t(bone)]);
                if (simd::dot(turn.vector, last.vector) < 0) turn = Quat(-turn.vector.x, -turn.vector.y, -turn.vector.z, -turn.vector.w);     // the short way round from the key before
                last = turn;
                turns.insert(turns.end(), {turn.vector.x, turn.vector.y, turn.vector.z, turn.vector.w});
            }
            int turnsAt = packed.floats(turns, "VEC4", clip.keys.size());
            animations << (bone ? "," : "") << "{\"sampler\":" << bone << ",\"target\":{\"node\":" << bone << ",\"path\":\"rotation\"}}";
            samplers << (bone ? "," : "") << "{\"input\":" << timesAt << ",\"output\":" << turnsAt << ",\"interpolation\":\"LINEAR\"}";
        }
        std::vector<float> places;
        for (const Pose& key : clip.keys) places.insert(places.end(), {key.hips.x, key.hips.y, key.hips.z});
        int placesAt = packed.floats(places, "VEC3", clip.keys.size());
        animations << ",{\"sampler\":" << int(kBones) << ",\"target\":{\"node\":0,\"path\":\"translation\"}}],\"samplers\":[" << samplers.str() << ",{\"input\":" << timesAt
                   << ",\"output\":" << placesAt << ",\"interpolation\":\"LINEAR\"}]}";
    }
    while (packed.bytes.size() % 4) packed.bytes.push_back(0);

    std::ostringstream json;
    json << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"Molten Metal Engine\"},\"scene\":0,\"scenes\":[{\"nodes\":[0," << int(kBones) << "]}],\"nodes\":[";
    for (int bone = 0; bone < kBones; bone++) {
        Vec3 from = kParents[bone] >= 0 ? jointOf(kParents[bone]) : Vec3{0, 0, 0};
        Vec3 offset = jointOf(bone) - from;
        json << "{\"name\":\"" << kNames[bone] << "\",\"translation\":[" << number(offset.x) << "," << number(offset.y) << "," << number(offset.z) << "]";
        std::string children;
        for (int child = 0; child < kBones; child++) {
            if (kParents[child] == bone) children += (children.empty() ? "" : ",") + std::to_string(child);
        }
        if (!children.empty()) json << ",\"children\":[" << children << "]";
        json << "},";
    }
    json << "{\"name\":\"Figure\",\"mesh\":0,\"skin\":0}],";
    json << "\"meshes\":[{\"name\":\"Figure\",\"primitives\":[{\"attributes\":{\"POSITION\":" << positionsAt << ",\"NORMAL\":" << normalsAt << ",\"TEXCOORD_0\":" << uvsAt
         << ",\"JOINTS_0\":" << jointsAt << ",\"WEIGHTS_0\":" << weightsAt << "},\"indices\":" << indicesAt << ",\"material\":0}]}],";
    json << "\"skins\":[{\"name\":\"Figure\",\"skeleton\":0,\"inverseBindMatrices\":" << inverseBindAt << ",\"joints\":[";
    for (int bone = 0; bone < kBones; bone++) json << (bone ? "," : "") << bone;
    json << "]}],";
    json << "\"materials\":[{\"name\":\"Figure\",\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0},\"metallicFactor\":0,\"roughnessFactor\":0.6}}],";
    json << "\"textures\":[{\"sampler\":0,\"source\":0}],\"samplers\":[{\"magFilter\":9728,\"minFilter\":9728}],\"images\":[{\"uri\":\"Figure.png\"}],";
    json << "\"animations\":[" << animations.str() << "],";
    json << "\"buffers\":[{\"byteLength\":" << packed.bytes.size() << "}],\"bufferViews\":[" << packed.views.str() << "],\"accessors\":[" << packed.accessors.str() << "]}";
    std::string text = json.str();
    while (text.size() % 4) text += ' ';

    auto u32 = [&](uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) figure.glb.push_back(uint8_t(value >> shift));
    };
    figure.glb.insert(figure.glb.end(), {'g', 'l', 'T', 'F'});
    u32(2);
    u32(uint32_t(12 + 8 + text.size() + 8 + packed.bytes.size()));
    u32(uint32_t(text.size()));
    u32(0x4E4F534A);
    figure.glb.insert(figure.glb.end(), text.begin(), text.end());
    u32(uint32_t(packed.bytes.size()));
    u32(0x004E4942);
    figure.glb.insert(figure.glb.end(), packed.bytes.begin(), packed.bytes.end());
    return figure;
}

}  // namespace starter

// ---------------------------------------------------------------- the figure, read back and watched

using namespace molten;

namespace {

namespace fs = std::filesystem;

std::optional<GltfModel> figureModel() {
    starter::Figure figure = starter::makeFigure();
    fs::path folder = fs::temp_directory_path() / "molten-figure";
    fs::create_directories(folder);
    std::string path = (folder / "Figure.glb").string();
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(figure.glb.data()), long(figure.glb.size()));
    return GltfModel::load(path);
}

int nodeCalled(const GltfModel& model, const char* name) {
    for (size_t i = 0; i < model.nodes.size(); i++) {
        if (model.nodes[i].name == name) return int(i);
    }
    return -1;
}

/// Where a node is at a time of a clip, in the model's space.
Vec3 nodeAt(const GltfModel& model, int animation, float time, int node) {
    GltfPose pose = model.restPose();
    model.sample(animation, time, pose);
    return transformPoint(model.globalMatrices(pose)[size_t(node)], Vec3{0, 0, 0});
}

}  // namespace

TEST(starter_figure_is_a_person_with_bones_and_the_clips_a_character_needs) {
    std::optional<GltfModel> model = figureModel();
    CHECK(model.has_value());
    if (!model) return;
    CHECK(model->skins.size() == 1 && model->skins[0].joints.size() == 17);
    for (const char* clip : {"Idle", "Walk", "Run", "Jump", "Fall"}) CHECK(model->findAnimation(clip) >= 0);
    // At rest it stands on the ground, 1.8 meters tall, about its own middle.
    MeshData standing = model->assembled(model->restPose());
    AABB bounds = standing.bounds();
    CHECK_NEAR(bounds.min.y, 0.0f, 0.002f);
    CHECK_NEAR(bounds.max.y, 1.8f, 0.01f);
    CHECK_NEAR(bounds.min.x + bounds.max.x, 0.0f, 0.002f);
    CHECK(standing.indices.size() > 1500 && standing.positions.size() < 12000);
    // Every face looks outward: the room it encloses is what a person takes up.
    float volume = 0;
    for (size_t i = 0; i + 2 < standing.indices.size(); i += 3) {
        volume += simd::dot(standing.positions[standing.indices[i]], simd::cross(standing.positions[standing.indices[i + 1]], standing.positions[standing.indices[i + 2]])) / 6.0f;
    }
    CHECK(volume > 0.05f && volume < 0.12f);
    // Its colors are a picture a program can read.
    starter::Figure figure = starter::makeFigure();
    CHECK(figure.palette.size() > 100 && figure.palette[1] == 'P' && figure.palette[2] == 'N' && figure.palette[3] == 'G');
    // A clip that loops ends as it began.
    for (const char* clip : {"Idle", "Walk", "Run", "Fall"}) {
        int animation = model->findAnimation(clip);
        float length = model->animations[size_t(animation)].duration;
        for (const char* bone : {"LeftFoot", "RightHand", "Head"}) {
            int node = nodeCalled(*model, bone);
            CHECK(simd::length(nodeAt(*model, animation, 0, node) - nodeAt(*model, animation, length, node)) < 0.002f);
        }
    }
}

TEST(starter_figure_walks_and_runs_without_its_feet_sliding) {
    std::optional<GltfModel> model = figureModel();
    CHECK(model.has_value());
    if (!model) return;
    struct Gait {
        const char* clip;
        float speed, from, to;          // the part of the clip in which the left foot is flat on the ground
    };
    for (const Gait& gait : {Gait{"Walk", starter::Figure::kWalkSpeed, 0.12f, 0.28f}, Gait{"Run", starter::Figure::kRunSpeed, 0.06f, 0.11f}}) {
        int animation = model->findAnimation(gait.clip);
        float length = model->animations[size_t(animation)].duration;
        int foot = nodeCalled(*model, "LeftFoot");
        // The body goes forward at the gait's speed, so a foot that stays where it is goes back at that speed under it.
        Vec3 early = nodeAt(*model, animation, gait.from * length, foot), late = nodeAt(*model, animation, gait.to * length, foot);
        float back = (early.z - late.z) / ((gait.to - gait.from) * length);
        CHECK_NEAR(back, gait.speed, gait.speed * 0.06f);
        CHECK_NEAR(early.y, 0.085f, 0.004f);
        CHECK_NEAR(late.y, 0.085f, 0.004f);
        // Nothing of it goes through the ground at any time, and its head stays about where a head is.
        for (int i = 0; i < 40; i++) {
            GltfPose pose = model->restPose();
            model->sample(animation, length * float(i) / 40.0f, pose);
            AABB bounds = model->assembled(pose).bounds();
            CHECK(bounds.min.y > -0.012f);
            CHECK(bounds.max.y > 1.6f && bounds.max.y < 1.86f);
        }
    }
}
