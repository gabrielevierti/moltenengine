#include "SceneDocument.hpp"
#include "Assets/AssetDatabase.hpp"

#include "Core/TextFile.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unordered_map>

namespace molten::editor {

namespace {
const size_t kUndoLevels = 200;
}

// ---------------------------------------------------------------- the file

std::string SceneDocument::snapshot() {
    return writeScene(engine_.scene, materials, engine_.renderer().settings);
}

void SceneDocument::resetHistory() {
    engine_.scene.updateTransforms();
    current_ = snapshot();
    saved_ = current_;
    undo_.clear();
    redo_.clear();
    touched_ = false;
}

void fitColliderToMesh(Collider& collider, const AABB& meshBounds, Vec3 scale) {
    Vec3 size{std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)};
    Vec3 half = meshBounds.halfExtents() * size;
    Vec3 offset = meshBounds.center() * scale;
    if (half.y < 0.02f) {
        // Nothing is as thin as a plane: a slab under it, its top where the plane is.
        half.y = 0.5f;
        offset.y -= 0.5f;
    }
    collider.halfExtents = simd::max(half, Vec3{0.01f, 0.01f, 0.01f});
    collider.radius = collider.shape == ColliderShape::Sphere ? std::max({half.x, half.y, half.z}) : std::max(half.x, half.z);
    collider.halfHeight = std::max(half.y - collider.radius, 0.0f);
    // A mesh collider is the model itself, where it is drawn: its middle is already in its points.
    collider.offset = collider.shape == ColliderShape::Mesh ? Vec3{0, 0, 0} : offset;
}

bool isColliderFitted(const Collider& collider, const AABB& meshBounds, Vec3 scale) {
    if (collider.shape == ColliderShape::Mesh) return false;
    Collider fitted = collider;
    fitColliderToMesh(fitted, meshBounds, scale);
    auto near = [](float a, float b) { return std::abs(a - b) <= 0.002f + 0.01f * std::max(std::abs(a), std::abs(b)); };
    auto nearVec = [&](Vec3 a, Vec3 b) { return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z); };
    if (!nearVec(collider.offset, fitted.offset)) return false;
    if (collider.shape == ColliderShape::Box) return nearVec(collider.halfExtents, fitted.halfExtents);
    if (collider.shape == ColliderShape::Sphere) return near(collider.radius, fitted.radius);
    return near(collider.radius, fitted.radius) && near(collider.halfHeight, fitted.halfHeight);
}

void SceneDocument::keepCollidersFitted() {
    Scene& scene = engine_.scene;
    std::vector<ScaleSeen> now;
    for (Entity entity : selection_) {
        const Transform* transform = scene.get<Transform>(entity);
        if (!transform) continue;
        Vec3 scale = transform->scale;
        for (const ScaleSeen& seen : scaleSeen_) {
            if (seen.entity != entity || (seen.scale.x == scale.x && seen.scale.y == scale.y && seen.scale.z == scale.z)) continue;
            Collider* collider = scene.get<Collider>(entity);
            const MeshRenderer* renderer = scene.get<MeshRenderer>(entity);
            if (collider && renderer && renderer->mesh && isColliderFitted(*collider, renderer->mesh->bounds(), seen.scale)) {
                fitColliderToMesh(*collider, renderer->mesh->bounds(), scale);
                markChanged();
            }
        }
        now.push_back(ScaleSeen{entity, scale});
    }
    scaleSeen_ = std::move(now);
}

std::string SceneDocument::displayName() const {
    return filePath.empty() ? "Untitled" : std::filesystem::path(filePath).stem().string();
}

void SceneDocument::createCamera() {
    camera_ = engine_.scene.create("Editor Camera");
    Transform transform;
    transform.position = cameraPosition;
    transform.rotation = cameraRotation();
    engine_.scene.add(camera_, transform);
    Camera camera;
    camera.farPlane = 2000.0f;
    engine_.scene.add(camera_, camera);
    engine_.scene.add(camera_, EditorOnly{});
    // The editor's camera is the scene view's alone (see Renderer::views): the game never looks through it.
    engine_.scene.get<Camera>(camera_)->isActive = false;
    engine_.renderer().cameraOverride = Entity{};
}

void SceneDocument::syncCamera() {
    if (Transform* transform = engine_.scene.get<Transform>(camera_)) {
        transform->position = cameraPosition;
        transform->rotation = cameraRotation();
    }
}

/// Replaces the scene with the one `text` describes. The selection is kept by position
/// in the scene: the entities are new, but the third one is still the third one.
void SceneDocument::keepPartPlace(Entity part) {
    Scene& scene = engine_.scene;
    if (!scene.has<FromPrefab>(part)) return;
    Entity copy = part;
    for (int depth = 0; depth < 64 && scene.isAlive(copy); depth++) {
        if (scene.has<PrefabInstance>(copy) && !scene.has<FromPrefab>(copy)) break;
        const Transform* transform = scene.get<Transform>(copy);
        copy = transform ? transform->parent : Entity{};
    }
    PrefabInstance* instance = scene.get<PrefabInstance>(copy);
    const Transform* transform = scene.get<Transform>(part);
    std::string path;
    if (!instance || !transform || !pathBelow(scene, copy, part, path)) return;
    char text[128];
    auto vec = [&](Vec3 v) {
        std::snprintf(text, sizeof(text), "%g %g %g", double(v.x), double(v.y), double(v.z));
        return std::string(text);
    };
    instance->setOverride(PrefabOverride{path, "Transform", "position", vec(transform->position)});
    instance->setOverride(PrefabOverride{path, "Transform", "rotation", vec(toEulerAngles(transform->rotation) * (180.0f / kPi))});
    instance->setOverride(PrefabOverride{path, "Transform", "scale", vec(transform->scale)});
}

