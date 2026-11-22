#include "Kit/Navigation.hpp"

#include "Animation/Animator.hpp"
#include "Game/Component.hpp"
#include "Kit/Character.hpp"
#include "Kit/Shooting.hpp"
#include "Physics/Collision.hpp"
#include "Scene/Components.hpp"

#include <algorithm>
#include <cmath>
#include <queue>

namespace molten::kit {

void linkNavigation() {}

NavigationArea* navigationAreaOf(World& world, Entity* on) {
    NavigationArea* found = nullptr;
    world.scene.each<NavigationArea>([&](Entity entity, NavigationArea& area) {
        if (found) return;
        found = &area;
        if (on) *on = entity;
    });
    return found;
}

// ---------------------------------------------------------------- the grid

int NavigationArea::cellOf(Vec3 point) const {
    int column = int(std::floor((point.x - origin.x) / cellSize));
    int row = int(std::floor((point.z - origin.z) / cellSize));
    if (column < 0 || row < 0 || column >= columns || row >= rows) return -1;
    return row * columns + column;
}

Vec3 NavigationArea::centerOf(int cell) const {
    int column = cell % columns, row = cell / columns;
    return Vec3{origin.x + (float(column) + 0.5f) * cellSize, heights[size_t(cell)], origin.z + (float(row) + 0.5f) * cellSize};
}

bool NavigationArea::canStep(int from, int to) const {
    if (from < 0 || to < 0 || !walkable[size_t(from)] || !walkable[size_t(to)]) return false;
    return std::abs(heights[size_t(from)] - heights[size_t(to)]) <= stepHeight;
}

bool NavigationArea::isWalkable(Vec3 point) const {
    int cell = cellOf(point);
    return cell >= 0 && walkable[size_t(cell)];
}

void NavigationArea::update(World& world, Entity self, float) {
    // After the first physics step, when the colliders of models have been read.
    if (!isBaked) bake(world, self);
}

namespace {

/// True for what moves by itself, or is part of something that does: a character, an agent, a machine that hunts.
bool goesAbout(Scene& scene, Entity entity) {
    for (int up = 0; up < 16 && scene.isAlive(entity); up++) {
        if (scene.get<CharacterController>(entity) || scene.get<NavAgent>(entity) || scene.get<Hunter>(entity)) return true;
        const Transform* place = scene.get<Transform>(entity);
        if (!place) break;
        entity = place->parent;
    }
    return false;
}

}  // namespace

void NavigationArea::bake(World& world, Entity self) {
    const Transform* place = world.scene.get<Transform>(self);
    Vec3 center = place ? place->worldPosition() : Vec3{0, 0, 0};
    float cell = std::max(cellSize, 0.05f);
    columns = std::max(1, int(std::ceil(size.x / cell)));
    rows = std::max(1, int(std::ceil(size.y / cell)));
    origin = Vec3{center.x - float(columns) * cell * 0.5f, center.y, center.z - float(rows) * cell * 0.5f};
    walkable.assign(size_t(columns * rows), 0);
    heights.assign(size_t(columns * rows), center.y);
    const float walkableNormal = std::cos(radians(std::clamp(maxSlope, 0.0f, 89.0f)));
    world.scene.updateTransforms();

    // Someone of the agents' size, standing a step above the ground: whatever it overlaps is in the way.
    ShapeInstance body;
    body.shape = ColliderShape::Capsule;
    body.radius = std::max(agentRadius, 0.05f);
    float tall = std::max(agentHeight - stepHeight, body.radius * 2.0f + 0.01f);
    body.halfHeight = std::max(tall * 0.5f - body.radius, 0.0f);
    for (int row = 0; row < rows; row++) {
        for (int column = 0; column < columns; column++) {
            size_t index = size_t(row * columns + column);
            Vec3 top{origin.x + (float(column) + 0.5f) * cell, center.y + above, origin.z + (float(row) + 0.5f) * cell};
            // What walks or flies about is not ground, nor in the way: it will not stay where it is. The ground is what is under it.
            auto ground = world.physics.raycast(world.scene, Ray{top, Vec3{0, -1, 0}}, above + below);
            for (int through = 0; ground && through < 4 && goesAbout(world.scene, ground->entity); through++) {
                float used = ground->distance + 0.02f + (top.y - ground->point.y - ground->distance);
                Vec3 from = ground->point - Vec3{0, 0.02f, 0};
                ground = world.physics.raycast(world.scene, Ray{from, Vec3{0, -1, 0}}, std::max(above + below - used, 0.1f), 0xFFFFFFFFu, ground->entity);
            }
            if (!ground || ground->normal.y < walkableNormal) continue;
            // A body that moves is not ground to plan on.
            if (const RigidBody* rigid = world.scene.get<RigidBody>(ground->entity); rigid && rigid->type == BodyType::Dynamic) continue;
            heights[index] = ground->point.y;
            body.center = ground->point + Vec3{0, stepHeight + tall * 0.5f, 0};
            AABB box = body.bounds();
            bool blocked = false;
            world.physics.forEachShapeNear(world.scene, box, Entity{}, [&](Entity other, const ShapeInstance& shape) {
                if (blocked) return;
                if (const RigidBody* rigid = world.scene.get<RigidBody>(other); rigid && rigid->type == BodyType::Dynamic) return;
                if (goesAbout(world.scene, other)) return;
                ContactInfo contact;
                if (collide(body, shape, contact) && contact.depth > 0.005f) blocked = true;
            });
            walkable[index] = blocked ? 0 : 1;
        }
    }
    isBaked = true;
}

bool NavigationArea::nearestWalkable(Vec3 point, Vec3& found) const {
    if (!isBaked) return false;
    int column = int(std::floor((point.x - origin.x) / cellSize)), row = int(std::floor((point.z - origin.z) / cellSize));
    // Rings of cells round the point, nearest first.
    for (int ring = 0; ring <= 12; ring++) {
        float best = 1e30f;
        int bestCell = -1;
        for (int dz = -ring; dz <= ring; dz++) {
            for (int dx = -ring; dx <= ring; dx++) {
                if (std::max(std::abs(dx), std::abs(dz)) != ring) continue;
                int c = column + dx, r = row + dz;
                if (c < 0 || r < 0 || c >= columns || r >= rows) continue;
                int cell = r * columns + c;
                if (!walkable[size_t(cell)]) continue;
                Vec3 at = centerOf(cell);
                float distance = simd::length_squared(Vec3{at.x - point.x, (at.y - point.y) * 0.25f, at.z - point.z});
                if (distance < best) best = distance, bestCell = cell;
            }
        }
        if (bestCell >= 0) {
            found = centerOf(bestCell);
            if (ring == 0) found = Vec3{point.x, found.y, point.z};
            return true;
        }
    }
    return false;
}

bool NavigationArea::isLineWalkable(Vec3 from, Vec3 to) const {
    // Every cell the straight line crosses, sampled at a third of a cell, walkable and stepped into.
    Vec3 delta = to - from;
    float length = std::sqrt(delta.x * delta.x + delta.z * delta.z);
    int samples = std::max(1, int(std::ceil(length / (cellSize / 3.0f))));
    int previous = cellOf(from);
    // As wide as the agent: the line and two beside it.
    Vec3 side = length > 1e-5f ? Vec3{-delta.z, 0, delta.x} / length * (agentRadius * 0.5f) : Vec3{0, 0, 0};
    for (int i = 1; i <= samples; i++) {
        Vec3 at = from + delta * (float(i) / float(samples));
        int cell = cellOf(at);
        if (cell < 0 || !walkable[size_t(cell)]) return false;
        if (cell != previous && !canStep(previous, cell)) return false;
        for (Vec3 offset : {side, -side}) {
            int beside = cellOf(at + offset);
            if (beside < 0 || !walkable[size_t(beside)]) return false;
        }
        previous = cell;
    }
    return true;
}

std::vector<Vec3> NavigationArea::findPath(Vec3 from, Vec3 to) const {
    std::vector<Vec3> path;
    Vec3 start, goal;
    if (!isBaked) return path;
    // A point outside the area: its edge, nearest the point.
    auto inside = [&](Vec3 point) {
        point.x = std::clamp(point.x, origin.x + cellSize * 0.5f, origin.x + (float(columns) - 0.5f) * cellSize);
        point.z = std::clamp(point.z, origin.z + cellSize * 0.5f, origin.z + (float(rows) - 0.5f) * cellSize);
        return point;
    };
    if (!nearestWalkable(inside(from), start) || !nearestWalkable(inside(to), goal)) return path;
    int first = cellOf(start), last = cellOf(goal);
    if (first < 0 || last < 0) return path;
    if (first == last || isLineWalkable(start, goal)) return {start, goal};

    // A* over the cells, eight ways, with the octile distance to the goal as the guess.
    const size_t count = walkable.size();
    std::vector<float> cost(count, 1e30f);
    std::vector<int> cameFrom(count, -1);
    std::vector<uint8_t> closed(count, 0);
    using Open = std::pair<float, int>;
    std::priority_queue<Open, std::vector<Open>, std::greater<Open>> open;
    auto guess = [&](int cell) {
        float dx = std::abs(float(cell % columns - last % columns)), dz = std::abs(float(cell / columns - last / columns));
        return (dx + dz + (1.41421356f - 2.0f) * std::min(dx, dz)) * cellSize;
    };
    cost[size_t(first)] = 0;
    open.push({guess(first), first});
    bool found = false;
    while (!open.empty()) {
        int cell = open.top().second;
        open.pop();
        if (closed[size_t(cell)]) continue;
        closed[size_t(cell)] = 1;
        if (cell == last) {
            found = true;
            break;
        }
        int column = cell % columns, row = cell / columns;
        for (int dz = -1; dz <= 1; dz++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dz == 0) continue;
                int c = column + dx, r = row + dz;
                if (c < 0 || r < 0 || c >= columns || r >= rows) continue;
                int neighbor = r * columns + c;
                if (closed[size_t(neighbor)] || !canStep(cell, neighbor)) continue;
                // Not diagonally between two cells that cannot be walked: no cutting corners.
                if (dx != 0 && dz != 0 && (!canStep(cell, row * columns + c) || !canStep(cell, r * columns + column))) continue;
                float step = (dx != 0 && dz != 0 ? 1.41421356f : 1.0f) * cellSize;
                float climb = std::abs(heights[size_t(neighbor)] - heights[size_t(cell)]);
                float through = cost[size_t(cell)] + step + climb;
                if (through < cost[size_t(neighbor)]) {
                    cost[size_t(neighbor)] = through;
                    cameFrom[size_t(neighbor)] = cell;
                    open.push({through + guess(neighbor), neighbor});
                }
            }
        }
    }
    if (!found) return path;
    std::vector<Vec3> cells;
    for (int cell = last; cell >= 0; cell = cameFrom[size_t(cell)]) cells.push_back(centerOf(cell));
    std::reverse(cells.begin(), cells.end());
    cells.front() = start;
    cells.back() = goal;
    // Pulled straight: from each point, on to the farthest one that can be walked to in a line.
    path.push_back(cells.front());
    size_t at = 0;
    while (at + 1 < cells.size()) {
        size_t farthest = at + 1;
        for (size_t k = cells.size() - 1; k > at + 1; k--) {
            if (isLineWalkable(cells[at], cells[k])) {
                farthest = k;
                break;
            }
        }
        path.push_back(cells[farthest]);
        at = farthest;
    }
    return path;
}

