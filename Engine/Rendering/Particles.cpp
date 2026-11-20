#include "Rendering/Particles.hpp"

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

/// A direction that changes smoothly from place to place and slowly in time: the swirl of air.
Vec3 swirlAt(Vec3 p, float size, double time) {
    Vec3 q = p / std::max(size, 0.01f);
    float t = float(time) * 0.35f;
    return Vec3{std::sin(q.y * 1.7f + t * 1.3f) + std::cos(q.z * 1.3f - t), std::sin(q.z * 1.9f - t * 0.9f) + std::cos(q.x * 1.1f + t * 1.1f),
                std::sin(q.x * 1.5f + t) + std::cos(q.y * 1.2f - t * 1.2f)} * 0.5f;
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
    {"shape", [](const ParticleEmitter& e) { return float(int(e.shape)); }, [](ParticleEmitter& e, float v) { e.shape = ParticleEmitter::Shape(std::clamp(int(v), 0, 2)); }, true},
    {"hasMiddle", [](const ParticleEmitter& e) { return e.hasMiddle ? 1.0f : 0.0f; }, [](ParticleEmitter& e, float v) { e.hasMiddle = v != 0; }, true},
    {"burstCount", [](const ParticleEmitter& e) { return float(e.burstCount); }, [](ParticleEmitter& e, float v) { e.burstCount = std::max(int(v), 0); }, true},
    {"loops", [](const ParticleEmitter& e) { return e.loops ? 1.0f : 0.0f; }, [](ParticleEmitter& e, float v) { e.loops = v != 0; }, true},
    {"bounces", [](const ParticleEmitter& e) { return e.bounces ? 1.0f : 0.0f; }, [](ParticleEmitter& e, float v) { e.bounces = v != 0; }, true},
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
    return lines;
}

}  // namespace

bool setEffectValue(ParticleEmitter& emitter, const std::string& key, const std::vector<float>& values) {
    for (const EffectValue& value : kValues) {
        if (key != value.key) continue;
        float* at = value.at(emitter);
        for (int i = 0; i < value.count && i < int(values.size()); i++) at[i] = values[size_t(i)];
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
        std::vector<float> values;
        float number = 0;
        while (words >> number) values.push_back(number);
        setEffectValue(emitter, key, values);
    }
    return true;
}

const std::vector<std::string>& effectPresets() {
    static const std::vector<std::string> names = {"Fire", "Smoke", "Sparks", "Explosion", "Dust", "Rain", "Snow", "Magic", "Fountain"};
    return names;
}

