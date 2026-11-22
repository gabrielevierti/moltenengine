#include "Rendering/Particles.hpp"
#include "Core/TextFile.hpp"
#include "Physics/Physics.hpp"

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <algorithm>
#include <cmath>

namespace molten {

Vec3 windAt(Vec3 wind, float gustiness, Vec3 position, double time) {
    float speed = simd::length(wind);
    if (speed < 1e-4f || gustiness <= 0) return wind;
    // Two waves of different length travelling downwind, so that gusts arrive in an
    // irregular rhythm and pass over neighbouring things one after the other.
    Vec3 direction = wind / speed;
    float along = simd::dot(position, direction) - speed * float(time);
    float across = position.x * direction.z - position.z * direction.x;
    float gust = 0.6f * std::sin(along * 0.35f + across * 0.11f) + 0.4f * std::sin(along * 0.83f - across * 0.27f + 1.7f);
    return wind * std::max(0.0f, 1.0f + gustiness * gust);
}

Vec4 particleColor(const ParticleEmitter& emitter, float t) {
    if (!emitter.hasMiddle) return emitter.colorStart + (emitter.colorEnd - emitter.colorStart) * t;
    float middle = std::clamp(emitter.middleAt, 0.001f, 0.999f);
    if (t < middle) return emitter.colorStart + (emitter.colorMiddle - emitter.colorStart) * (t / middle);
    return emitter.colorMiddle + (emitter.colorEnd - emitter.colorMiddle) * ((t - middle) / (1 - middle));
}

float particleSize(const ParticleEmitter& emitter, float t) {
    if (!emitter.hasMiddle) return lerp(emitter.sizeStart, emitter.sizeEnd, t);
    float middle = std::clamp(emitter.middleAt, 0.001f, 0.999f);
    if (t < middle) return lerp(emitter.sizeStart, emitter.sizeMiddle, t / middle);
    return lerp(emitter.sizeMiddle, emitter.sizeEnd, (t - middle) / (1 - middle));
}

namespace {

/// A field that changes smoothly from place to place and slowly in time.
Vec3 potentialAt(Vec3 q, float t) {
    return Vec3{std::sin(q.y * 1.7f + t * 1.3f) + std::cos(q.z * 1.3f - t), std::sin(q.z * 1.9f - t * 0.9f) + std::cos(q.x * 1.1f + t * 1.1f),
                std::sin(q.x * 1.5f + t) + std::cos(q.y * 1.2f - t * 1.2f)};
}

/// How air swirls: the curl of that field, which neither gathers nor spreads what it carries, so
/// that smoke curls into eddies instead of clumping. Each octave adds smaller, weaker swirls.
Vec3 swirlAt(Vec3 p, float size, double time, int octaves) {
    Vec3 sum{0, 0, 0};
    float amplitude = 0.5f, scale = 1.0f / std::max(size, 0.01f);
    float t = float(time) * 0.35f;
    for (int octave = 0; octave < std::clamp(octaves, 1, 3); octave++) {
        Vec3 q = p * scale + Vec3{float(octave) * 17.3f, float(octave) * 9.1f, float(octave) * 4.7f};
        const float e = 0.15f;
        Vec3 dx = (potentialAt(q + Vec3{e, 0, 0}, t) - potentialAt(q - Vec3{e, 0, 0}, t)) / (2 * e);
        Vec3 dy = (potentialAt(q + Vec3{0, e, 0}, t) - potentialAt(q - Vec3{0, e, 0}, t)) / (2 * e);
        Vec3 dz = (potentialAt(q + Vec3{0, 0, e}, t) - potentialAt(q - Vec3{0, 0, e}, t)) / (2 * e);
        sum += Vec3{dy.z - dz.y, dz.x - dx.z, dx.y - dy.x} * amplitude;
        amplitude *= 0.5f;
        scale *= 2.1f;
        t *= 1.4f;
    }
    return sum;
}

/// Every setting of an emitter, by name, with where its numbers are.
struct EffectValue {
    const char* key;
    int count;
    float* (*at)(ParticleEmitter&);
    bool isExtra;           // not in the two `particles` lines of a scene file
};

// Flags and whole numbers are kept as floats on the way through.
struct Scratch {
    float values[4];
};

#define NUMBER(name, extra) EffectValue{#name, 1, [](ParticleEmitter& e) -> float* { return &e.name; }, extra}
#define VECTOR(name, n, extra) EffectValue{#name, n, [](ParticleEmitter& e) -> float* { return reinterpret_cast<float*>(&e.name); }, extra}

const EffectValue kValues[] = {
    NUMBER(rate, false), NUMBER(lifetimeMin, false), NUMBER(lifetimeMax, false), NUMBER(speedMin, false), NUMBER(speedMax, false),
    VECTOR(direction, 3, false), NUMBER(spread, false), NUMBER(spawnRadius, false), VECTOR(gravity, 3, false), NUMBER(drag, false),
    NUMBER(sizeStart, false), NUMBER(sizeEnd, false), VECTOR(colorStart, 4, false), VECTOR(colorEnd, 4, false), NUMBER(windInfluence, false),
    VECTOR(boxSize, 3, true), NUMBER(middleAt, true), VECTOR(colorMiddle, 4, true), NUMBER(sizeMiddle, true), NUMBER(burstEvery, true),
    NUMBER(duration, true), NUMBER(turbulence, true), NUMBER(turbulenceSize, true), NUMBER(floorHeight, true), NUMBER(bounciness, true),
    NUMBER(stretch, true), NUMBER(spinMin, true), NUMBER(spinMax, true),
    NUMBER(simulationSpeed, true), NUMBER(startDelay, true), NUMBER(sizeVariation, true), NUMBER(shadeVariation, true),
    NUMBER(inheritVelocity, true), NUMBER(ratePerMeter, true), VECTOR(drift, 3, true), NUMBER(radial, true), NUMBER(orbital, true),
    NUMBER(buoyancy, true), NUMBER(turbulenceSpeed, true), NUMBER(friction, true), NUMBER(lifetimeLoss, true), NUMBER(sheetFps, true),
    NUMBER(softness, true), NUMBER(lit, true),
};
#undef NUMBER
#undef VECTOR

/// The settings that are not floats: read and written as one number each.
struct EffectFlag {
    const char* key;
    float (*get)(const ParticleEmitter&);
    void (*set)(ParticleEmitter&, float);
    bool isExtra;
};
const EffectFlag kFlags[] = {
    {"isAdditive", [](const ParticleEmitter& e) { return e.isAdditive ? 1.0f : 0.0f; }, [](ParticleEmitter& e, float v) { e.isAdditive = v != 0; }, false},
    {"isEmitting", [](const ParticleEmitter& e) { return e.isEmitting ? 1.0f : 0.0f; }, [](ParticleEmitter& e, float v) { e.isEmitting = v != 0; }, false},
    {"maxParticles", [](const ParticleEmitter& e) { return float(e.maxParticles); }, [](ParticleEmitter& e, float v) { e.maxParticles = std::max(int(v), 1); }, false},
    {"burstAtStart", [](const ParticleEmitter& e) { return float(e.burstAtStart); }, [](ParticleEmitter& e, float v) { e.burstAtStart = std::max(int(v), 0); }, false},
    {"shape", [](const ParticleEmitter& e) { return float(int(e.shape)); }, [](ParticleEmitter& e, float v) { e.shape = ParticleEmitter::Shape(std::clamp(int(v), 0, 5)); }, true},
    {"hasMiddle", [](const ParticleEmitter& e) { return e.hasMiddle ? 1.0f : 0.0f; }, [](ParticleEmitter& e, float v) { e.hasMiddle = v != 0; }, true},
    {"burstCount", [](const ParticleEmitter& e) { return float(e.burstCount); }, [](ParticleEmitter& e, float v) { e.burstCount = std::max(int(v), 0); }, true},
    {"loops", [](const ParticleEmitter& e) { return e.loops ? 1.0f : 0.0f; }, [](ParticleEmitter& e, float v) { e.loops = v != 0; }, true},
    {"bounces", [](const ParticleEmitter& e) { return e.bounces ? 1.0f : 0.0f; }, [](ParticleEmitter& e, float v) { e.bounces = v != 0; }, true},
    {"sheetColumns", [](const ParticleEmitter& e) { return float(e.sheetColumns); }, [](ParticleEmitter& e, float v) { e.sheetColumns = std::max(int(v), 1); }, true},
    {"sheetRows", [](const ParticleEmitter& e) { return float(e.sheetRows); }, [](ParticleEmitter& e, float v) { e.sheetRows = std::max(int(v), 1); }, true},
    {"randomRotation", [](const ParticleEmitter& e) { return e.randomRotation ? 1.0f : 0.0f; }, [](ParticleEmitter& e, float v) { e.randomRotation = v != 0; }, true},
    {"isRing", [](const ParticleEmitter& e) { return e.isRing ? 1.0f : 0.0f; }, [](ParticleEmitter& e, float v) { e.isRing = v != 0; }, true},
    {"collides", [](const ParticleEmitter& e) { return e.collides ? 1.0f : 0.0f; }, [](ParticleEmitter& e, float v) { e.collides = v != 0; }, true},
    {"fromShell", [](const ParticleEmitter& e) { return e.fromShell ? 1.0f : 0.0f; }, [](ParticleEmitter& e, float v) { e.fromShell = v != 0; }, true},
    {"space", [](const ParticleEmitter& e) { return float(int(e.space)); }, [](ParticleEmitter& e, float v) { e.space = ParticleEmitter::Space(std::clamp(int(v), 0, 1)); }, true},
    {"prewarm", [](const ParticleEmitter& e) { return e.prewarm ? 1.0f : 0.0f; }, [](ParticleEmitter& e, float v) { e.prewarm = v != 0; }, true},
    {"turbulenceOctaves", [](const ParticleEmitter& e) { return float(e.turbulenceOctaves); }, [](ParticleEmitter& e, float v) { e.turbulenceOctaves = std::clamp(int(v), 1, 3); }, true},
    {"diesOnHit", [](const ParticleEmitter& e) { return e.diesOnHit ? 1.0f : 0.0f; }, [](ParticleEmitter& e, float v) { e.diesOnHit = v != 0; }, true},
    {"randomFrame", [](const ParticleEmitter& e) { return e.randomFrame ? 1.0f : 0.0f; }, [](ParticleEmitter& e, float v) { e.randomFrame = v != 0; }, true},
    // After `stretch`: a file from before render modes had a streak whenever stretch was above zero.
    {"renderMode", [](const ParticleEmitter& e) { return float(int(e.renderMode)); }, [](ParticleEmitter& e, float v) { e.renderMode = ParticleEmitter::RenderMode(std::clamp(int(v), 0, 3)); }, true},
};

std::string numbersText(const float* values, int count) {
    std::string out;
    for (int i = 0; i < count; i++) {
        char buffer[32];
        for (int digits = 6; digits <= 9; digits++) {
            std::snprintf(buffer, sizeof(buffer), "%.*g", digits, double(values[i]));
            if (std::strtof(buffer, nullptr) == values[i]) break;
        }
        out += " ";
        out += buffer;
    }
    return out;
}

std::vector<std::string> effectLines(const ParticleEmitter& emitter, bool onlyExtras) {
    std::vector<std::string> lines;
    ParticleEmitter usual, copy = emitter;
    for (const EffectValue& value : kValues) {
        if (onlyExtras && !value.isExtra) continue;
        const float* is = value.at(copy);
        const float* was = value.at(usual);
        bool differs = false;
        for (int i = 0; i < value.count; i++) differs = differs || is[i] != was[i];
        if (!onlyExtras || differs) lines.push_back(std::string(value.key) + numbersText(is, value.count));
    }
    for (const EffectFlag& flag : kFlags) {
        if (onlyExtras && !flag.isExtra) continue;
        float is = flag.get(emitter);
        if (!onlyExtras || is != flag.get(usual)) lines.push_back(std::string(flag.key) + numbersText(&is, 1));
    }
    // Its picture: a word, not a number.
    if (!emitter.texture.empty()) lines.push_back("texture " + text::quote(emitter.texture));
    return lines;
}

}  // namespace

bool setEffectText(ParticleEmitter& emitter, const std::string& key, const std::string& value) {
    if (key != "texture") return false;
    emitter.texture = value;
    return true;
}

bool setEffectValue(ParticleEmitter& emitter, const std::string& key, const std::vector<float>& values) {
    for (const EffectValue& value : kValues) {
        if (key != value.key) continue;
        float* at = value.at(emitter);
        for (int i = 0; i < value.count && i < int(values.size()); i++) at[i] = values[size_t(i)];
        if (key == "stretch" && emitter.stretch > 0) emitter.renderMode = ParticleEmitter::RenderMode::Stretched;
        return true;
    }
    for (const EffectFlag& flag : kFlags) {
        if (key != flag.key) continue;
        if (!values.empty()) flag.set(emitter, values[0]);
        return true;
    }
    return false;
}

std::vector<std::string> effectExtras(const ParticleEmitter& emitter) { return effectLines(emitter, true); }

std::string writeEffect(const ParticleEmitter& emitter) {
    std::string out = "molten-effect 1\n";
    for (const std::string& line : effectLines(emitter, false)) out += line + "\n";
    return out;
}

bool readEffect(const std::string& text, ParticleEmitter& emitter) {
    std::istringstream lines(text);
    std::string line;
    if (!std::getline(lines, line) || line.rfind("molten-effect", 0) != 0) return false;
    emitter = ParticleEmitter{};
    while (std::getline(lines, line)) {
        std::istringstream words(line);
        std::string key;
        if (!(words >> key)) continue;
        if (key == "texture") {
            std::vector<std::string> parts = text::splitWords(line);
            setEffectText(emitter, key, parts.size() > 1 ? parts[1] : std::string());
            continue;
        }
        std::vector<float> values;
        float number = 0;
        while (words >> number) values.push_back(number);
        setEffectValue(emitter, key, values);
    }
    return true;
}


namespace {

/// A collider as particles meet it: a box, a sphere or a capsule where it is in the world.
struct Obstacle {
    ColliderShape shape;
    Vec3 center;
    Quat rotation;
    Vec3 halfExtents;
    float radius, halfHeight;
};

std::vector<Obstacle> obstaclesOf(Scene& scene) {
    std::vector<Obstacle> out;
    scene.each<Collider, Transform>([&](Entity, Collider& collider, Transform& transform) {
        if (!transform.isActiveInHierarchy || collider.isTrigger) return;
        if (collider.shape != ColliderShape::Box && collider.shape != ColliderShape::Sphere && collider.shape != ColliderShape::Capsule) return;
        Vec3 position, scale;
        Quat rotation;
        decompose(transform.world, position, rotation, scale);
        out.push_back(Obstacle{collider.shape, position + rotate(rotation, collider.offset), rotation, collider.halfExtents, collider.radius, collider.halfHeight});
    });
    return out;
}

/// Out of anything it went into: put on its surface, its speed into it turned back (by
/// `bounciness`), and along it slowed by the friction. True if it hit something.
bool collide(Particle& particle, const std::vector<Obstacle>& obstacles, float bounciness, float friction) {
    bool hit = false;
    for (const Obstacle& o : obstacles) {
        Vec3 local = rotate(simd::conjugate(o.rotation), particle.position - o.center);
        Vec3 normal{0, 0, 0}, surface = local;
        bool inside = false;
        if (o.shape == ColliderShape::Box) {
            Vec3 h = o.halfExtents;
            if (std::abs(local.x) < h.x && std::abs(local.y) < h.y && std::abs(local.z) < h.z) {
                inside = true;
                // Out by the nearest face.
                Vec3 gap{h.x - std::abs(local.x), h.y - std::abs(local.y), h.z - std::abs(local.z)};
                if (gap.x <= gap.y && gap.x <= gap.z) normal = Vec3{local.x < 0 ? -1.0f : 1.0f, 0, 0}, surface.x = h.x * normal.x;
                else if (gap.y <= gap.z) normal = Vec3{0, local.y < 0 ? -1.0f : 1.0f, 0}, surface.y = h.y * normal.y;
                else normal = Vec3{0, 0, local.z < 0 ? -1.0f : 1.0f}, surface.z = h.z * normal.z;
            }
        } else {
            // A sphere, or a capsule: the nearest point of its line, and a sphere round it.
            Vec3 axisPoint{0, o.shape == ColliderShape::Capsule ? std::clamp(local.y, -o.halfHeight, o.halfHeight) : 0.0f, 0};
            Vec3 away = local - axisPoint;
            float distance = simd::length(away);
            if (distance < o.radius) {
                inside = true;
                normal = distance > 1e-5f ? away / distance : Vec3{0, 1, 0};
                surface = axisPoint + normal * o.radius;
            }
        }
        if (!inside) continue;
        hit = true;
        Vec3 worldNormal = rotate(o.rotation, normal);
        particle.position = o.center + rotate(o.rotation, surface) + worldNormal * 0.002f;
        float into = simd::dot(particle.velocity, worldNormal);
        if (into < 0) {
            Vec3 along = particle.velocity - worldNormal * into;
            particle.velocity = along * (1.0f - std::clamp(friction, 0.0f, 1.0f)) - worldNormal * (into * bounciness);
        }
    }
    return hit;
}

/// A point of the emitter's shape, in its own axes, and the way a particle born there leaves.
void bornAt(ParticleEmitter& emitter, Vec3 axis, Vec3& position, Vec3& direction) {
    Random& random = emitter.random;
    auto onOrIn = [&](Vec3 unit) { return emitter.fromShell ? safeNormalize(unit) : unit; };
    switch (emitter.shape) {
        case ParticleEmitter::Shape::Box: {
            Vec3 inBox{random.range(-0.5f, 0.5f), random.range(-0.5f, 0.5f), random.range(-0.5f, 0.5f)};
            if (emitter.fromShell) {
                // On a face: the axis furthest out is pushed onto it.
                Vec3 a{std::abs(inBox.x), std::abs(inBox.y), std::abs(inBox.z)};
                if (a.x >= a.y && a.x >= a.z) inBox.x = inBox.x < 0 ? -0.5f : 0.5f;
                else if (a.y >= a.z) inBox.y = inBox.y < 0 ? -0.5f : 0.5f;
                else inBox.z = inBox.z < 0 ? -0.5f : 0.5f;
            }
            position = inBox * emitter.boxSize;
            direction = random.inCone(axis, emitter.spread);
            return;
        }
        case ParticleEmitter::Shape::Ring: {
            // On a circle across the direction it emits along.
            Vec3 side = safeNormalize(simd::cross(axis, std::abs(axis.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}));
            Vec3 other = simd::cross(axis, side);
            float angle = random.range(0.0f, 6.2831853f);
            position = (side * std::cos(angle) + other * std::sin(angle)) * emitter.spawnRadius;
            direction = random.inCone(axis, emitter.spread);
            return;
        }
        case ParticleEmitter::Shape::Line: {
            Vec3 side = safeNormalize(simd::cross(axis, std::abs(axis.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}));
            position = side * (random.range(-0.5f, 0.5f) * emitter.boxSize.x);
            direction = random.inCone(axis, emitter.spread);
            return;
        }
        case ParticleEmitter::Shape::Sphere:
        case ParticleEmitter::Shape::Hemisphere: {
            Vec3 unit = random.insideUnitSphere();
            if (simd::length(unit) < 1e-4f) unit = axis * 0.01f;
            if (emitter.shape == ParticleEmitter::Shape::Hemisphere && simd::dot(unit, axis) < 0) unit -= axis * (2 * simd::dot(unit, axis));
            position = onOrIn(unit) * emitter.spawnRadius;
            // Outward from the middle; the spread scatters them a little round that.
            direction = emitter.spread > 0 ? random.inCone(safeNormalize(unit), emitter.spread * 0.25f) : safeNormalize(unit);
            return;
        }
        case ParticleEmitter::Shape::Cone:
        default:
            position = onOrIn(random.insideUnitSphere()) * emitter.spawnRadius;
            direction = random.inCone(axis, emitter.spread);
            return;
    }
}

/// One emitter, one step of `dt` seconds. `world` is its entity's matrix now; everything here is
/// in world space unless the system lives in its entity's.
void stepEmitter(ParticleEmitter& emitter, const Mat4& world, float dt, const std::vector<Obstacle>& obstacles, Vec3 wind, float gustiness,
                 double time) {
    const bool isLocal = emitter.space == ParticleEmitter::Space::Local;
    Vec3 origin = xyz(world.columns[3]);
    Vec3 position, scale;
    Quat rotation;
    decompose(world, position, rotation, scale);
    Quat toLocal = simd::conjugate(rotation);
    // Forces are said in the world; a system that lives in the entity's axes feels them turned.
    auto inSpace = [&](Vec3 v) { return isLocal ? rotate(toLocal, v) : v; };
    const Vec3 gravity = inSpace(emitter.gravity);
    const Vec3 up = inSpace(Vec3{0, 1, 0});
    const Vec3 axis = safeNormalize(emitter.direction);                     // in the entity's axes
    const Vec3 orbitAxis = isLocal ? axis : safeNormalize(rotate(rotation, axis));
    const Vec3 center = isLocal ? Vec3{0, 0, 0} : origin;

    emitter.age += dt;
    bool isOver = emitter.duration > 0 && emitter.age >= emitter.startDelay + emitter.duration;
    if (isOver && emitter.loops && emitter.particles.empty() && emitter.pendingBurst == 0) {
        emitter.age = emitter.startDelay;
        emitter.burstTimer = 0;
        emitter.pendingBurst += emitter.burstAtStart;
        isOver = false;
    }
    const bool isWaiting = emitter.age < emitter.startDelay;
    if (emitter.burstEvery > 0 && emitter.isEmitting && !isOver && !isWaiting) {
        emitter.burstTimer += dt;
        while (emitter.burstTimer >= emitter.burstEvery) {
            emitter.burstTimer -= emitter.burstEvery;
            emitter.pendingBurst += emitter.burstCount;
        }
    }

    // ---- the living: aged, pushed, moved; the dead dropped by swapping with the last.
    for (size_t i = 0; i < emitter.particles.size();) {
        Particle& particle = emitter.particles[i];
        particle.age += dt;
        if (particle.age >= particle.lifetime) {
            particle = emitter.particles.back();
            emitter.particles.pop_back();
            continue;
        }
        float t = particle.age / particle.lifetime;
        particle.velocity += gravity * dt;
        // Hot gas rises, and less as it cools.
        if (emitter.buoyancy != 0) particle.velocity += up * (emitter.buoyancy * (1.0f - t) * (1.0f - t) * dt);
        Vec3 worldPosition = isLocal ? transformPoint(world, particle.position) : particle.position;
        if (emitter.windInfluence > 0) {
            // The air drags a particle toward its own speed: light things (smoke) follow
            // it almost at once, heavy ones (embers) only lean.
            Vec3 air = inSpace(windAt(wind, gustiness, worldPosition, time));
            particle.velocity += (air - particle.velocity) * std::min(emitter.windInfluence * 2.5f * dt, 1.0f);
        }
        if (emitter.turbulence > 0) {
            Vec3 swirl = swirlAt(worldPosition, emitter.turbulenceSize, time * double(emitter.turbulenceSpeed), emitter.turbulenceOctaves);
            particle.velocity += inSpace(swirl) * (emitter.turbulence * 4.0f * dt);
        }
        particle.velocity *= 1.0f / (1.0f + emitter.drag * dt);
        // What moves it without being a speed it keeps: the drift, outward, round the axis.
        Vec3 extra = emitter.drift;
        if (emitter.radial != 0) extra += safeNormalize(particle.position - center) * emitter.radial;
        if (emitter.orbital != 0) extra += simd::cross(orbitAxis, particle.position - center) * emitter.orbital;
        particle.position += (particle.velocity + extra) * dt;
        particle.rotation += particle.spin * dt;
        bool hit = false;
        if (emitter.collides && !obstacles.empty() && !isLocal) hit |= collide(particle, obstacles, emitter.bounciness, emitter.friction);
        if (emitter.bounces && !isLocal && particle.position.y < emitter.floorHeight && particle.velocity.y < 0) {
            // It lands: back up with a share of its speed, and slower along the floor.
            particle.position.y = emitter.floorHeight;
            float keep = 1.0f - std::clamp(emitter.friction, 0.0f, 1.0f);
            particle.velocity = Vec3{particle.velocity.x * keep, -particle.velocity.y * emitter.bounciness, particle.velocity.z * keep};
            hit = true;
        }
        if (hit && (emitter.diesOnHit || emitter.lifetimeLoss > 0)) {
            particle.age += emitter.diesOnHit ? particle.lifetime : particle.lifetime * emitter.lifetimeLoss;
            if (particle.age >= particle.lifetime) {
                particle = emitter.particles.back();
                emitter.particles.pop_back();
                continue;
            }
        }
        i++;
    }

    // ---- the newborn. The accumulators carry fractions of a particle from step to step.
    Vec3 moved = emitter.hasLastOrigin ? origin - emitter.lastOrigin : Vec3{0, 0, 0};
    float movedBy = simd::length(moved);
    bool jumped = movedBy > 25.0f;                  // put somewhere else, not moved there: nothing along the way
    if (jumped) moved = Vec3{0, 0, 0}, movedBy = 0;
    Vec3 emitterVelocity = dt > 0 ? moved / dt : Vec3{0, 0, 0};
    int toSpawn = emitter.pendingBurst;
    emitter.pendingBurst = 0;
    if (emitter.isEmitting && !isOver && !isWaiting) {
        emitter.spawnAccumulator += emitter.rate * dt;
        int whole = int(emitter.spawnAccumulator);
        emitter.spawnAccumulator -= float(whole);
        toSpawn += whole;
        if (emitter.ratePerMeter > 0) {
            emitter.distanceAccumulator += movedBy * emitter.ratePerMeter;
            int trail = int(emitter.distanceAccumulator);
            emitter.distanceAccumulator -= float(trail);
            toSpawn += trail;
        }
    }
    for (int i = 0; i < toSpawn && int(emitter.particles.size()) < emitter.maxParticles; i++) {
        Particle particle;
        Vec3 offset, direction;
        bornAt(emitter, axis, offset, direction);
        float speed = emitter.random.range(emitter.speedMin, emitter.speedMax);
        // Spread along the way the emitter came this step, each as old as it would be had it
        // been born there: a moving emitter leaves a line, not a clump every frame.
        float along = toSpawn > 1 ? (float(i) + emitter.random.range(0.0f, 1.0f)) / float(toSpawn) : 1.0f;
        float early = isLocal || !emitter.hasLastOrigin ? 0.0f : (1.0f - along) * dt;
        if (isLocal) {
            particle.position = offset;
            particle.velocity = direction * speed;
        } else {
            Vec3 base = emitter.hasLastOrigin ? emitter.lastOrigin + moved * along : origin;
            particle.position = base + transformDirection(world, offset);
            particle.velocity = safeNormalize(transformDirection(world, direction)) * speed + emitterVelocity * emitter.inheritVelocity;
        }
        particle.lifetime = emitter.random.range(emitter.lifetimeMin, emitter.lifetimeMax);
        particle.age = early;
        particle.position += particle.velocity * early;
        if (emitter.randomRotation) particle.rotation = emitter.random.range(0.0f, 6.2831853f);
        if (emitter.spinMax > 0 || emitter.spinMin > 0) {
            particle.spin = emitter.random.range(emitter.spinMin, emitter.spinMax) * (emitter.random.range(0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f);
        }
        if (emitter.sizeVariation > 0) particle.sizeScale = 1.0f + emitter.random.range(-1.0f, 1.0f) * std::clamp(emitter.sizeVariation, 0.0f, 1.0f);
        if (emitter.shadeVariation > 0) particle.shade = 1.0f + emitter.random.range(-1.0f, 1.0f) * std::clamp(emitter.shadeVariation, 0.0f, 1.0f);
        if (emitter.randomFrame) particle.frame = std::floor(emitter.random.range(0.0f, float(std::max(emitter.sheetColumns * emitter.sheetRows, 1))));
        emitter.particles.push_back(particle);
    }
    emitter.lastOrigin = origin;
    emitter.hasLastOrigin = true;
}

}  // namespace

void updateParticles(Scene& scene, float dt, Vec3 wind, float gustiness, double time) {
    // What particles bounce off: read once, only if some emitter asks.
    std::vector<Obstacle> obstacles;
    bool wantsObstacles = false;
    scene.each<ParticleEmitter>([&](Entity, ParticleEmitter& emitter) { wantsObstacles |= emitter.collides; });
    if (wantsObstacles) obstacles = obstaclesOf(scene);
    scene.each<ParticleEmitter, Transform>([&](Entity entity, ParticleEmitter& emitter, Transform& transform) {
        if (!transform.isActiveInHierarchy) return;
        if (!emitter.isSimulated) {
            emitter.particles.clear();
            emitter.spawnAccumulator = 0;
            emitter.hasLastOrigin = false;
            return;
        }
        float step = dt * std::max(emitter.simulationSpeed, 0.0f);
        if (!emitter.isSeeded) {
            emitter.random = Random(uint64_t(entity.index) * 7919u + 17u);     // a different pattern per emitter
            emitter.isSeeded = true;
            emitter.pendingBurst += emitter.burstAtStart;
            emitter.age = 0;
            emitter.burstTimer = 0;
            emitter.hasLastOrigin = false;
            if (emitter.prewarm) {
                // As if it had been running for as long as its particles live: already full.
                emitter.hasLastOrigin = true;
                emitter.lastOrigin = transform.worldPosition();
                float span = std::max(emitter.lifetimeMax, 0.1f) + emitter.startDelay;
                int steps = std::min(int(span * 30.0f) + 1, 600);
                for (int i = 0; i < steps; i++) stepEmitter(emitter, transform.world, span / float(steps), obstacles, wind, gustiness, time);
            }
        }
        stepEmitter(emitter, transform.world, step, obstacles, wind, gustiness, time);
    });
}

}  // namespace molten