void SceneDocument::rebuild() {
    restore(snapshot());
    markChanged();
}

void SceneDocument::retargetAsset(const std::string& from, const std::string& to) {
    auto moved = [&](std::string& text) { text = AssetDatabase::replaceReferences(text, from, to); };
    for (std::string& step : undo_) moved(step);
    for (std::string& step : redo_) moved(step);
    moved(saved_);
    if (isPlaying_) {
        // The game keeps what it has until it stops; the scene it goes back to has the new path.
        moved(beforePlay_);
        return;
    }
    std::string now = snapshot();
    std::string changed = AssetDatabase::replaceReferences(now, from, to);
    if (changed == now) return;
    bool wasSaved = current_ == saved_ || AssetDatabase::replaceReferences(current_, from, to) == saved_;
    restore(changed);
    current_ = snapshot();
    if (wasSaved) saved_ = current_;
}

void SceneDocument::restore(const std::string& text) {
    std::vector<int> selectedIndices;
    for (Entity entity : selection_) {
        auto found = std::find(entities_.begin(), entities_.end(), entity);
        if (found != entities_.end()) selectedIndices.push_back(int(found - entities_.begin()));
    }
    engine_.scene.clear();
    engine_.physics.reset();
    createCamera();
    readScene(text, engine_.scene, materials, engine_.renderer().settings, sceneAssets(engine_.assets()));
    defaultMaterial();
    beginFrame();
    selection_.clear();
    for (int index : selectedIndices) {
        if (index < int(entities_.size())) selection_.push_back(entities_[size_t(index)]);
    }
    engine_.scene.updateTransforms();
}

/// A floor, something on it, a sun and a camera: enough to see the lighting.
void SceneDocument::populateDefaultScene(Scene& scene, Assets& assets, std::vector<NamedMaterial>& materials) {
    auto ground = std::make_shared<Material>();
    ground->baseColor = Vec4{0.55f, 0.57f, 0.6f, 1};
    ground->roughness = 0.9f;
    materials = {{"Default", std::make_shared<Material>()}, {"Ground", ground}};

    Entity sun = scene.create("Sun");
    Transform sunTransform;
    sunTransform.position = Vec3{0, 6, 0};
    sunTransform.rotation = lookRotation(Vec3{0.5f, -0.8f, -0.35f});
    scene.add(sun, sunTransform);
    Light light;
    light.type = LightType::Directional;
    light.color = Vec3{1.0f, 0.95f, 0.85f};
    light.intensity = 3.5f;
    light.castsShadows = true;
    scene.add(sun, light);

    Entity floor = scene.create("Floor");
    Transform floorTransform;
    floorTransform.scale = Vec3{20, 1, 20};
    scene.add(floor, floorTransform);
    scene.add(floor, MeshRenderer{assets.plane(), ground});
    Collider floorCollider = Collider::box(Vec3{10, 0.5f, 10});
    floorCollider.offset = Vec3{0, -0.5f, 0};
    scene.add(floor, floorCollider);

    Entity cube = scene.create("Cube");
    Transform cubeTransform;
    cubeTransform.position = Vec3{0, 0.5f, 0};
    scene.add(cube, cubeTransform);
    scene.add(cube, MeshRenderer{assets.cube(), materials[0].material});
    scene.add(cube, Collider::box(Vec3{0.5f, 0.5f, 0.5f}));

    Entity camera = scene.create("Main Camera");
    Transform cameraTransform;
    cameraTransform.position = Vec3{0, 2, 7};
    cameraTransform.rotation = lookRotation(Vec3{0, -0.2f, -1});
    scene.add(camera, cameraTransform);
    scene.add(camera, Camera{});
}

// What a new scene starts with: the defaults, and the sea, a little under the floor. (A scene of
// before, or one a game makes in code, has none: RenderSettings{}.)
static RenderSettings newSceneSettings() {
    RenderSettings settings;
    settings.sea.enabled = true;
    settings.sea.level = -0.5f;
    return settings;
}

std::string SceneDocument::defaultSceneText() {
    Scene scene;
    std::vector<NamedMaterial> sceneMaterials;
    populateDefaultScene(scene, engine_.assets(), sceneMaterials);
    scene.updateTransforms();
    return writeScene(scene, sceneMaterials, newSceneSettings());
}

void SceneDocument::newScene() {
    if (isPlaying_) setPlaying(false);
    filePath.clear();
    Scene& scene = engine_.scene;
    scene.clear();
    engine_.physics.reset();
    engine_.renderer().settings = newSceneSettings();
    selection_.clear();
    createCamera();

    populateDefaultScene(scene, engine_.assets(), materials);

    beginFrame();
    resetHistory();
}

bool SceneDocument::open(const std::string& absolutePath, std::string* error) {
    std::optional<std::string> contents = text::readFile(absolutePath);
    // Read it into a scratch scene first: a file that cannot be read must not cost the open scene.
    Scene probe;
    std::vector<NamedMaterial> probeMaterials;
    RenderSettings probeSettings;
    if (!contents || !readScene(*contents, probe, probeMaterials, probeSettings, SceneAssets{})) {
        if (error) *error = "Cannot open " + absolutePath;
        return false;
    }
    if (isPlaying_) setPlaying(false);
    filePath = absolutePath;
    selection_.clear();
    entities_.clear();
    engine_.renderer().settings = RenderSettings{};
    restore(*contents);
    resetHistory();
    return true;
}

