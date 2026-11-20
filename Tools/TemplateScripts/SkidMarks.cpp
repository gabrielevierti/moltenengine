// SkidMarks.cpp
// Black marks on the road where the tires slide: put it on an entity with a Mesh Renderer
// (a dark, see-through material) at the origin. Each wheel of the car lays a strip while it
// slides; the oldest strips are taken up again when there are too many.
#include "MoltenMetal.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace molten;

struct SkidMarks {
    std::string car = "Car";            // the name of the car whose wheels leave them
    int segmentsPerWheel = 400;         // how much of the road can be marked at once
    float width = 0.19f;                // meters: the tire's
    float spacing = 0.22f;              // meters between the points of a strip

    struct Trail {
        Vec3 last{0, 0, 0};
        bool isLaying = false;
        int next = 0;                   // the segment it writes next, round and round
    };
    std::vector<Trail> trails;
    MeshData marks;
    bool isChanged = false;

    void start(World&, Entity) {
        trails.clear();
        marks = MeshData{};
    }

    void update(World& world, Entity self, float) {
        Entity body = world.find(car);
        const Transform* carPlace = world.scene.get<Transform>(body);
        if (!carPlace) return;
        Vec3 up = rotate(carPlace->rotation, Vec3{0, 1, 0});
        // The wheels of the car, in a fixed order.
        std::vector<Entity> wheels;
        world.scene.each<kit::Wheel, Transform>([&](Entity entity, kit::Wheel&, Transform&) {
            if (kit::vehicleOf(world.scene, entity) == body) wheels.push_back(entity);
        });
        if (wheels.empty()) return;
        if (marks.positions.empty()) makeRoom(int(wheels.size()));
        trails.resize(wheels.size());

        for (size_t k = 0; k < wheels.size(); k++) {
            const kit::Wheel* wheel = world.scene.get<kit::Wheel>(wheels[k]);
            const Transform* place = world.scene.get<Transform>(wheels[k]);
            Trail& trail = trails[k];
            // How much more it is asked than it can give: past its best slip, sideways or along.
            float excess = std::max(std::abs(wheel->state.slipRatio) / 0.16f, std::abs(wheel->state.slipAngle) / 0.2f) - 1.0f;
            bool slides = wheel->state.isOnGround && excess > 0.15f && wheel->state.load > 600.0f;
            Vec3 ground = place->worldPosition() - up * (wheel->radius - wheel->state.deflection) + up * 0.012f;
            if (!slides) {
                trail.isLaying = false;
                continue;
            }
            if (!trail.isLaying) {
                trail.last = ground;
                trail.isLaying = true;
                continue;
            }
            Vec3 along = ground - trail.last;
            float length = simd::length(along);
            if (length < spacing) continue;
            if (length > spacing * 6) {                 // a jump, or the car put somewhere else: a new strip
                trail.last = ground;
                continue;
            }
            Vec3 across = safeNormalize(simd::cross(up, along / length), Vec3{1, 0, 0}) * (width * 0.5f);
            int segment = 1 + int(k) * segmentsPerWheel + trail.next;      // segment 0 holds the bounds
            trail.next = (trail.next + 1) % segmentsPerWheel;
            size_t v = size_t(segment) * 4;
            marks.positions[v + 0] = trail.last - across;
            marks.positions[v + 1] = trail.last + across;
            marks.positions[v + 2] = ground + across;
            marks.positions[v + 3] = ground - across;
            trail.last = ground;
            isChanged = true;
        }
        if (isChanged) {
            world.showMesh(self, marks);
            isChanged = false;
        }
    }

    /// Every segment there will ever be, folded to nothing until it is laid: the mesh keeps
    /// its size, so that it is only changed, never made again. The first stands for the
    /// whole road, so that the marks are never thought to be out of sight.
    void makeRoom(int wheelCount) {
        int segments = 1 + wheelCount * segmentsPerWheel;
        marks.positions.assign(size_t(segments) * 4, Vec3{0, -1000, 0});
        marks.normals.assign(marks.positions.size(), Vec3{0, 1, 0});
        marks.uvs.assign(marks.positions.size(), Vec2{0, 0});
        marks.tangents.assign(marks.positions.size(), Vec4{1, 0, 0, 1});
        for (int s = 0; s < segments; s++) {
            uint32_t base = uint32_t(s) * 4;
            marks.uvs[base + 1] = Vec2{1, 0};
            marks.uvs[base + 2] = Vec2{1, 1};
            marks.uvs[base + 3] = Vec2{0, 1};
            for (uint32_t i : {0u, 2u, 1u, 0u, 3u, 2u}) marks.indices.push_back(base + i);
        }
        marks.positions[0] = Vec3{-600, -1, -600};
        marks.positions[1] = marks.positions[2] = marks.positions[3] = Vec3{600, 1, 600};
    }
};

MOLTEN_COMPONENT(SkidMarks, MOLTEN_FIELD(car).help("The name of the car whose wheels leave them"), MOLTEN_FIELD(segmentsPerWheel).range(50, 2000),
                 MOLTEN_FIELD(width).range(0.05f, 0.6f), MOLTEN_FIELD(spacing).range(0.05f, 1))