ParticleEmitter effectPreset(const std::string& name) {
    ParticleEmitter e;
    if (name == "Fire") {
        e.rate = 70, e.lifetimeMin = 0.5f, e.lifetimeMax = 1.1f, e.speedMin = 0.8f, e.speedMax = 1.8f, e.spread = radians(14.0f);
        e.spawnRadius = 0.18f, e.gravity = Vec3{0, 1.2f, 0}, e.drag = 0.8f, e.turbulence = 0.7f, e.turbulenceSize = 0.5f;
        e.hasMiddle = true, e.middleAt = 0.35f;
        e.colorStart = Vec4{4.0f, 2.6f, 0.7f, 1}, e.colorMiddle = Vec4{3.0f, 0.9f, 0.12f, 0.9f}, e.colorEnd = Vec4{0.4f, 0.08f, 0.02f, 0};
        e.sizeStart = 0.16f, e.sizeMiddle = 0.34f, e.sizeEnd = 0.08f, e.maxParticles = 400;
    } else if (name == "Smoke") {
        e.rate = 14, e.lifetimeMin = 2.5f, e.lifetimeMax = 4.5f, e.speedMin = 0.5f, e.speedMax = 1.0f, e.spread = radians(12.0f);
        e.spawnRadius = 0.15f, e.gravity = Vec3{0, 0.35f, 0}, e.drag = 0.5f, e.turbulence = 0.5f, e.turbulenceSize = 1.2f, e.windInfluence = 0.8f;
        e.isAdditive = false, e.hasMiddle = true, e.middleAt = 0.2f;
        e.colorStart = Vec4{0.35f, 0.35f, 0.36f, 0}, e.colorMiddle = Vec4{0.3f, 0.3f, 0.31f, 0.55f}, e.colorEnd = Vec4{0.5f, 0.5f, 0.5f, 0};
        e.sizeStart = 0.25f, e.sizeMiddle = 0.8f, e.sizeEnd = 2.2f, e.maxParticles = 200;
    } else if (name == "Sparks") {
        e.rate = 60, e.lifetimeMin = 0.4f, e.lifetimeMax = 1.0f, e.speedMin = 3.0f, e.speedMax = 7.0f, e.spread = radians(40.0f);
        e.gravity = Vec3{0, -9.8f, 0}, e.drag = 0.3f, e.bounces = true, e.bounciness = 0.35f;
        e.colorStart = Vec4{5.0f, 3.2f, 1.0f, 1}, e.colorEnd = Vec4{2.0f, 0.4f, 0.05f, 0}, e.sizeStart = 0.035f, e.sizeEnd = 0.01f;
    } else if (name == "Explosion") {
        e.rate = 0, e.burstAtStart = 220, e.lifetimeMin = 0.5f, e.lifetimeMax = 1.4f, e.speedMin = 2.0f, e.speedMax = 11.0f, e.spread = radians(180.0f);
        e.spawnRadius = 0.2f, e.gravity = Vec3{0, -1.5f, 0}, e.drag = 2.6f, e.turbulence = 1.0f, e.duration = 0.1f;
        e.hasMiddle = true, e.middleAt = 0.25f;
        e.colorStart = Vec4{6.0f, 4.5f, 2.0f, 1}, e.colorMiddle = Vec4{3.0f, 0.8f, 0.1f, 0.9f}, e.colorEnd = Vec4{0.15f, 0.1f, 0.1f, 0};
        e.sizeStart = 0.25f, e.sizeMiddle = 0.7f, e.sizeEnd = 1.1f, e.maxParticles = 400;
    } else if (name == "Dust") {
        e.rate = 0, e.burstAtStart = 40, e.lifetimeMin = 0.7f, e.lifetimeMax = 1.6f, e.speedMin = 0.5f, e.speedMax = 2.2f, e.spread = radians(75.0f);
        e.spawnRadius = 0.25f, e.gravity = Vec3{0, 0.2f, 0}, e.drag = 2.4f, e.windInfluence = 0.5f, e.isAdditive = false;
        e.colorStart = Vec4{0.55f, 0.48f, 0.38f, 0.5f}, e.colorEnd = Vec4{0.6f, 0.55f, 0.45f, 0}, e.sizeStart = 0.2f, e.sizeEnd = 0.9f;
    } else if (name == "Rain") {
        e.rate = 500, e.lifetimeMin = 1.0f, e.lifetimeMax = 1.3f, e.speedMin = 14.0f, e.speedMax = 17.0f, e.direction = Vec3{0, -1, 0}, e.spread = radians(2.0f);
        e.shape = ParticleEmitter::Shape::Box, e.boxSize = Vec3{24, 0.5f, 24}, e.gravity = Vec3{0, -4, 0}, e.drag = 0, e.windInfluence = 0.3f;
        e.isAdditive = false, e.colorStart = Vec4{0.7f, 0.78f, 0.9f, 0.5f}, e.colorEnd = Vec4{0.7f, 0.78f, 0.9f, 0.3f};
        e.sizeStart = 0.035f, e.sizeEnd = 0.03f, e.maxParticles = 1200;
    } else if (name == "Snow") {
        e.rate = 160, e.lifetimeMin = 5.0f, e.lifetimeMax = 8.0f, e.speedMin = 0.6f, e.speedMax = 1.2f, e.direction = Vec3{0, -1, 0}, e.spread = radians(10.0f);
        e.shape = ParticleEmitter::Shape::Box, e.boxSize = Vec3{24, 0.5f, 24}, e.gravity = Vec3{0, -0.2f, 0}, e.drag = 0.6f;
        e.turbulence = 0.6f, e.turbulenceSize = 2.0f, e.windInfluence = 0.7f, e.isAdditive = false;
        e.colorStart = Vec4{1, 1, 1, 0.9f}, e.colorEnd = Vec4{1, 1, 1, 0.6f}, e.sizeStart = 0.05f, e.sizeEnd = 0.05f, e.maxParticles = 1500;
    } else if (name == "Magic") {
        e.rate = 45, e.lifetimeMin = 1.0f, e.lifetimeMax = 2.0f, e.speedMin = 0.2f, e.speedMax = 0.7f, e.spread = radians(180.0f);
        e.shape = ParticleEmitter::Shape::Ring, e.spawnRadius = 0.7f, e.gravity = Vec3{0, 0.5f, 0}, e.drag = 0.4f, e.turbulence = 0.8f, e.turbulenceSize = 0.6f;
        e.hasMiddle = true, e.middleAt = 0.5f;
        e.colorStart = Vec4{0.6f, 1.2f, 4.0f, 0}, e.colorMiddle = Vec4{2.2f, 0.8f, 4.0f, 1}, e.colorEnd = Vec4{4.0f, 0.5f, 2.0f, 0};
        e.sizeStart = 0.02f, e.sizeMiddle = 0.07f, e.sizeEnd = 0.0f;
    } else if (name == "Fountain") {
        e.rate = 220, e.lifetimeMin = 1.6f, e.lifetimeMax = 2.2f, e.speedMin = 6.0f, e.speedMax = 7.5f, e.spread = radians(9.0f);
        e.gravity = Vec3{0, -9.8f, 0}, e.drag = 0.1f, e.bounces = true, e.bounciness = 0.2f, e.isAdditive = false;
        e.colorStart = Vec4{0.75f, 0.88f, 1.0f, 0.7f}, e.colorEnd = Vec4{0.9f, 0.95f, 1.0f, 0}, e.sizeStart = 0.06f, e.sizeEnd = 0.12f, e.maxParticles = 800;
    }
    return e;
}