bool SceneDocument::save(std::string* error) {
    if (filePath.empty()) {
        if (error) *error = "The scene has no file yet";
        return false;
    }
    return saveAs(filePath, error);
}

bool SceneDocument::saveAs(const std::string& absolutePath, std::string* error) {
    if (isPlaying_) {
        if (error) *error = "Stop the game before saving";
        return false;
    }
    std::error_code ignored;
    std::filesystem::create_directories(std::filesystem::path(absolutePath).parent_path(), ignored);
    engine_.scene.updateTransforms();
    std::string contents = snapshot();
    if (!text::writeFile(absolutePath, contents)) {
        if (error) *error = "Cannot write " + absolutePath;
        return false;
    }
    filePath = absolutePath;
    current_ = contents;
    saved_ = contents;
    return true;
}

// ---------------------------------------------------------------- the selection

bool SceneDocument::isSelected(Entity entity) const {
    return std::find(selection_.begin(), selection_.end(), entity) != selection_.end();
}

void SceneDocument::select(Entity entity) {
    selection_.clear();
    if (engine_.scene.isAlive(entity)) selection_.push_back(entity);
}

void SceneDocument::addToSelection(Entity entity) {
    if (!engine_.scene.isAlive(entity)) return;
    selection_.erase(std::remove(selection_.begin(), selection_.end(), entity), selection_.end());
    selection_.push_back(entity);       // last: it becomes the primary one
}

void SceneDocument::toggleSelection(Entity entity) {
    if (isSelected(entity)) {
        selection_.erase(std::remove(selection_.begin(), selection_.end(), entity), selection_.end());
    } else {
        addToSelection(entity);
    }
}

void SceneDocument::selectAll() {
    selection_ = entities_;
}

// ---------------------------------------------------------------- entities

std::vector<Entity> SceneDocument::roots() const {
    std::vector<Entity> result;
    for (Entity entity : entities_) {
        const Transform* transform = engine_.scene.get<Transform>(entity);
        if (transform && !engine_.scene.isAlive(transform->parent)) result.push_back(entity);
    }
    return result;
}

std::vector<Entity> SceneDocument::children(Entity parent) const {
    std::vector<Entity> result;
    for (Entity entity : entities_) {
        const Transform* transform = engine_.scene.get<Transform>(entity);
        if (transform && transform->parent == parent) result.push_back(entity);
    }
    return result;
}

bool SceneDocument::isDescendant(Entity entity, Entity ancestor) const {
    Entity up = entity;
    for (int depth = 0; depth < 256 && engine_.scene.isAlive(up); depth++) {
        if (up == ancestor) return true;
        const Transform* transform = engine_.scene.get<Transform>(up);
        if (!transform) break;
        up = transform->parent;
    }
    return false;
}

std::string SceneDocument::nameOf(Entity entity) const {
    const Name* name = engine_.scene.get<Name>(entity);
    return name && !name->value.empty() ? name->value : std::string("Entity");
}

std::shared_ptr<Material> SceneDocument::defaultMaterial() {
    if (materials.empty()) materials.push_back(NamedMaterial{"Default", std::make_shared<Material>()});
    return materials[0].material;
}

int SceneDocument::materialIndex(const std::shared_ptr<Material>& material) const {
    for (size_t i = 0; i < materials.size(); i++) {
        if (materials[i].material == material) return int(i);
    }
    return -1;
}

ParticleEmitter SceneDocument::newParticleSystem() {
    // As a new system starts in Unity: soft white particles rising in a cone, to shape from there.
    ParticleEmitter emitter;
    emitter.rate = 20;
    emitter.lifetimeMin = 1.5f, emitter.lifetimeMax = 2.5f;
    emitter.speedMin = 1.0f, emitter.speedMax = 2.0f;
    emitter.spread = radians(25.0f);
    emitter.spawnRadius = 0.1f;
    emitter.gravity = Vec3{0, 0, 0};
    emitter.drag = 0.2f;
    emitter.sizeStart = 0.25f, emitter.sizeEnd = 0.05f;
    emitter.colorStart = Vec4{1, 1, 1, 1}, emitter.colorEnd = Vec4{1, 1, 1, 0};
    emitter.isAdditive = false;
    emitter.softness = 0.2f;
    return emitter;
}

