#include "Kit/Weather.hpp"

#include "Core/World.hpp"
#include "Physics/Physics.hpp"
#include "Rendering/Particles.hpp"
#include "Rendering/Renderer.hpp"
#include "Scene/Components.hpp"
#include "Scene/SceneFile.hpp"
#include "Water/Waves.hpp"

#include <algorithm>
#include <cmath>

namespace molten::kit {

namespace {

constexpr float kRainRadius = 28.0f;            // meters round each viewer that it rains in
constexpr float kRainAbove = 22.0f;             // born this high above the viewer
constexpr float kDropSpeed = 9.0f;              // meters a second: a raindrop falls this fast
constexpr float kDropsPerSecond = 5200.0f;      // round each viewer, in a downpour
constexpr float kSplashReach = 18.0f;           // splashes and rings only this near a viewer: farther, they are less than a pixel
constexpr float kSnowRadius = 22.0f;            // snow falls slowly: a smaller patch round the viewer is enough
constexpr float kSnowAbove = 10.0f;
constexpr float kFlakeSpeed = 1.5f;             // meters a second: a snowflake drifts down this fast
constexpr float kFlakesPerSecond = 900.0f;      // round each viewer, in a heavy fall
constexpr float kSteep = 0.6f;                  // a surface whose normal points up less than this is a wall: drops run down it

// The particle systems, made the first time it rains or snows: the world's, not the scene's.
Entity makeSystem(Scene& scene, const char* name, const ParticleEmitter& emitter) {
    Entity entity = scene.create(name);
    scene.add(entity, Transform{});
    scene.add(entity, emitter);
    scene.add(entity, EditorOnly{});
    return entity;
}

ParticleEmitter rainDrops() {
    // Streaks: each drop drawn a few hundredths of a second of its fall long, faint, lit by
    // the sky and the sun (dark at night). They are born and aimed here, not by the emitter.
    ParticleEmitter e;
    e.rate = 0, e.isEmitting = false;
    e.gravity = Vec3{0, 0, 0}, e.drag = 0;
    e.sizeStart = e.sizeEnd = 0.011f;
    e.colorStart = e.colorEnd = Vec4{0.72f, 0.76f, 0.82f, 0.32f};
    e.isAdditive = false;
    e.renderMode = ParticleEmitter::RenderMode::Stretched;
    e.stretch = 0.035f;
    e.lit = 0.7f;
    e.maxParticles = 30000;
    return e;
}

ParticleEmitter splashDrops() {
    // What a drop throws up where it lands: droplets that fly a little and fall back.
    ParticleEmitter e;
    e.rate = 0, e.isEmitting = false;
    e.gravity = Vec3{0, -9.8f, 0}, e.drag = 0.6f;
    e.sizeStart = 0.018f, e.sizeEnd = 0.004f;
    e.colorStart = Vec4{0.8f, 0.84f, 0.9f, 0.55f};
    e.colorEnd = Vec4{0.8f, 0.84f, 0.9f, 0.0f};
    e.isAdditive = false;
    e.lit = 0.7f;
    e.maxParticles = 8000;
    return e;
}

ParticleEmitter runningDrops() {
    // On a wall, a windscreen, the side of a car: a drop that runs down it a little way, thin and
    // drawn out along its way, slowing as it goes.
    ParticleEmitter e;
    e.rate = 0, e.isEmitting = false;
    e.gravity = Vec3{0, 0, 0}, e.drag = 1.2f;
    e.sizeStart = 0.007f, e.sizeEnd = 0.005f;
    e.colorStart = Vec4{0.8f, 0.85f, 0.9f, 0.45f};
    e.colorEnd = Vec4{0.8f, 0.85f, 0.9f, 0.0f};
    e.isAdditive = false;
    e.renderMode = ParticleEmitter::RenderMode::Stretched;
    e.stretch = 0.12f;
    e.lit = 0.6f;
    e.maxParticles = 4000;
    return e;
}

ParticleEmitter waterRings() {
    // Where a drop lands on water: a ring lying on it that widens and fades.
    ParticleEmitter e;
    e.rate = 0, e.isEmitting = false;
    e.gravity = Vec3{0, 0, 0}, e.drag = 0;
    e.sizeStart = 0.03f, e.sizeEnd = 0.34f;
    e.colorStart = Vec4{0.85f, 0.9f, 0.95f, 0.45f};
    e.colorEnd = Vec4{0.85f, 0.9f, 0.95f, 0.0f};
    e.isAdditive = false;
    e.renderMode = ParticleEmitter::RenderMode::Horizontal;
    e.isRing = true;
    e.lit = 0.6f;
    e.maxParticles = 8000;
    return e;
}

ParticleEmitter snowFlakes() {
    // Soft white dots that drift down slowly, swaying in the air and carried by the wind. Born
    // and aimed here, as the drops are; the emitter only moves them.
    ParticleEmitter e;
    e.rate = 0, e.isEmitting = false;
    e.gravity = Vec3{0, -2.4f, 0}, e.drag = 1.6f;              // as fast as they fall: a meter and a half a second
    e.windInfluence = 0.5f;
    e.turbulence = 0.35f, e.turbulenceSize = 1.6f, e.turbulenceSpeed = 0.6f;
    e.sizeStart = e.sizeEnd = 0.03f;
    e.colorStart = e.colorEnd = Vec4{0.95f, 0.96f, 1.0f, 0.85f};
    e.isAdditive = false;
    e.lit = 0.8f;
    e.maxParticles = 16000;
    return e;
}

ParticleEmitter settledFlakes() {
    // A flake that has landed lies there a moment, and melts away.
    ParticleEmitter e;
    e.rate = 0, e.isEmitting = false;
    e.gravity = Vec3{0, 0, 0}, e.drag = 0;
    e.sizeStart = 0.03f, e.sizeEnd = 0.012f;
    e.colorStart = Vec4{0.95f, 0.96f, 1.0f, 0.8f};
    e.colorEnd = Vec4{0.95f, 0.96f, 1.0f, 0.0f};
    e.isAdditive = false;
    e.lit = 0.8f;
    e.maxParticles = 6000;
    return e;
}

// Where it rains: round each view's camera (as the engine said), or round the scene's camera.
std::vector<Vec3> viewersOf(World& world) {
    if (!world.weatherNow.viewers.empty()) return world.weatherNow.viewers;
    std::vector<Vec3> found;
    world.scene.each<Camera, Transform>([&](Entity entity, Camera& camera, Transform& transform) {
        if (found.empty() && camera.isActive && transform.isActiveInHierarchy && !world.scene.has<EditorOnly>(entity)) found.push_back(transform.worldPosition());
    });
    return found;
}

ParticleEmitter* emitterOf(Scene& scene, Entity entity) {
    return entity.isValid() && scene.isAlive(entity) ? scene.get<ParticleEmitter>(entity) : nullptr;
}

// A direction in one number, kept where a drop has nothing else to keep it (the turn of its
// picture, which a streak does not have): 12 bits for each of the two of its folded octahedron.
float packNormal(Vec3 n) {
    float sum = std::abs(n.x) + std::abs(n.y) + std::abs(n.z);
    if (sum < 1e-6f) return packNormal(Vec3{0, 1, 0});
    float u = n.x / sum, v = n.z / sum;
    if (n.y < 0) {
        float fu = (1.0f - std::abs(v)) * (u >= 0 ? 1.0f : -1.0f), fv = (1.0f - std::abs(u)) * (v >= 0 ? 1.0f : -1.0f);
        u = fu, v = fv;
    }
    float qu = std::round((u * 0.5f + 0.5f) * 4095.0f), qv = std::round((v * 0.5f + 0.5f) * 4095.0f);
    return qu * 4096.0f + qv;
}

Vec3 unpackNormal(float packed) {
    float qu = std::floor(packed / 4096.0f), qv = packed - qu * 4096.0f;
    float u = qu / 4095.0f * 2.0f - 1.0f, v = qv / 4095.0f * 2.0f - 1.0f;
    float y = 1.0f - std::abs(u) - std::abs(v);
    if (y < 0) {
        float fu = (1.0f - std::abs(v)) * (u >= 0 ? 1.0f : -1.0f), fv = (1.0f - std::abs(u)) * (v >= 0 ? 1.0f : -1.0f);
        u = fu, v = fv;
    }
    Vec3 n{u, y, v};
    float length = simd::length(n);
    return length > 1e-6f ? n / length : Vec3{0, 1, 0};
}

// What a drop born at `from`, falling along `velocity`, lands on: the first collider in its way
// (a roof, a car, the ground's mesh), or the water above it, if that is nearer. False if it
// lands on nothing within its fall (it falls out of sight). `normal` is the way what it lands on faces.
bool landingOf(World& world, Vec3 from, Vec3 velocity, float fall, Vec3& at, Vec3& normal, bool& onWater) {
    Vec3 direction = simd::normalize(velocity);
    float reach = fall / std::max(-direction.y, 0.2f);
    std::optional<RaycastHit> hit = world.physics.raycast(world.scene, Ray{from, direction}, reach);
    float distance = hit ? hit->distance : reach;
    onWater = false;
    normal = hit ? hit->normal : Vec3{0, 1, 0};
    // Water lies under the drop's line somewhere: found by its height where the drop would end.
    Vec3 end = from + direction * distance;
    float surface = 0;
    if (waterHeightAt(world, end, surface) && surface > end.y - 0.05f) {
        float down = (from.y - surface) / std::max(-direction.y, 0.2f);
        if (down > 0 && down <= distance + 0.05f) {
            distance = down;
            onWater = true;
            normal = Vec3{0, 1, 0};
        }
    }
    if (!hit && !onWater) return false;
    at = from + direction * distance;
    return true;
}

bool isNearAViewer(const std::vector<Vec3>& viewers, Vec3 at) {
    for (Vec3 viewer : viewers) {
        if (simd::length_squared(at - viewer) < kSplashReach * kSplashReach) return true;
    }
    return false;
}

// ---- lightning: now and then a stroke of one to four flashes in quick succession, somewhere
// within a few kilometers. Most stay in the clouds and light them from inside; the others are a
// bolt down to the ground, whose shape the sky draws from the seed chosen here.
void strike(World& world, Random& random, const std::vector<Vec3>& viewers, float storm) {
    World::WeatherNow& now = world.weatherNow;
    Vec3 around = viewers.empty() ? Vec3{0, 0, 0} : viewers.front();
    float angle = random.range(0.0f, 2.0f * kPi);
    float away = random.range(500.0f, 900.0f) + 3800.0f * random.next() * random.next();     // more often near than far
    Vec3 at{around.x + std::cos(angle) * away, 0.0f, around.z + std::sin(angle) * away};
    // The ground it would strike: what is there (a hill, a tower), or the water, or the level of the ground.
    float ground = 0;
    if (std::optional<RaycastHit> hit = world.physics.raycast(world.scene, Ray{Vec3{at.x, 5000.0f, at.z}, Vec3{0, -1, 0}}, 10000.0f)) {
        ground = hit->point.y;
    } else if (float surface = 0; waterHeightAt(world, at, surface)) {
        ground = surface;
    }
    at.y = ground;
    now.strikePlace = at;
    // A bolt to the ground: a third of the strokes of a light storm, more than half in a full one.
    now.bolt = random.next() < 0.3f + 0.3f * storm ? std::floor(random.range(1.0f, 60000.0f)) : 0.0f;
}

}  // namespace

void updateWeather(World& world, float dt) {
    const WeatherSettings& weather = world.weather();
    World::WeatherNow& now = world.weatherNow;
    Scene& scene = world.scene;
    Random random(now.seed);
    now.seed = random.nextUInt() | (uint64_t(random.nextUInt()) << 32);
    const double time = world.effectsTime;

    // ---- lightning
    const float storm = std::clamp(weather.storm, 0.0f, 1.0f);
    now.flash = std::max(now.flash - dt * 9.0f, 0.0f);
    if (storm <= 0.0f) {
        now.nextStrike = -1;
        now.flickers = 0;
    } else {
        // A full storm strikes every five seconds or so; a light one every half minute.
        auto nextGap = [&] { return double(random.range(0.4f, 1.6f) * (5.0f + 25.0f * (1.0f - storm))); };
        if (now.nextStrike < 0 || now.nextStrike > time + 60.0) now.nextStrike = time + nextGap();
        if (time >= now.nextStrike) {
            now.strikeAt = time;
            strike(world, random, viewersOf(world), storm);
            now.flickers = 1 + int(random.next() * 3.5f);
            now.flickerAt = time;
            now.nextStrike = time + nextGap();
        }
        if (now.flickers > 0 && time >= now.flickerAt) {
            now.flash = std::max(now.flash, random.range(0.55f, 1.0f) * (0.5f + 0.5f * storm));
            now.flickers--;
            now.flickerAt = time + double(random.range(0.05f, 0.22f));
        }
    }

    const float rain = std::clamp(weather.rain, 0.0f, 1.0f);
    const float snow = std::clamp(weather.snow, 0.0f, 1.0f);
    const std::vector<Vec3> viewers = (rain > 0 || snow > 0 || now.rain.isValid() || now.snow.isValid()) ? viewersOf(world) : std::vector<Vec3>{};
    const Vec3 wind = world.wind();

    // ---- rain
    ParticleEmitter* drops = emitterOf(scene, now.rain);
    if (rain > 0.0f && !drops) {
        now.rain = makeSystem(scene, "Rain", rainDrops());
        now.splashes = makeSystem(scene, "Rain Splashes", splashDrops());
        now.ripples = makeSystem(scene, "Rain Rings", waterRings());
        now.runs = makeSystem(scene, "Rain Running", runningDrops());
        drops = emitterOf(scene, now.rain);
    }
    if (drops && !emitterOf(scene, now.runs)) now.runs = makeSystem(scene, "Rain Running", runningDrops());   // a world from before drops ran
    ParticleEmitter* splashes = emitterOf(scene, now.splashes);
    ParticleEmitter* rings = emitterOf(scene, now.ripples);
    ParticleEmitter* runs = emitterOf(scene, now.runs);
    if (drops && splashes && rings && runs) {
        // Drops that land this frame end there: on water a ring, on what is solid a splash, thrown
        // off the way the surface faces; on a wall some run down it. (Each was given, as it was
        // born, the time it takes to reach what it lands on, so the emitter takes it away as it
        // lands; its landing is where it will be then.)
        for (const Particle& drop : drops->particles) {
            if (drop.age + dt < drop.lifetime || drop.frame < 0.0f) continue;         // still falling, or falling out of sight
            Vec3 at = drop.position + drop.velocity * std::max(drop.lifetime - drop.age, 0.0f);
            if (!isNearAViewer(viewers, at)) continue;
            if (drop.frame > 0.5f) {
                // On water (marked as it was born): a ring, lying just on it.
                if (int(rings->particles.size()) >= rings->maxParticles) continue;
                Particle ring;
                ring.position = at + Vec3{0, 0.01f, 0};
                ring.lifetime = random.range(0.6f, 1.0f);
                ring.sizeScale = random.range(0.7f, 1.3f);
                rings->particles.push_back(ring);
                continue;
            }
            Vec3 normal = unpackNormal(drop.rotation);
            float speed = simd::length(drop.velocity);
            Vec3 falling = speed > 1e-4f ? drop.velocity / speed : Vec3{0, -1, 0};
            // Straight on, a drop bursts; glancing off a wall, it hardly splashes at all.
            float square = std::clamp(-simd::dot(falling, normal), 0.0f, 1.0f);
            Vec3 bounced = falling - normal * (2.0f * simd::dot(falling, normal));
            // Two ways along the surface, for the spray to spread across it.
            Vec3 side = simd::cross(normal, std::abs(normal.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0});
            side = simd::normalize(side);
            Vec3 other = simd::cross(normal, side);
            int count = square > 0.5f ? (random.next() < 0.6f ? 2 : 1) : (random.next() < square * 2.0f ? 1 : 0);
            for (int i = 0; i < count && int(splashes->particles.size()) < splashes->maxParticles; i++) {
                Particle droplet;
                droplet.position = at + normal * 0.01f;
                float angle = random.range(0.0f, 2.0f * kPi), out = random.range(0.3f, 0.9f);
                Vec3 spread = (side * std::cos(angle) + other * std::sin(angle)) * out;
                Vec3 away = normal * random.range(0.9f, 1.8f) + bounced * 0.4f;
                droplet.velocity = (away + spread) * (0.45f + 0.55f * square);
                droplet.lifetime = random.range(0.18f, 0.32f);
                droplet.sizeScale = random.range(0.7f, 1.2f);
                splashes->particles.push_back(droplet);
            }
            // On what is steep, the water gathers and runs down it: down along the surface.
            if (normal.y < kSteep && normal.y > -0.2f && random.next() < 0.35f && int(runs->particles.size()) < runs->maxParticles) {
                Vec3 downhill = Vec3{0, -1, 0} - normal * (-normal.y);
                float length = simd::length(downhill);
                if (length > 0.05f) {
                    Particle run;
                    run.position = at + normal * 0.004f;
                    run.velocity = downhill / length * random.range(0.35f, 0.8f);
                    run.lifetime = random.range(0.5f, 1.3f);
                    run.sizeScale = random.range(0.8f, 1.2f);
                    runs->particles.push_back(run);
                }
            }
        }
    }
    if (drops && rain > 0.0f && !viewers.empty()) {
        // New drops: born above and round each viewer, falling with the wind, each aimed at what it will hit.
        // A drop falls at its own speed (a large one faster) and the air carries it: within a second
        // of its fall it goes sideways as fast as the wind there, gusts and all, so the rain slants
        // along the wind, more the harder it blows (the drops are born high: they have that second).
        const float gusts = world.gusts();
        Vec3 velocity{wind.x, -kDropSpeed, wind.z};
        // Born upwind of where they will fall, so that the patch of rain stays round the viewer.
        Vec3 lead = Vec3{velocity.x, 0, velocity.z} * (kRainAbove / kDropSpeed);
        float wanted = kDropsPerSecond * rain * rain * dt;
        for (Vec3 viewer : viewers) {
            int count = int(wanted);
            if (random.next() < wanted - float(count)) count++;
            for (int i = 0; i < count && int(drops->particles.size()) < drops->maxParticles; i++) {
                // Spread over a disk, denser near the middle, where it is seen.
                float r = kRainRadius * std::pow(random.next(), 0.7f), a = random.range(0.0f, 2.0f * kPi);
                Vec3 from = viewer + Vec3{std::cos(a) * r, kRainAbove + random.range(-4.0f, 6.0f), std::sin(a) * r} - lead;
                Particle drop;
                drop.position = from;
                float size = random.range(0.9f, 1.12f);                 // its fall: larger drops, faster
                Vec3 air = windAt(wind, gusts, from, time);
                drop.velocity = Vec3{air.x, -kDropSpeed * size, air.z};
                Vec3 at, normal;
                bool onWater = false;
                float fall = kRainAbove + 40.0f;
                // How it will land, kept in `frame` (a drop has no picture whose frames it would choose):
                // 0 on something solid, 1 on water, -1 on nothing (it falls out of sight). The way
                // what it lands on faces is kept in `rotation`, which a streak does not use.
                if (landingOf(world, from, drop.velocity, fall, at, normal, onWater)) {
                    drop.lifetime = simd::length(at - from) / simd::length(drop.velocity);
                    drop.frame = onWater ? 1.0f : 0.0f;
                    drop.rotation = packNormal(normal);
                } else {
                    drop.lifetime = fall / kDropSpeed;
                    drop.frame = -1.0f;
                }
                drops->particles.push_back(drop);
            }
        }
    }

    // ---- snow
    ParticleEmitter* flakes = emitterOf(scene, now.snow);
    if (snow > 0.0f && !flakes) {
        now.snow = makeSystem(scene, "Snow", snowFlakes());
        now.settled = makeSystem(scene, "Snow Settled", settledFlakes());
        flakes = emitterOf(scene, now.snow);
    }
    ParticleEmitter* settled = emitterOf(scene, now.settled);
    if (flakes && settled) {
        // A flake sways and drifts on its way down, so where it lands is not known as it is
        // born: only the height of what is under its way (kept in `rotation`). There it ends; on
        // what is solid it lies a moment.
        for (Particle& flake : flakes->particles) {
            if (flake.age >= flake.lifetime) continue;
            float landing = flake.rotation;
            if (flake.position.y + flake.velocity.y * dt > landing) continue;
            Vec3 at{flake.position.x, landing, flake.position.z};
            flake.age = flake.lifetime;                     // the emitter takes it away
            if (flake.frame != 0.0f || !isNearAViewer(viewers, at) || int(settled->particles.size()) >= settled->maxParticles) continue;
            Particle rest;
            rest.position = at + Vec3{0, 0.012f, 0};
            rest.lifetime = random.range(1.2f, 2.6f);
            rest.sizeScale = flake.sizeScale;
            settled->particles.push_back(rest);
        }
        if (snow > 0.0f && !viewers.empty()) {
            // A flake is light: it goes wherever the air goes, gusts and all, as it drifts down.
            Vec3 drifting{wind.x, -kFlakeSpeed, wind.z};
            const float flakeGusts = world.gusts();
            Vec3 lead = Vec3{drifting.x, 0, drifting.z} * (kSnowAbove / kFlakeSpeed);
            float wanted = kFlakesPerSecond * snow * dt;
            for (Vec3 viewer : viewers) {
                int count = int(wanted);
                if (random.next() < wanted - float(count)) count++;
                for (int i = 0; i < count && int(flakes->particles.size()) < flakes->maxParticles; i++) {
                    float r = kSnowRadius * std::pow(random.next(), 0.7f), a = random.range(0.0f, 2.0f * kPi);
                    Vec3 from = viewer + Vec3{std::cos(a) * r, kSnowAbove + random.range(-3.0f, 5.0f), std::sin(a) * r} - lead;
                    Particle flake;
                    flake.position = from;
                    Vec3 air = windAt(wind, flakeGusts, from, time);
                    flake.velocity = Vec3{air.x, -kFlakeSpeed, air.z} * random.range(0.85f, 1.15f);
                    flake.sizeScale = random.range(0.6f, 1.4f);
                    Vec3 at, normal;
                    bool onWater = false;
                    float fall = kSnowAbove + 30.0f;
                    if (landingOf(world, from, flake.velocity, fall, at, normal, onWater)) {
                        flake.rotation = at.y;
                        flake.frame = onWater ? 1.0f : 0.0f;
                    } else {
                        flake.rotation = from.y - fall;
                        flake.frame = -1.0f;
                    }
                    flake.lifetime = (from.y - flake.rotation) / kFlakeSpeed * 3.0f + 10.0f;     // longer than it can take: it ends where it lands
                    flakes->particles.push_back(flake);
                }
            }
        }
    }
}

}  // namespace molten::kit