// ---------------------------------------------------------------- NavAgent

void NavAgent::fixedUpdate(World& world, Entity self, float dt) {
    Transform* transform = world.scene.get<Transform>(self);
    NavigationArea* area = navigationAreaOf(world);
    if (!transform || !area || !area->isBaked || dt <= 0) return;
    if (!target.empty()) {
        if (const Transform* followed = world.scene.get<Transform>(world.find(target))) {
            destination = followed->worldPosition();
            hasDestination = true;
        }
    }
    Vec3 feet = transform->worldPosition();
    CharacterController* controller = world.scene.get<CharacterController>(self);
    Vec3 step{0, 0, 0};
    speedNow = 0;
    if (hasDestination) {
        Vec3 away = destination - feet;
        arrived = std::sqrt(away.x * away.x + away.z * away.z) <= stoppingDistance;
        sincePath += dt;
        if (!arrived && (path.empty() || sincePath >= repathEvery)) {
            path = area->findPath(feet, destination);
            next = path.size() > 1 ? 1 : 0;
            sincePath = 0;
        }
        if (!arrived && next < path.size()) {
            Vec3 toward = path[next] - feet;
            float flat = std::sqrt(toward.x * toward.x + toward.z * toward.z);
            if (flat < std::max(speed * dt * 1.5f, 0.15f) && next + 1 < path.size()) {
                next++;
                toward = path[next] - feet;
                flat = std::sqrt(toward.x * toward.x + toward.z * toward.z);
            }
            if (flat > 1e-4f) {
                Vec3 direction{toward.x / flat, 0, toward.z / flat};
                float distance = std::min(speed * dt, flat);
                step = direction * distance;
                speedNow = distance / dt;
                Quat wanted = lookRotation(direction);
                transform->rotation = simd::normalize(simd::slerp(transform->rotation, wanted, std::min(turnSpeed * dt, 1.0f)));
            }
        }
    }
    if (controller) {
        // Walked with its capsule: round other characters and up steps, with gravity.
        fall = controller->isGrounded ? -2.0f : fall - 20.0f * dt;
        controller->move(world, self, step + Vec3{0, fall * dt, 0});
    } else {
        // Without one, it keeps to the ground the area found.
        Vec3 placed = feet + step;
        if (int cell = area->cellOf(placed); cell >= 0 && area->walkable[size_t(cell)]) placed.y = area->heights[size_t(cell)];
        if (!transform->parent.isValid()) transform->position = placed;
    }
}