Entity SceneDocument::create(Primitive kind, Entity parent) {
    static const char* const names[] = {"Empty", "Cube", "Sphere", "Plane", "Cylinder", "Directional Light",
                                        "Point Light", "Spot Light", "Camera", "Particles"};
    Scene& scene = engine_.scene;
    Entity entity = scene.create(names[int(kind)]);
    Transform transform;
    // In front of the camera, on the ground if the camera looks at it.
    Vec3 forward = cameraForward();
    float distance = forward.y < -0.05f ? std::min(-cameraPosition.y / forward.y, 30.0f) : 8.0f;
    Vec3 point = cameraPosition + forward * distance;
    transform.position = Vec3{std::round(point.x * 2) / 2, forward.y < -0.05f ? 0.0f : point.y, std::round(point.z * 2) / 2};
    std::shared_ptr<Material> material = defaultMaterial();
    Assets& assets = engine_.assets();
    switch (kind) {
        case Primitive::Empty:
            break;
        case Primitive::Cube:
            transform.position.y += 0.5f;
            scene.add(entity, MeshRenderer{assets.cube(), material});
            scene.add(entity, Collider::box(Vec3{0.5f, 0.5f, 0.5f}));
            break;
        case Primitive::Sphere:
            transform.position.y += 0.5f;
            scene.add(entity, MeshRenderer{assets.sphere(), material});
            scene.add(entity, Collider::sphere(0.5f));
            break;
        case Primitive::Plane: {
            // Ten meters a side, as Unity's is: a floor to put things on, and solid like one.
            transform.position.y += 0.01f;
            transform.scale = Vec3{10, 1, 10};
            scene.add(entity, MeshRenderer{assets.plane(), material});
            Collider slab;
            fitColliderToMesh(slab, assets.plane()->bounds(), transform.scale);
            scene.add(entity, slab);
            break;
        }
        case Primitive::Cylinder:
            transform.position.y += 0.5f;
            scene.add(entity, MeshRenderer{assets.cylinder(), material});
            scene.add(entity, Collider::capsule(0.5f, 0.0f));
            break;
        case Primitive::DirectionalLight: {
            transform.position.y += 4.0f;
            transform.rotation = lookRotation(Vec3{0.4f, -0.8f, -0.3f});
            Light light;
            light.type = LightType::Directional;
            light.intensity = 3.0f;
            scene.add(entity, light);
            break;
        }
        case Primitive::PointLight: {
            transform.position.y += 2.0f;
            Light light;
            light.intensity = 8;
            light.range = 8;
            scene.add(entity, light);
            break;
        }
        case Primitive::SpotLight: {
            transform.position.y += 3.0f;
            transform.rotation = lookRotation(Vec3{0, -1, -0.001f});
            Light light;
            light.type = LightType::Spot;
            light.intensity = 20;
            light.range = 12;
            scene.add(entity, light);
            break;
        }
        case Primitive::Camera: {
            transform.position = cameraPosition;
            transform.rotation = cameraRotation();          // looks where the editor looks now
            Camera camera;
            camera.isActive = scene.count<Camera>() <= 1;   // the first one becomes the game's camera
            scene.add(entity, camera);
            break;
        }
        case Primitive::ParticleEmitter: {
            transform.position.y += 0.2f;
            scene.add(entity, newParticleSystem());
            break;
        }
    }
    scene.add(entity, transform);
    entities_.push_back(entity);
    if (scene.isAlive(parent)) {
        scene.updateTransforms();
        reparent(entity, parent);
    }
    select(entity);
    markChanged();
    return entity;
}

Entity SceneDocument::createFromMesh(const std::string& meshName, Vec3 position) {
    std::shared_ptr<Mesh> mesh = engine_.assets().mesh(meshName);
    if (!mesh) return Entity{};
    Entity entity = engine_.scene.create(std::filesystem::path(meshName).stem().string());
    Transform transform;
    transform.position = position;
    transform.position.y -= mesh->bounds().min.y;          // standing on the point, not sunk into it
    engine_.scene.add(entity, transform);
    engine_.scene.add(entity, MeshRenderer{mesh, defaultMaterial()});
    entities_.push_back(entity);
    select(entity);
    markChanged();
    return entity;
}

int SceneDocument::takeOffMissingMaterials(const std::function<bool(const std::string&)>& exists) {
    if (isPlaying()) return 0;              // the game's copy of the scene: what is kept is the edited one
    int count = 0;
    std::vector<std::string> gone;
    engine_.scene.each<MeshRenderer>([&](Entity, MeshRenderer& renderer) {
        if (!renderer.material || renderer.material->assetName.empty() || exists(renderer.material->assetName)) return;
        if (std::find(gone.begin(), gone.end(), renderer.material->assetName) == gone.end()) gone.push_back(renderer.material->assetName);
        renderer.material = defaultMaterial();
        count++;
    });
    // Forgotten: a material made later under the same name is that one, not this.
    for (const std::string& name : gone) engine_.assets().reload(name);
    if (count > 0) markChanged();
    return count;
}

bool SceneDocument::savePrefab(Entity root, const std::string& absolutePath) {
    Scene& scene = engine_.scene;
    if (!scene.isAlive(root)) return false;
    std::vector<Entity> tree;
    scene.each<Transform>([&](Entity entity, Transform&) {
        if (entity == root || isDescendant(entity, root)) tree.push_back(entity);
    });
    // In the file the root sits at the origin: where a prefab goes is decided when it is used.
    Transform* transform = scene.get<Transform>(root);
    Vec3 position = transform->position;
    transform->position = Vec3{0, 0, 0};
    std::string text = writeScene(scene, materials, RenderSettings{}, &tree);
    scene.get<Transform>(root)->position = position;
    return text::writeFile(absolutePath, text);
}