void updateParticles(Scene& scene, float dt, Vec3 wind, float gustiness, double time) {
    scene.each<ParticleEmitter, Transform>([&](Entity entity, ParticleEmitter& emitter, Transform& transform) {
        if (!transform.isActiveInHierarchy) return;
        if (!emitter.isSimulated) {
            emitter.particles.clear();
            emitter.spawnAccumulator = 0;
            return;
        }
        if (!emitter.isSeeded) {
            emitter.random = Random(uint64_t(entity.index) * 7919u + 17u);     // a different pattern per emitter
            emitter.isSeeded = true;
            emitter.pendingBurst += emitter.burstAtStart;
            emitter.age = 0;
            emitter.burstTimer = 0;
        }
        emitter.age += dt;
        // One that ends has stopped emitting; one that loops starts over once what it made is gone.
        bool isOver = emitter.duration > 0 && emitter.age >= emitter.duration;
        if (isOver && emitter.loops && emitter.particles.empty() && emitter.pendingBurst == 0) {
            emitter.age = 0;
            emitter.burstTimer = 0;
            emitter.pendingBurst += emitter.burstAtStart;
            isOver = false;
        }
        if (emitter.burstEvery > 0 && emitter.isEmitting && !isOver) {
            emitter.burstTimer += dt;
            while (emitter.burstTimer >= emitter.burstEvery) {
                emitter.burstTimer -= emitter.burstEvery;
                emitter.pendingBurst += emitter.burstCount;
            }
        }

        // Age and move the living particles; drop the dead ones by swapping with the last.
        for (size_t i = 0; i < emitter.particles.size();) {
            Particle& particle = emitter.particles[i];
            particle.age += dt;
            if (particle.age >= particle.lifetime) {
                particle = emitter.particles.back();
                emitter.particles.pop_back();
                continue;
            }
            particle.velocity += emitter.gravity * dt;
            if (emitter.windInfluence > 0) {
                // The air drags a particle toward its own speed: light things (smoke) follow
                // it almost at once, heavy ones (embers) only lean.
                Vec3 air = windAt(wind, gustiness, particle.position, time);
                particle.velocity += (air - particle.velocity) * std::min(emitter.windInfluence * 2.5f * dt, 1.0f);
            }
            if (emitter.turbulence > 0) particle.velocity += swirlAt(particle.position, emitter.turbulenceSize, time) * (emitter.turbulence * 4.0f * dt);
            particle.velocity *= 1.0f / (1.0f + emitter.drag * dt);
            particle.position += particle.velocity * dt;
            if (emitter.bounces && particle.position.y < emitter.floorHeight && particle.velocity.y < 0) {
                // It lands: back up with a share of its speed, and a little slower along the floor.
                particle.position.y = emitter.floorHeight;
                particle.velocity = Vec3{particle.velocity.x * 0.8f, -particle.velocity.y * emitter.bounciness, particle.velocity.z * 0.8f};
            }
            i++;
        }

        // Spawn. The accumulator carries fractions of a particle from frame to frame.
        int toSpawn = emitter.pendingBurst;
        emitter.pendingBurst = 0;
        if (emitter.isEmitting && emitter.rate > 0 && !isOver) {
            emitter.spawnAccumulator += emitter.rate * dt;
            int whole = int(emitter.spawnAccumulator);
            emitter.spawnAccumulator -= float(whole);
            toSpawn += whole;
        }

        Vec3 origin = transform.worldPosition();
        Vec3 axis = safeNormalize(transformDirection(transform.world, emitter.direction));
        for (int i = 0; i < toSpawn && int(emitter.particles.size()) < emitter.maxParticles; i++) {
            Particle particle;
            if (emitter.shape == ParticleEmitter::Shape::Box) {
                Vec3 inBox{emitter.random.range(-0.5f, 0.5f) * emitter.boxSize.x, emitter.random.range(-0.5f, 0.5f) * emitter.boxSize.y,
                           emitter.random.range(-0.5f, 0.5f) * emitter.boxSize.z};
                particle.position = origin + transformDirection(transform.world, inBox);
            } else if (emitter.shape == ParticleEmitter::Shape::Ring) {
                // On a circle across the direction it emits along.
                Vec3 side = safeNormalize(simd::cross(axis, std::abs(axis.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}));
                Vec3 other = simd::cross(axis, side);
                float angle = emitter.random.range(0.0f, 6.2831853f);
                particle.position = origin + (side * std::cos(angle) + other * std::sin(angle)) * emitter.spawnRadius;
            } else {
                particle.position = origin + emitter.random.insideUnitSphere() * emitter.spawnRadius;
            }
            Vec3 direction = emitter.random.inCone(axis, emitter.spread);
            particle.velocity = direction * emitter.random.range(emitter.speedMin, emitter.speedMax);
            particle.lifetime = emitter.random.range(emitter.lifetimeMin, emitter.lifetimeMax);
            emitter.particles.push_back(particle);
        }
    });
}

}  // namespace molten
