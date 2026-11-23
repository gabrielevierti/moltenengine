#include "Kit/LightProbeVolume.hpp"

#include "Game/Component.hpp"
#include "Kit/Terrain.hpp"
#include "Physics/Physics.hpp"
#include "Rendering/Material.hpp"
#include "Rendering/Mesh.hpp"
#include "Rendering/Renderer.hpp"
#include "Scene/Components.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace molten::kit {

void linkLightProbes() {
    ComponentRegistry::instance().setCategory("LightProbeVolume", "Rendering");
}

ProbeVolumeDesc LightProbeVolume::desc(const Transform& transform) const {
    Quat rotation;
    Vec3 position, scale;
    decompose(transform.world, position, rotation, scale);
    Vec3 half = simd::abs(size * scale) * 0.5f;
    ProbeVolumeDesc out;
    out.box = AABB(position - half, position + half);
    out.spacing = spacing;
    out.bounce = bounce;
    return out;
}

std::string lightingFileFor(const std::string& sceneName) {
    size_t dot = sceneName.rfind('.');
    size_t slash = sceneName.find_last_of('/');
    std::string stem = dot != std::string::npos && (slash == std::string::npos || dot > slash) ? sceneName.substr(0, dot) : sceneName;
    return stem + ".lighting";
}

namespace {

void addTriangles(BakeScene& scene, const MeshData& data, const Mat4& world, Vec3 albedo, Vec3 emissive, bool doubleSided) {
    // A mirrored entity (a negative scale) turns its triangles inside out: they are turned back.
    bool mirrored = simd::dot(simd::cross(xyz(world.columns[0]), xyz(world.columns[1])), xyz(world.columns[2])) < 0;
    for (size_t i = 0; i + 2 < data.indices.size(); i += 3) {
        BakeTriangle t;
        t.a = transformPoint(world, data.positions[data.indices[i]]);
        t.b = transformPoint(world, data.positions[data.indices[i + 1]]);
        t.c = transformPoint(world, data.positions[data.indices[i + 2]]);
        if (mirrored) std::swap(t.b, t.c);
        t.albedo = albedo;
        t.emissive = emissive;
        t.isDoubleSided = doubleSided;
        scene.add(t);
    }
}

}  // namespace

LightingBakeJob prepareLightingBake(World& world, const RenderSettings& settings, int raysPerProbe) {
    LightingBakeJob job;
    Scene& scene = world.scene;
    scene.updateTransforms();

    scene.each<LightProbeVolume, Transform>([&](Entity, LightProbeVolume& volume, Transform& transform) {
        if (transform.isActiveInHierarchy) job.volumes.push_back(volume.desc(transform));
    });
    if (job.volumes.empty()) return job;

    // What is drawn and stays where it is: models, their many copies, the ground.
    std::unordered_map<std::string, std::optional<MeshData>> models;
    auto modelOf = [&](const std::string& name) -> const MeshData* {
        auto found = models.find(name);
        if (found == models.end()) found = models.emplace(name, world.readMeshData(name)).first;
        return found->second ? &*found->second : nullptr;
    };
    scene.each<MeshRenderer, Transform>([&](Entity entity, MeshRenderer& renderer, Transform& transform) {
        if (!renderer.isVisible || !transform.isActiveInHierarchy) return;
        if (const RigidBody* body = scene.get<RigidBody>(entity); body && body->type == BodyType::Dynamic) return;
        const Material* material = renderer.material.get();
        if (material && (material->isSeenThrough() || material->isSea)) return;
        Vec3 albedo = material ? xyz(material->baseColor) * xyz(renderer.tint) : Vec3{0.8f, 0.8f, 0.8f};
        Vec3 emissive = material ? material->emissive : Vec3{0, 0, 0};
        bool doubleSided = material && material->isDoubleSided;
        const MeshData* data = nullptr;
        std::optional<MeshData> ground;
        if (const Terrain* terrain = scene.get<Terrain>(entity); terrain && !terrain->samples.empty()) {
            ground = Terrain::makeMesh(terrain->samples, terrain->resolution, terrain->size, terrain->uvTiling);
            data = &*ground;
        } else {
            std::string name = renderer.mesh ? renderer.mesh->name : renderer.meshName;
            if (name.empty()) return;           // made in code: nothing to read it from
            data = modelOf(name);
        }
        if (!data) return;
        if (const MeshInstances* copies = scene.get<MeshInstances>(entity)) {
            for (const Mat4& place : copies->places) addTriangles(job.scene, *data, transform.world * place, albedo, emissive, doubleSided);
        } else {
            addTriangles(job.scene, *data, transform.world, albedo, emissive, doubleSided);
        }
    });
    job.scene.build();

    // The lights that are on, as the renderer sees them.
    scene.each<Light, Transform>([&](Entity, Light& light, Transform& transform) {
        if (!light.isEnabled || !transform.isActiveInHierarchy || light.intensity <= 0) return;
        BakeLight out;
        out.radiance = light.color * light.intensity;
        out.position = transform.worldPosition();
        out.direction = simd::normalize(transformDirection(transform.world, Vec3{0, 0, -1}));
        out.range = light.range;
        if (light.type == LightType::Directional) out.kind = BakeLight::Sun;
        else if (light.type == LightType::Spot) {
            out.kind = BakeLight::Spot;
            out.cosInner = std::cos(light.innerConeAngle);
            out.cosOuter = std::cos(light.outerConeAngle);
        } else {
            out.kind = BakeLight::Point;            // (an area light, as a point at its middle)
        }
        job.lights.push_back(out);
    });

    // The sky's light on what the rays hit, roughly: the colors of its upper half, as bright as the scene has it.
    Vec3 sky = (settings.sky.zenithColor + settings.sky.horizonColor) * 0.5f * settings.sky.intensity * settings.environmentIntensity;
    job.settings.skyLight = sky;
    job.settings.raysPerProbe = raysPerProbe;
    return job;
}

}  // namespace molten::kit

using namespace molten::kit;

MOLTEN_COMPONENT(LightProbeVolume,
                 MOLTEN_FIELD(size).help("Meters along the world's x, y and z, centered on the entity (times its scale). Round a room, a building, a street"),
                 MOLTEN_FIELD(spacing).range(0.25f, 32).help("Meters between probes. 1 for a room, 2 for a building, 4 or more for open ground; "
                                                            "closer is finer and slower to bake"),
                 MOLTEN_FIELD(bounce).range(0, 4).help("How much light the walls and floor throw back: 1 as their colors say, 0 none (only the sky they hide)"))