Entity SceneDocument::instantiatePrefab(const std::string& assetName, Vec3 position) {
    if (!text::readFile(engine_.assets().path(assetName))) return Entity{};
    // A copy that follows the prefab: an entity of its own (named after it, where it is put)
    // that the scene keeps, with the prefab's contents put under it each time the scene is read.
    std::string name = std::filesystem::path(assetName).stem().string();
    std::optional<std::string> contents = "molten-scene 1\nentity \"" + name + "\"\n  transform 0 0 0 0 0 0 1 1 1 1\n  prefab \"" + assetName + "\"\n";
    std::vector<NamedMaterial> prefabMaterials;
    RenderSettings ignored;
    std::vector<Entity> created;
    if (!readScene(*contents, engine_.scene, prefabMaterials, ignored, sceneAssets(engine_.assets()), &created) || created.empty()) return Entity{};
    // The prefab's materials join the scene's. One the scene already has by that name is
    // used instead, so putting the same prefab in twice does not make two of each.
    for (const NamedMaterial& named : prefabMaterials) {
        std::shared_ptr<Material> kept;
        for (const NamedMaterial& existing : materials) {
            if (existing.name == named.name) kept = existing.material;
        }
        if (!kept) {
            materials.push_back(named);
            continue;
        }
        for (Entity entity : created) {
            MeshRenderer* renderer = engine_.scene.get<MeshRenderer>(entity);
            if (renderer && renderer->material == named.material) renderer->material = kept;
        }
    }
    Entity root = created.front();
    for (Entity entity : created) {
        const Transform* transform = engine_.scene.get<Transform>(entity);
        if (transform && !engine_.scene.isAlive(transform->parent)) {
            root = entity;
            break;
        }
    }
    if (Transform* transform = engine_.scene.get<Transform>(root)) transform->position = position;
    for (Entity entity : created) entities_.push_back(entity);
    engine_.scene.updateTransforms();
    select(root);
    markChanged();
    return root;
}

void SceneDocument::copyComponents(Entity from, Entity to) {
    Scene& scene = engine_.scene;
    // Components are plain values: copying an entity is copying each one.
    if (const Tag* c = scene.get<Tag>(from)) {
        Tag value = *c;
        scene.add(to, value);
    }
    if (const DisabledComponents* c = scene.get<DisabledComponents>(from)) {
        DisabledComponents value = *c;
        scene.add(to, value);
    }
    if (const MeshRenderer* c = scene.get<MeshRenderer>(from)) scene.add(to, *c);
    if (const Light* c = scene.get<Light>(from)) scene.add(to, *c);
    if (const Camera* c = scene.get<Camera>(from)) {
        Camera camera = *c;
        camera.isActive = false;
        scene.add(to, camera);
    }
    if (const Collider* c = scene.get<Collider>(from)) scene.add(to, *c);
    if (const RigidBody* c = scene.get<RigidBody>(from)) {
        RigidBody body = *c;
        body.isInitialized = false;
        scene.add(to, body);
    }
    if (const ParticleEmitter* c = scene.get<ParticleEmitter>(from)) {
        ParticleEmitter emitter = *c;
        emitter.particles.clear();
        scene.add(to, emitter);
    }
    // The game's own components, and the ones kept as text for a module that is not loaded.
    for (const ComponentType& type : ComponentRegistry::instance().types()) type.copy(scene, from, to);
    if (const UnknownComponents* c = scene.get<UnknownComponents>(from)) {
        UnknownComponents kept = *c;
        scene.add(to, kept);
    }
}

Entity SceneDocument::duplicateTree(Entity entity, Entity newParent) {
    Scene& scene = engine_.scene;
    const Transform* source = scene.get<Transform>(entity);
    if (!source) return Entity{};
    std::vector<Entity> kids = children(entity);            // before the copy joins the list
    Entity copy = scene.create(nameOf(entity));
    Transform transform = *source;
    transform.parent = newParent;
    scene.add(copy, transform);
    copyComponents(entity, copy);
    entities_.push_back(copy);
    for (Entity child : kids) duplicateTree(child, copy);
    return copy;
}

Entity SceneDocument::duplicate(Entity entity) {
    const Transform* transform = engine_.scene.get<Transform>(entity);
    if (!transform) return Entity{};
    Entity copy = duplicateTree(entity, transform->parent);
    markChanged();
    return copy;
}

std::vector<Entity> SceneDocument::selectionTops() const {
    std::vector<Entity> tops;
    for (Entity entity : selection_) {
        const Transform* transform = engine_.scene.get<Transform>(entity);
        bool underAnother = false;
        for (Entity other : selection_) {
            if (other != entity && transform && isDescendant(transform->parent, other)) underAnother = true;
        }
        if (transform && !underAnother) tops.push_back(entity);
    }
    return tops;
}

void SceneDocument::duplicateSelection() {
    // Only the top of each selected tree: a selected child of a selected parent comes along anyway.
    std::vector<Entity> tops = selectionTops();
    std::vector<Entity> copies;
    for (Entity entity : tops) {
        Entity copy = duplicate(entity);
        if (copy.isValid()) copies.push_back(copy);
    }
    if (!copies.empty()) selection_ = copies;
}

void SceneDocument::deleteSelection() {
    if (selection_.empty()) return;
    for (Entity entity : selection_) engine_.scene.destroy(entity);    // children go with their parent
    selection_.clear();
    markChanged();
}

// ---------------------------------------------------------------- copy and paste, and the Entity menu

std::string SceneDocument::copySelection() {
    Scene& scene = engine_.scene;
    std::vector<Entity> tops = selectionTops();
    if (tops.empty()) return {};
    scene.updateTransforms();
    // The trees, in the order the scene has them. A top is written where it is in the world:
    // what it was under may not be where it is pasted.
    std::vector<Entity> tree;
    for (Entity entity : entities_) {
        for (Entity top : tops) {
            if (entity == top || isDescendant(entity, top)) {
                tree.push_back(entity);
                break;
            }
        }
    }
    std::vector<Transform> kept;
    for (Entity top : tops) {
        Transform* transform = scene.get<Transform>(top);
        kept.push_back(*transform);
        decompose(transform->world, transform->position, transform->rotation, transform->scale);
        transform->parent = Entity{};
    }
    std::string text = writeScene(scene, materials, RenderSettings{}, &tree);
    for (size_t i = 0; i < tops.size(); i++) *scene.get<Transform>(tops[i]) = kept[i];
    return text;
}