// ---------------------------------------------------------------- Guard

const char* Guard::stateName() const {
    switch (state) {
        case State::Patrol: return "Patrol";
        case State::Chase: return "Chase";
        case State::Search: return "Search";
        case State::Return: return "Return";
    }
    return "";
}

void Guard::fixedUpdate(World& world, Entity self, float dt) {
    NavAgent* agent = world.scene.get<NavAgent>(self);
    const Transform* place = world.scene.get<Transform>(self);
    if (!agent || !place) return;
    agent->target.clear();                  // the guard says where it goes
    Entity seen = world.find(player);
    const Transform* body = world.scene.get<Transform>(seen);

    // Does it see the player: near enough, ahead within its angle (or very near), nothing between.
    seesPlayer = false;
    Vec3 eyes = place->worldPosition() + Vec3{0, 1.6f, 0};
    if (body) {
        Vec3 them = body->worldPosition() + Vec3{0, 1.2f, 0};
        Vec3 toward = them - eyes;
        float distance = simd::length(toward);
        Vec3 facing = transformDirection(place->world, Vec3{0, 0, -1});
        facing.y = 0;
        facing = safeNormalize(facing, Vec3{0, 0, -1});
        Vec3 flat = safeNormalize(Vec3{toward.x, 0, toward.z}, facing);
        bool ahead = simd::dot(facing, flat) >= std::cos(radians(sightAngle * 0.5f));
        if (distance <= hearingDistance || (distance <= sightDistance && ahead)) {
            auto hit = world.physics.raycast(world.scene, Ray{eyes, toward / std::max(distance, 1e-4f)}, distance, 0xFFFFFFFFu, self);
            seesPlayer = !hit || hit->entity == seen || Physics::isUnder(world.scene, hit->entity, seen);
        }
        if (seesPlayer) lastSeen = body->worldPosition();
    }

    Entity next = world.find(round + " " + std::to_string(point));
    if (!next.isValid() && point != 1) {
        point = 1;
        next = world.find(round + " 1");
    }
    switch (state) {
        case State::Patrol:
        case State::Return:
            agent->speed = walkSpeed;
            if (seesPlayer) {
                state = State::Chase;
                break;
            }
            if (const Transform* stop = world.scene.get<Transform>(next)) {
                if (!agent->hasDestination || simd::length(agent->destination - stop->worldPosition()) > 0.01f) agent->goTo(stop->worldPosition());
                if (agent->hasArrived()) {
                    state = State::Patrol;
                    timer += dt;
                    if (timer >= waitAtPoints) {
                        timer = 0;
                        point++;
                    }
                }
            } else {
                agent->stop();
            }
            break;
        case State::Chase:
            agent->speed = chaseSpeed;
            agent->goTo(lastSeen);
            if (!seesPlayer) {
                state = State::Search;
                timer = 0;
            }
            break;
        case State::Search:
            agent->speed = walkSpeed;
            if (!agent->hasDestination || simd::length(agent->destination - lastSeen) > 0.01f) agent->goTo(lastSeen);
            timer += dt;
            if (seesPlayer) state = State::Chase;
            else if (timer >= giveUpAfter) {
                state = State::Return;
                timer = 0;
                agent->stop();
            }
            break;
    }
    // An Animator on it or under it hears how fast it goes and whether it is after someone.
    world.scene.each<Animator>([&](Entity entity, Animator& animator) {
        if ((entity == self || Physics::isUnder(world.scene, entity, self)) && !animator.controller.empty()) {
            animator.setFloat("speed", agent->speedNow);
            animator.setBool("chasing", state == State::Chase);
        }
    });
}

}  // namespace molten::kit

