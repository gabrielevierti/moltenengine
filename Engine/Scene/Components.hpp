// Components.hpp
// The components the engine itself understands. Rendering, physics and particle
// components live next to the systems that use them.
#pragma once

#include <string>
#include "Scene/Scene.hpp"

#include <functional>

namespace molten {

class Engine;

/// A camera looks down the -Z axis of its entity's Transform. The first active
/// camera found in the scene is the one the renderer uses.
struct Camera {
    float fovY = radians(60.0f);           // vertical field of view, in radians
    float nearPlane = 0.1f;
    float farPlane = 500.0f;
    bool isOrthographic = false;
    float orthographicHeight = 10.0f;      // world units visible from bottom to top
    bool isActive = true;

    Mat4 projection(float aspect) const {
        if (isOrthographic) {
            float h = orthographicHeight * 0.5f, w = h * aspect;
            return orthographic(-w, w, -h, h, nearPlane, farPlane);
        }
        return perspective(fovY, aspect, nearPlane, farPlane);
    }
};

enum class LightType : uint32_t { Directional = 0, Point = 1, Spot = 2, Area = 3 };

/// A light shines along the -Z axis of its entity's Transform (directional and spot),
/// or in every direction from its position (point).
struct Light {
    LightType type = LightType::Point;
    Vec3 color{1, 1, 1};                   // linear RGB
    float intensity = 1.0f;
    float range = 10.0f;                   // point and spot: no light beyond this distance
    float innerConeAngle = radians(20.0f); // spot: full brightness inside this half-angle
    float outerConeAngle = radians(30.0f); // spot: darkness outside this half-angle
    bool castsShadows = false;             // honored for the first directional light (the sun)
    bool isEnabled = true;                 // off: it is there, and gives no light
    Vec2 areaSize{2.0f, 1.0f};             // area: the glowing rectangle, meters across (x) and up (y), facing its -Z
    /// Spot: a cookie, as in Unity: a picture in Assets the light shines through, spread across its
    /// cone (white lets it through, black stops it, colors tint it): a window's frame, leaves, a gobo.
    /// Turned with the light about its axis. Empty: none.
    std::string cookie;
    /// Point and spot: it lights the air it passes through, as a lamp in the mist or a beam in a
    /// dusty room; more where there is more fog. It is seen in front of what it lights, not
    /// through walls, and casts no shadow in the air.
    bool isVolumetric = false;
    float volumetricStrength = 1.0f;       // how much it lights the air, 1 as fog would be lit
};

/// Game logic attached to one entity. `update` runs once per frame, `fixedUpdate` once
/// per physics step. Either may be empty.
struct Behavior {
    std::function<void(Engine&, Entity, float)> update;
    std::function<void(Engine&, Entity, float)> fixedUpdate;
};

}  // namespace molten