bool SceneDocument::isEntityText(const std::string& text) {
    return text.rfind("molten-scene ", 0) == 0 && text.find("\nentity ") != std::string::npos;
}

std::vector<Entity> SceneDocument::paste(const std::string& text, Entity beside) {
    Scene& scene = engine_.scene;
    if (!isEntityText(text)) return {};
    std::vector<NamedMaterial> pastedMaterials;
    RenderSettings ignored;
    std::vector<Entity> created;
    if (!readScene(text, scene, pastedMaterials, ignored, sceneAssets(engine_.assets()), &created) || created.empty()) return {};
    // Materials the scene already has by that name are used; the others join it.
    for (const NamedMaterial& named : pastedMaterials) {
        std::shared_ptr<Material> existing;
        for (const NamedMaterial& mine : materials) {
            if (mine.name == named.name) existing = mine.material;
        }
        if (!existing) {
            materials.push_back(named);
            continue;
        }
        for (Entity entity : created) {
            MeshRenderer* renderer = scene.get<MeshRenderer>(entity);
            if (renderer && renderer->material == named.material) renderer->material = existing;
        }
    }
    for (Entity entity : created) entities_.push_back(entity);
    scene.updateTransforms();
    Entity parent;
    if (const Transform* place = scene.get<Transform>(beside)) parent = place->parent;
    std::vector<Entity> tops;
    for (Entity entity : created) {
        const Transform* transform = scene.get<Transform>(entity);
        if (!transform || scene.isAlive(transform->parent)) continue;
        tops.push_back(entity);
        if (scene.isAlive(parent)) reparent(entity, parent);
    }
    scene.updateTransforms();
    selection_ = tops;
    markChanged();
    return tops;
}

void SceneDocument::selectChildren() {
    std::vector<Entity> all = selection_;
    for (Entity entity : entities_) {
        if (isSelected(entity)) continue;
        for (Entity chosen : selection_) {
            if (isDescendant(entity, chosen)) {
                all.push_back(entity);
                break;
            }
        }
    }
    selection_ = all;
}

void SceneDocument::invertSelection() {
    std::vector<Entity> others;
    for (Entity entity : entities_) {
        if (!isSelected(entity)) others.push_back(entity);
    }
    selection_ = others;
}

Entity SceneDocument::groupSelection() {
    Scene& scene = engine_.scene;
    std::vector<Entity> tops = selectionTops();
    if (tops.empty()) return Entity{};
    scene.updateTransforms();
    Vec3 middle{0, 0, 0};
    for (Entity top : tops) middle += scene.get<Transform>(top)->worldPosition();
    middle = middle / float(tops.size());
    // Under what the first of them was under, where it was in the list.
    Entity parent = scene.get<Transform>(tops.front())->parent;
    Entity group = create(Primitive::Empty, parent);
    if (Name* name = scene.get<Name>(group)) name->value = "Group";
    scene.updateTransforms();
    setWorldMatrix(group, compose(middle, Quat{}, Vec3{1, 1, 1}));
    scene.updateTransforms();
    moveBeside(group, tops.front(), false);
    for (Entity top : tops) reparent(top, group);
    scene.updateTransforms();
    select(group);
    markChanged();
    return group;
}

void SceneDocument::clearParentOfSelection() {
    engine_.scene.updateTransforms();
    for (Entity top : selectionTops()) reparent(top, Entity{});
    engine_.scene.updateTransforms();
}

void SceneDocument::moveSelectionInOrder(bool first) {
    Scene& scene = engine_.scene;
    std::vector<Entity> tops = selectionTops();
    // Going first, the last of them is moved first, so that they keep their order among themselves.
    if (first) std::reverse(tops.begin(), tops.end());
    for (Entity top : tops) {
        const Transform* transform = scene.get<Transform>(top);
        if (!transform) continue;
        std::vector<Entity> siblings = scene.isAlive(transform->parent) ? children(transform->parent) : roots();
        siblings.erase(std::remove(siblings.begin(), siblings.end(), top), siblings.end());
        if (siblings.empty()) continue;
        moveBeside(top, first ? siblings.front() : siblings.back(), !first);
    }
}

void SceneDocument::toggleSelectionActive() {
    Scene& scene = engine_.scene;
    bool anyOff = false;
    for (Entity entity : selection_) {
        const Transform* transform = scene.get<Transform>(entity);
        if (transform && !transform->isActive) anyOff = true;
    }
    for (Entity entity : selection_) {
        if (Transform* transform = scene.get<Transform>(entity)) transform->isActive = anyOff;
    }
    if (!selection_.empty()) markChanged();
}

void SceneDocument::moveSelectionToView() {
    Scene& scene = engine_.scene;
    std::vector<Entity> tops = selectionTops();
    if (tops.empty()) return;
    scene.updateTransforms();
    // The middle of them goes to the view's pivot; each keeps its place around that middle.
    Vec3 middle{0, 0, 0};
    for (Entity top : tops) middle += scene.get<Transform>(top)->worldPosition();
    middle = middle / float(tops.size());
    Vec3 offset = cameraPosition + cameraForward() * orbitDistance - middle;
    for (Entity top : tops) {
        const Transform* transform = scene.get<Transform>(top);
        Mat4 world = transform->world;
        world.columns[3].x += offset.x;
        world.columns[3].y += offset.y;
        world.columns[3].z += offset.z;
        setWorldMatrix(top, world);
    }
    scene.updateTransforms();
    markChanged();
}