namespace molten::kit {

MOLTEN_COMPONENT(Guard, MOLTEN_FIELD(round).picks("entityPrefix").help("Its round: entities named this and 1, 2, 3... in order"), MOLTEN_FIELD(player).picks("entity"),
                 MOLTEN_FIELD(sightDistance).range(0, 100), MOLTEN_FIELD(sightAngle).range(0, 360), MOLTEN_FIELD(hearingDistance).range(0, 30),
                 MOLTEN_FIELD(walkSpeed).range(0, 20), MOLTEN_FIELD(chaseSpeed).range(0, 30), MOLTEN_FIELD(giveUpAfter).range(0, 30),
                 MOLTEN_FIELD(waitAtPoints).range(0, 30))
MOLTEN_COMPONENT(NavigationArea, MOLTEN_FIELD(size).help("Meters along x and z, centered on the entity"), MOLTEN_FIELD(cellSize).range(0.1f, 4),
                 MOLTEN_FIELD(agentRadius).range(0.05f, 4), MOLTEN_FIELD(agentHeight).range(0.2f, 10), MOLTEN_FIELD(stepHeight).range(0, 2),
                 MOLTEN_FIELD(maxSlope).range(0, 89), MOLTEN_FIELD(above).range(0, 500), MOLTEN_FIELD(below).range(0, 500))
MOLTEN_COMPONENT(NavAgent, MOLTEN_FIELD(target).picks("entity").help("The name of an entity to go to and follow; empty: where code sends it with goTo"),
                 MOLTEN_FIELD(speed).range(0, 30), MOLTEN_FIELD(turnSpeed).range(0, 40), MOLTEN_FIELD(stoppingDistance).range(0, 10),
                 MOLTEN_FIELD(repathEvery).range(0.05f, 5))

}  // namespace molten::kit