void SceneDocument::setWorldMatrix(Entity entity, const Mat4& world) {
    Scene& scene = engine_.scene;
    Transform* transform = scene.get<Transform>(entity);
    if (!transform) return;
    // Transform holds what is relative to the parent: take the parent out again.
    Mat4 local = world;
    if (const Transform* parent = scene.get<Transform>(transform->parent)) local = simd::inverse(parent->world) * world;
    decompose(local, transform->position, transform->rotation, transform->scale);
    transform->world = world;
    // While the game runs, physics decides where a body is: tell it, or the entity would
    // snap back on the next step.
    if (RigidBody* body = scene.get<RigidBody>(entity); body && isPlaying_) {
        body->position = transform->position;
        body->previousPosition = transform->position;
        body->velocity = Vec3{0, 0, 0};
    }
}

bool SceneDocument::reparent(Entity child, Entity newParent) {
    Scene& scene = engine_.scene;
    Transform* transform = scene.get<Transform>(child);
    if (!transform || child == newParent) return false;
    if (scene.isAlive(newParent) && isDescendant(newParent, child)) return false;     // a loop
    if (transform->parent == newParent) return true;
    Mat4 world = transform->world;
    transform->parent = scene.isAlive(newParent) ? newParent : Entity{};
    setWorldMatrix(child, world);       // same place in the world, new numbers relative to the new parent
    markChanged();
    return true;
}

bool SceneDocument::moveBeside(Entity entity, Entity sibling, bool after) {
    Scene& scene = engine_.scene;
    if (!scene.isAlive(entity) || !scene.isAlive(sibling) || entity == sibling) return false;
    const Transform* place = scene.get<Transform>(sibling);
    if (!place || !reparent(entity, place->parent)) return false;
    size_t from = scene.orderOf(entity), to = scene.orderOf(sibling);
    // Taking it out first moves everything after it one place back.
    if (from < to) to = after ? to : to - 1;
    else to = after ? to + 1 : to;
    scene.setOrder(entity, to);
    markChanged();
    return true;
}

void SceneDocument::frameSelection() {
    Scene& scene = engine_.scene;
    AABB bounds;
    bool any = false;
    for (Entity entity : selection_) {
        const Transform* transform = scene.get<Transform>(entity);
        if (!transform) continue;
        const MeshRenderer* renderer = scene.get<MeshRenderer>(entity);
        if (renderer && renderer->mesh) {
            const AABB& box = renderer->mesh->bounds();
            for (int i = 0; i < 8; i++) {
                Vec3 corner{i & 1 ? box.max.x : box.min.x, i & 2 ? box.max.y : box.min.y, i & 4 ? box.max.z : box.min.z};
                bounds.expand(transformPoint(transform->world, corner));
            }
        } else {
            bounds.expand(transform->worldPosition() - Vec3{0.5f, 0.5f, 0.5f});
            bounds.expand(transform->worldPosition() + Vec3{0.5f, 0.5f, 0.5f});
        }
        any = true;
    }
    if (!any) return;
    float radius = std::max(simd::length(bounds.halfExtents()), 0.5f);
    // Far enough for a sphere of that radius to fit the camera's 60 degree view.
    orbitDistance = radius / std::sin(radians(30.0f)) * 1.1f;
    cameraPosition = bounds.center() - cameraForward() * orbitDistance;
    syncCamera();
}

// ---------------------------------------------------------------- undo

bool SceneDocument::recover(const std::string& sceneText) {
    Scene probe;
    std::vector<NamedMaterial> probeMaterials;
    RenderSettings probeSettings;
    if (isPlaying_ || !readScene(sceneText, probe, probeMaterials, probeSettings, SceneAssets{})) return false;
    undo_.push_back(std::move(current_));
    redo_.clear();
    selection_.clear();
    restore(sceneText);
    current_ = snapshot();
    return true;
}

void SceneDocument::undo() {
    if (!canUndo()) return;
    redo_.push_back(std::move(current_));
    current_ = std::move(undo_.back());
    undo_.pop_back();
    restore(current_);
}

void SceneDocument::redo() {
    if (!canRedo()) return;
    undo_.push_back(std::move(current_));
    current_ = std::move(redo_.back());
    redo_.pop_back();
    restore(current_);
}

// ---------------------------------------------------------------- play

Entity SceneDocument::gameCamera() const {
    Entity found;
    Scene& scene = engine_.scene;
    scene.each<Camera, Transform>([&](Entity entity, Camera& camera, Transform&) {
        if (!found.isValid() && camera.isActive && !scene.has<EditorOnly>(entity)) found = entity;
    });
    return found;
}

/// Play runs the scene: physics starts and the scene's own camera takes over. Stop puts
/// everything back exactly as it was, from the text the scene was written to in memory.
void SceneDocument::setPlaying(bool playing) {
    if (playing == isPlaying_) return;
    Scene& scene = engine_.scene;
    if (playing) {
        scene.updateTransforms();
        beforePlay_ = snapshot();
        isPlaying_ = true;
        engine_.physics.reset();
        // The game may start the scene over, or go to another: it returns to what is open
        // here, saved or not, and the editor's camera is put back into whatever it loads.
        engine_.noteScene(filePath.empty() ? std::string("Untitled") : std::filesystem::path(filePath).filename().string(), beforePlay_);
        stepIsRunning_ = false;
        engine_.onSceneLoaded = [this] {
            if (!isPlaying_) return;
            createCamera();
            selection_.clear();
        };
        engine_.isSimulationPaused = false;
        engine_.restartComponents();        // every component's start() runs again
        engine_.runsComponents = true;
    } else {
        isPlaying_ = false;
        engine_.onSceneLoaded = nullptr;
        engine_.endScene();                 // components hear onDestroy; the run's sounds, tasks and pools end with it
        engine_.clearValues();              // the score of one run is not the next one's
        engine_.isSimulationPaused = true;
        engine_.runsComponents = false;
        engine_.time.timeScale = 1.0f;      // slow motion the game asked for ends with it
        engine_.applySettings(engine_.settings);   // and gravity is the project's again, whatever the game did to it
        restore(beforePlay_);
    }
}

void SceneDocument::stepFrame() {
    if (!isPlaying_) return;
    setPaused(false);
    stepIsRunning_ = true;                  // beginFrame pauses again once the frame has run
}

void SceneDocument::alignSelectionWithView() {
    if (selection_.empty()) return;
    engine_.scene.updateTransforms();
    for (Entity entity : selection_) {
        const Transform* transform = engine_.scene.get<Transform>(entity);
        if (!transform) continue;
        // Its size stays; where it is and which way it faces become the view's.
        Vec3 position, scale;
        Quat rotation;
        decompose(transform->world, position, rotation, scale);
        setWorldMatrix(entity, compose(cameraPosition, cameraRotation(), scale));
    }
    engine_.scene.updateTransforms();
    markChanged();
}

void SceneDocument::alignViewWithSelection() {
    const Transform* transform = engine_.scene.get<Transform>(primary());
    if (!transform) return;
    Vec3 position, scale;
    Quat rotation;
    decompose(transform->world, position, rotation, scale);
    Vec3 angles = toEulerAngles(rotation);  // pitch, yaw, roll: the view has no roll
    cameraPosition = position;
    cameraPitch = std::clamp(angles.x, -1.55f, 1.55f);
    cameraYaw = angles.y;
    syncCamera();
}

void SceneDocument::setPaused(bool paused) {
    if (!isPlaying_) return;
    engine_.isSimulationPaused = paused;
    engine_.runsComponents = !paused;
}

/// Replaces the game's code under an open scene. The scene is written to text, emptied
/// down to its containers (they were made by the old code), `swap` loads the new code,
/// and the text is read back: components come back by name, with the fields the new
/// code has. Undo history is text too, so it survives.
///
/// A field that was left as the code gave it follows the code: change `float speed = 0;`
/// to `= 1` in a script and every component that still had 0 has 1. A field the scene set
/// to something else keeps what the scene says.
void SceneDocument::reloadGameCode(const std::function<void()>& swap) {
    if (isPlaying_) setPlaying(false);
    engine_.scene.updateTransforms();
    std::string text = snapshot();
    // What each field starts as, by type and field name, as the code of the moment says.
    using Defaults = std::unordered_map<std::string, std::unordered_map<std::string, std::string>>;
    auto defaultsNow = [] {
        Defaults all;
        Scene scratch;
        Entity holder = scratch.create("defaults");
        for (const ComponentType& type : ComponentRegistry::instance().types()) {
            if (!type.add || type.fields.empty()) continue;
            void* component = type.add(scratch, holder);
            if (!component) continue;
            for (const Field& field : type.fields) all[type.name][field.name] = field.valueText(component);
        }
        scratch.clear();
        scratch.dropStorages();         // made by the code that is about to go
        return all;
    };
    Defaults before = defaultsNow();
    engine_.scene.clear();
    engine_.scene.dropStorages();
    engine_.physics.reset();
    swap();
    restore(text);
    Defaults after_ = defaultsNow();
    for (const ComponentType& type : ComponentRegistry::instance().types()) {
        auto was = before.find(type.name), is = after_.find(type.name);
        if (was == before.end() || is == after_.end() || !type.collect || !type.get) continue;
        std::vector<Entity> holders;
        type.collect(engine_.scene, holders);
        for (const Field& field : type.fields) {
            auto old = was->second.find(field.name), fresh = is->second.find(field.name);
            if (old == was->second.end() || fresh == is->second.end() || old->second == fresh->second) continue;
            for (Entity entity : holders) {
                void* component = type.get(engine_.scene, entity);
                if (component && field.valueText(component) == old->second) field.setValueText(component, fresh->second);
            }
        }
    }
    // What a scene says can change with the code (a field added or removed): that is not an edit.
    std::string after = snapshot();
    if (current_ == saved_) saved_ = after;
    current_ = after;
}

// ---------------------------------------------------------------- once per frame

void SceneDocument::beginFrame() {
    Scene& scene = engine_.scene;
    entities_.clear();
    scene.each<Transform>([&](Entity entity, Transform&) {
        if (!scene.has<EditorOnly>(entity)) entities_.push_back(entity);
    });
    selection_.erase(std::remove_if(selection_.begin(), selection_.end(), [&](Entity entity) { return !scene.isAlive(entity); }),
                     selection_.end());
    syncCamera();
    // A single step has had its frame.
    if (stepIsRunning_) {
        stepIsRunning_ = false;
        setPaused(true);
    }
}

void SceneDocument::endFrame(bool interacting) {
    if (!isPlaying_) keepCollidersFitted();
    // What was created or moved in this frame is drawn in this frame.
    engine_.removeDestroyed();
    engine_.scene.updateTransforms();
    if (!touched_ || isPlaying_ || interacting) return;
    touched_ = false;
    std::string state = snapshot();
    if (state == current_) return;
    undo_.push_back(std::move(current_));
    if (undo_.size() > kUndoLevels) undo_.erase(undo_.begin());
    redo_.clear();
    current_ = std::move(state);
}

}  // namespace molten::editor
