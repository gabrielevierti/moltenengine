#include "Scene/SceneFile.hpp"

#include "Game/Component.hpp"

#include "Core/Log.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace molten {

namespace {

// ---------------------------------------------------------------- writing

/// Builds one line: a keyword followed by values separated by spaces.
struct Line {
    std::string text;

    explicit Line(const char* keyword, int indent = 1) : text(std::string(size_t(indent) * 2, ' ') + keyword) {}

    Line& number(float value) {
        // The shortest text that reads back as exactly the same float: 0.05, not 0.0500000007.
        char buffer[32];
        for (int digits = 6; digits <= 9; digits++) {
            std::snprintf(buffer, sizeof(buffer), "%.*g", digits, double(value));
            if (std::strtof(buffer, nullptr) == value) break;
        }
        text += " ";
        text += buffer;
        return *this;
    }
    Line& integer(long value) {
        text += " " + std::to_string(value);
        return *this;
    }
    Line& flag(bool value) { return integer(value ? 1 : 0); }
    Line& vec(Vec2 v) { return number(v.x).number(v.y); }
    Line& vec(Vec3 v) { return number(v.x).number(v.y).number(v.z); }
    Line& vec(Vec4 v) { return number(v.x).number(v.y).number(v.z).number(v.w); }
    Line& word(const char* value) {
        text += " ";
        text += value;
        return *this;
    }
    /// A string in quotes, so that it may contain spaces.
    Line& string(const std::string& value) {
        text += " \"";
        for (char c : value) {
            if (c == '"' || c == '\\') text += '\\';
            if (c != '\n' && c != '\r') text += c;
        }
        text += "\"";
        return *this;
    }
    void to(std::string& out) const { out += text + "\n"; }
};

const char* const kLightTypes[] = {"directional", "point", "spot"};
const char* const kBodyTypes[] = {"static", "kinematic", "dynamic"};
const char* const kShapes[] = {"sphere", "box", "capsule"};
const char* const kAlphaModes[] = {"opaque", "mask", "blend"};

void writeTexture(std::string& out, const char* keyword, const std::shared_ptr<Texture>& texture) {
    if (texture && !texture->name.empty()) Line(keyword).string(texture->name).to(out);
}

// ---------------------------------------------------------------- reading

/// One line split into words. Quoted strings count as one word.
struct Words {
    std::vector<std::string> items;
    size_t next = 1;                    // items[0] is the keyword

    explicit Words(const std::string& line) {
        size_t i = 0;
        while (i < line.size()) {
            if (line[i] == ' ' || line[i] == '\t' || line[i] == '\r') {
                i++;
            } else if (line[i] == '"') {
                std::string word;
                i++;
                while (i < line.size() && line[i] != '"') {
                    if (line[i] == '\\' && i + 1 < line.size()) i++;
                    word += line[i++];
                }
                i++;
                items.push_back(word);
            } else {
                size_t start = i;
                while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') i++;
                items.push_back(line.substr(start, i - start));
            }
        }
    }

    const std::string& keyword() const {
        static const std::string empty;
        return items.empty() ? empty : items[0];
    }
    bool hasMore() const { return next < items.size(); }
    std::string string() { return hasMore() ? items[next++] : std::string(); }
    float number(float fallback = 0.0f) { return hasMore() ? std::strtof(items[next++].c_str(), nullptr) : fallback; }
    long integer(long fallback = 0) { return hasMore() ? std::strtol(items[next++].c_str(), nullptr, 10) : fallback; }
    bool flag(bool fallback = false) { return integer(fallback ? 1 : 0) != 0; }
    Vec2 vec2(Vec2 d = Vec2{0, 0}) { float x = number(d.x), y = number(d.y); return Vec2{x, y}; }
    Vec3 vec3(Vec3 d = Vec3{0, 0, 0}) { float x = number(d.x), y = number(d.y), z = number(d.z); return Vec3{x, y, z}; }
    Vec4 vec4(Vec4 d = Vec4{0, 0, 0, 0}) { float x = number(d.x), y = number(d.y), z = number(d.z), w = number(d.w); return Vec4{x, y, z, w}; }
    /// The position of the next word in a list of names, or `fallback`.
    int choice(const char* const* names, int count, int fallback) {
        std::string word = string();
        for (int i = 0; i < count; i++) {
            if (word == names[i]) return i;
        }
        return fallback;
    }
};

}  // namespace

std::string writeScene(Scene& scene, const std::vector<NamedMaterial>& materials, const RenderSettings& settings) {
    std::string out = "molten-scene 1\n";

    // ---- settings: the look of the scene
    out += "settings\n";
    Line("skyZenith").vec(settings.sky.zenithColor).to(out);
    Line("skyHorizon").vec(settings.sky.horizonColor).to(out);
    Line("skyGround").vec(settings.sky.groundColor).to(out);
    Line("skyIntensity").number(settings.sky.intensity).to(out);
    Line("environmentIntensity").number(settings.environmentIntensity).to(out);
    Line("fog").vec(settings.fogColor).number(settings.fogDensity).to(out);
    Line("shadows").flag(settings.shadowsEnabled).number(settings.cascades.shadowDistance).to(out);
    Line("rayTracedShadows").flag(settings.rayTracedShadows).integer(settings.shadowRayCount).to(out);
    Line("ambientOcclusion").flag(settings.ambientOcclusionEnabled).number(settings.ambientOcclusionRadius)
        .number(settings.ambientOcclusionIntensity).to(out);
    Line("bloom").flag(settings.bloomEnabled).number(settings.bloomIntensity).number(settings.bloomThreshold).to(out);
    Line("exposure").number(settings.exposure).to(out);
    Line("toneMapper").integer(settings.toneMapper).to(out);
    Line("vignette").number(settings.vignette).to(out);
    Line("clouds").number(settings.sky.cloudCoverage).number(settings.sky.cloudScale).to(out);
    Line("lens").number(settings.lensDistortion).number(settings.chromaticAberration).number(settings.filmGrain).to(out);

    // ---- materials: the named ones, then any other that an entity uses
    std::vector<NamedMaterial> all = materials;
    std::unordered_map<const Material*, int> materialIndex;
    for (size_t i = 0; i < all.size(); i++) materialIndex[all[i].material.get()] = int(i);
    scene.each<MeshRenderer, Transform>([&](Entity entity, MeshRenderer& renderer, Transform&) {
        if (scene.has<EditorOnly>(entity) || !renderer.material || materialIndex.count(renderer.material.get())) return;
        materialIndex[renderer.material.get()] = int(all.size());
        all.push_back(NamedMaterial{"Material " + std::to_string(all.size() + 1), renderer.material});
    });
    for (const NamedMaterial& named : all) {
        const Material& m = *named.material;
        Line("material", 0).string(named.name).to(out);
        Line("baseColor").vec(m.baseColor).to(out);
        Line("metallic").number(m.metallic).to(out);
        Line("roughness").number(m.roughness).to(out);
        Line("emissive").vec(m.emissive).to(out);
        Line("normalScale").number(m.normalScale).to(out);
        Line("occlusionStrength").number(m.occlusionStrength).to(out);
        Line("alpha").word(kAlphaModes[int(m.alphaMode)]).number(m.alphaCutoff).to(out);
        Line("uvScale").vec(m.uvScale).to(out);
        Line("doubleSided").flag(m.isDoubleSided).to(out);
        Line("unlit").flag(m.isUnlit).to(out);
        writeTexture(out, "baseColorTexture", m.baseColorTexture);
        writeTexture(out, "normalTexture", m.normalTexture);
        writeTexture(out, "metallicRoughnessTexture", m.metallicRoughnessTexture);
        writeTexture(out, "occlusionTexture", m.occlusionTexture);
        writeTexture(out, "emissiveTexture", m.emissiveTexture);
    }

    // ---- entities, in the order the scene stores their transforms. A parent is written
    // as its position in this list.
    std::vector<Entity> entities;
    std::unordered_map<uint32_t, int> fileIndex;
    scene.each<Transform>([&](Entity entity, Transform&) {
        if (scene.has<EditorOnly>(entity)) return;
        fileIndex[entity.index] = int(entities.size());
        entities.push_back(entity);
    });
    for (Entity entity : entities) {
        const Name* name = scene.get<Name>(entity);
        Line("entity", 0).string(name ? name->value : std::string()).to(out);

        const Transform& t = *scene.get<Transform>(entity);
        Line("transform").vec(t.position).vec(t.rotation.vector).vec(t.scale).to(out);
        if (scene.isAlive(t.parent) && fileIndex.count(t.parent.index)) Line("parent").integer(fileIndex[t.parent.index]).to(out);

        if (const Camera* c = scene.get<Camera>(entity)) {
            Line("camera").number(c->fovY).number(c->nearPlane).number(c->farPlane).flag(c->isOrthographic)
                .number(c->orthographicHeight).flag(c->isActive).to(out);
        }
        if (const Light* l = scene.get<Light>(entity)) {
            Line("light").word(kLightTypes[int(l->type)]).vec(l->color).number(l->intensity).number(l->range)
                .number(l->innerConeAngle).number(l->outerConeAngle).flag(l->castsShadows).to(out);
        }
        if (const MeshRenderer* r = scene.get<MeshRenderer>(entity)) {
            int material = r->material ? materialIndex[r->material.get()] : -1;
            Line("mesh").string(r->mesh ? r->mesh->name : std::string()).integer(material).vec(r->tint)
                .flag(r->castsShadows).flag(r->isVisible).to(out);
        }
        if (const Collider* c = scene.get<Collider>(entity)) {
            Line line("collider");
            line.word(kShapes[int(c->shape)]);
            if (c->shape == ColliderShape::Box) line.vec(c->halfExtents);
            else if (c->shape == ColliderShape::Sphere) line.number(c->radius);
            else line.number(c->radius).number(c->halfHeight);
            line.vec(c->offset).flag(c->isTrigger).to(out);
        }
        if (const RigidBody* b = scene.get<RigidBody>(entity)) {
            Line("body").word(kBodyTypes[int(b->type)]).number(b->mass).number(b->restitution).number(b->friction)
                .number(b->linearDamping).number(b->gravityScale).to(out);
        }
        if (const ParticleEmitter* e = scene.get<ParticleEmitter>(entity)) {
            Line("particles").number(e->rate).number(e->lifetimeMin).number(e->lifetimeMax).number(e->speedMin).number(e->speedMax)
                .vec(e->direction).number(e->spread).number(e->spawnRadius).vec(e->gravity).number(e->drag).to(out);
            Line("particleLook").number(e->sizeStart).number(e->sizeEnd).vec(e->colorStart).vec(e->colorEnd)
                .flag(e->isAdditive).flag(e->isEmitting).integer(e->maxParticles).to(out);
        }

        // The game's own components: the type's name, then one line per field.
        //     component "Spinner"
        //       field turnsPerSecond "0.5"
        for (const ComponentType& type : ComponentRegistry::instance().types()) {
            void* component = type.get(scene, entity);
            if (!component) continue;
            Line("component").string(type.name).to(out);
            for (const Field& field : type.fields) Line("field", 2).word(field.name.c_str()).string(field.valueText(component)).to(out);
        }
        // And those of types nobody has declared, exactly as they were read.
        if (const UnknownComponents* unknown = scene.get<UnknownComponents>(entity)) {
            for (const std::string& kept : unknown->lines) out += kept + "\n";
        }
    }
    return out;
}

bool readScene(const std::string& text, Scene& scene, std::vector<NamedMaterial>& materials, RenderSettings& settings,
               const SceneAssets& assets) {
    std::istringstream stream(text);
    std::string line;
    // "learnmetal-scene" is what the engine wrote before it had this name: still read.
    std::string header = std::getline(stream, line) ? std::string(Words(line).keyword()) : std::string();
    if (header != "molten-scene" && header != "learnmetal-scene") {
        logError("not a scene file: the first line must be \"molten-scene 1\"");
        return false;
    }

    enum class Block { None, Settings, Material, Entity };
    Block block = Block::None;
    materials.clear();
    std::vector<Entity> entities;
    std::vector<std::pair<Entity, long>> parents;       // resolved at the end: a parent may come later in the file
    Entity entity;
    Material* material = nullptr;
    // The game component the following "field" lines belong to. For a type nobody has
    // declared the lines are kept as text instead.
    const ComponentType* componentType = nullptr;
    bool keepsComponentLines = false;

    auto loadTexture = [&](Words& words, TextureFormat format) -> std::shared_ptr<Texture> {
        std::string name = words.string();
        return assets.texture && !name.empty() ? assets.texture(name, format) : nullptr;
    };

    while (std::getline(stream, line)) {
        Words words(line);
        const std::string& key = words.keyword();
        if (key.empty() || key[0] == '#') continue;

        // ---- the lines that start a block
        if (key == "settings") {
            block = Block::Settings;
        } else if (key == "material") {
            block = Block::Material;
            materials.push_back(NamedMaterial{words.string(), std::make_shared<Material>()});
            material = materials.back().material.get();
        } else if (key == "entity") {
            block = Block::Entity;
            componentType = nullptr;
            keepsComponentLines = false;
            entity = scene.create(words.string());
            scene.add(entity, Transform{});
            entities.push_back(entity);

        // ---- settings
        } else if (block == Block::Settings) {
            if (key == "skyZenith") settings.sky.zenithColor = words.vec3();
            else if (key == "skyHorizon") settings.sky.horizonColor = words.vec3();
            else if (key == "skyGround") settings.sky.groundColor = words.vec3();
            else if (key == "skyIntensity") settings.sky.intensity = words.number(1);
            else if (key == "environmentIntensity") settings.environmentIntensity = words.number(1);
            else if (key == "fog") {
                settings.fogColor = words.vec3();
                settings.fogDensity = words.number();
            } else if (key == "shadows") {
                settings.shadowsEnabled = words.flag(true);
                settings.cascades.shadowDistance = words.number(60);
            } else if (key == "rayTracedShadows") {
                settings.rayTracedShadows = words.flag(false);
                settings.shadowRayCount = int(words.integer(4));
            } else if (key == "ambientOcclusion") {
                settings.ambientOcclusionEnabled = words.flag(true);
                settings.ambientOcclusionRadius = words.number(0.6f);
                settings.ambientOcclusionIntensity = words.number(1);
            } else if (key == "bloom") {
                settings.bloomEnabled = words.flag(true);
                settings.bloomIntensity = words.number(0.06f);
                settings.bloomThreshold = words.number(1);
            } else if (key == "exposure") settings.exposure = words.number(1);
            else if (key == "toneMapper") settings.toneMapper = int(words.integer());
            else if (key == "vignette") settings.vignette = words.number();
            else if (key == "clouds") {
                settings.sky.cloudCoverage = words.number();
                settings.sky.cloudScale = words.number(1);
            } else if (key == "lens") {
                settings.lensDistortion = words.number();
                settings.chromaticAberration = words.number();
                settings.filmGrain = words.number();
            }

        // ---- a material
        } else if (block == Block::Material && material) {
            if (key == "baseColor") material->baseColor = words.vec4(Vec4{1, 1, 1, 1});
            else if (key == "metallic") material->metallic = words.number();
            else if (key == "roughness") material->roughness = words.number(0.6f);
            else if (key == "emissive") material->emissive = words.vec3();
            else if (key == "normalScale") material->normalScale = words.number(1);
            else if (key == "occlusionStrength") material->occlusionStrength = words.number(1);
            else if (key == "alpha") {
                material->alphaMode = AlphaMode(words.choice(kAlphaModes, 3, 0));
                material->alphaCutoff = words.number(0.5f);
            } else if (key == "uvScale") material->uvScale = words.vec2(Vec2{1, 1});
            else if (key == "doubleSided") material->isDoubleSided = words.flag();
            else if (key == "unlit") material->isUnlit = words.flag();
            // Colors are stored sRGB-encoded; everything else is data and must not be converted.
            else if (key == "baseColorTexture") material->baseColorTexture = loadTexture(words, TextureFormat::RGBA8_sRGB);
            else if (key == "emissiveTexture") material->emissiveTexture = loadTexture(words, TextureFormat::RGBA8_sRGB);
            else if (key == "normalTexture") material->normalTexture = loadTexture(words, TextureFormat::RGBA8);
            else if (key == "metallicRoughnessTexture") material->metallicRoughnessTexture = loadTexture(words, TextureFormat::RGBA8);
            else if (key == "occlusionTexture") material->occlusionTexture = loadTexture(words, TextureFormat::RGBA8);

        // ---- an entity's components
        } else if (block == Block::Entity) {
            if (key == "component") {
                std::string typeName = words.string();
                componentType = ComponentRegistry::instance().find(typeName);
                if (componentType) componentType->add(scene, entity);
                keepsComponentLines = componentType == nullptr;
                if (keepsComponentLines) {
                    if (!scene.has<UnknownComponents>(entity)) scene.add(entity, UnknownComponents{});
                    scene.get<UnknownComponents>(entity)->lines.push_back(line);
                }
            } else if (key == "field") {
                if (keepsComponentLines) {
                    scene.get<UnknownComponents>(entity)->lines.push_back(line);
                } else if (void* component = componentType ? componentType->get(scene, entity) : nullptr) {
                    std::string fieldName = words.string();
                    // A field the code no longer has is dropped; one the file does not have keeps its default.
                    if (const Field* field = componentType->findField(fieldName)) field->setValueText(component, words.string());
                }
            } else if (key == "transform") {
                Transform& t = *scene.get<Transform>(entity);
                t.position = words.vec3();
                Vec4 q = words.vec4(Vec4{0, 0, 0, 1});
                // A hand-edited rotation may not have length 1; one we wrote does, and must
                // come back bit for bit.
                float lengthSquared = simd::dot(q, q);
                t.rotation = std::abs(lengthSquared - 1.0f) < 1e-4f ? Quat(q.x, q.y, q.z, q.w)
                           : lengthSquared > 1e-8f ? simd::normalize(Quat(q.x, q.y, q.z, q.w)) : Quat{};
                t.scale = words.vec3(Vec3{1, 1, 1});
            } else if (key == "parent") {
                parents.push_back({entity, words.integer(-1)});
            } else if (key == "camera") {
                Camera c;
                c.fovY = words.number(c.fovY);
                c.nearPlane = words.number(c.nearPlane);
                c.farPlane = words.number(c.farPlane);
                c.isOrthographic = words.flag();
                c.orthographicHeight = words.number(c.orthographicHeight);
                c.isActive = words.flag(true);
                scene.add(entity, c);
            } else if (key == "light") {
                Light l;
                l.type = LightType(words.choice(kLightTypes, 3, 1));
                l.color = words.vec3(Vec3{1, 1, 1});
                l.intensity = words.number(1);
                l.range = words.number(l.range);
                l.innerConeAngle = words.number(l.innerConeAngle);
                l.outerConeAngle = words.number(l.outerConeAngle);
                l.castsShadows = words.flag();
                scene.add(entity, l);
            } else if (key == "mesh") {
                MeshRenderer r;
                std::string name = words.string();
                if (assets.mesh && !name.empty()) r.mesh = assets.mesh(name);
                long index = words.integer(-1);
                if (index >= 0 && size_t(index) < materials.size()) r.material = materials[size_t(index)].material;
                r.tint = words.vec4(Vec4{1, 1, 1, 1});
                r.castsShadows = words.flag(true);
                r.isVisible = words.flag(true);
                scene.add(entity, r);
            } else if (key == "collider") {
                Collider c;
                c.shape = ColliderShape(words.choice(kShapes, 3, 1));
                if (c.shape == ColliderShape::Box) {
                    c.halfExtents = words.vec3(c.halfExtents);
                } else if (c.shape == ColliderShape::Sphere) {
                    c.radius = words.number(c.radius);
                } else {
                    c.radius = words.number(c.radius);
                    c.halfHeight = words.number(c.halfHeight);
                }
                c.offset = words.vec3();
                c.isTrigger = words.flag();
                scene.add(entity, c);
            } else if (key == "body") {
                RigidBody b;
                b.type = BodyType(words.choice(kBodyTypes, 3, 2));
                b.mass = words.number(b.mass);
                b.restitution = words.number(b.restitution);
                b.friction = words.number(b.friction);
                b.linearDamping = words.number(b.linearDamping);
                b.gravityScale = words.number(b.gravityScale);
                scene.add(entity, b);
            } else if (key == "particles") {
                ParticleEmitter e;
                e.rate = words.number(e.rate);
                e.lifetimeMin = words.number(e.lifetimeMin);
                e.lifetimeMax = words.number(e.lifetimeMax);
                e.speedMin = words.number(e.speedMin);
                e.speedMax = words.number(e.speedMax);
                e.direction = words.vec3(e.direction);
                e.spread = words.number(e.spread);
                e.spawnRadius = words.number(e.spawnRadius);
                e.gravity = words.vec3(e.gravity);
                e.drag = words.number(e.drag);
                scene.add(entity, e);
            } else if (key == "particleLook") {
                if (ParticleEmitter* e = scene.get<ParticleEmitter>(entity)) {
                    e->sizeStart = words.number(e->sizeStart);
                    e->sizeEnd = words.number(e->sizeEnd);
                    e->colorStart = words.vec4(e->colorStart);
                    e->colorEnd = words.vec4(e->colorEnd);
                    e->isAdditive = words.flag(true);
                    e->isEmitting = words.flag(true);
                    e->maxParticles = int(words.integer(e->maxParticles));
                }
            }
        }
    }

    for (const auto& [child, index] : parents) {
        if (index >= 0 && size_t(index) < entities.size() && entities[size_t(index)] != child) {
            scene.get<Transform>(child)->parent = entities[size_t(index)];
        }
    }
    return true;
}

bool saveSceneFile(const std::string& path, Scene& scene, const std::vector<NamedMaterial>& materials, const RenderSettings& settings) {
    std::ofstream file(path, std::ios::binary);
    if (file) file << writeScene(scene, materials, settings);
    if (!file) {
        logError("cannot write scene %s", path.c_str());
        return false;
    }
    logInfo("saved %s", path.c_str());
    return true;
}

bool loadSceneFile(const std::string& path, Scene& scene, std::vector<NamedMaterial>& materials, RenderSettings& settings,
                   const SceneAssets& assets) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        logError("cannot read scene %s", path.c_str());
        return false;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return readScene(contents.str(), scene, materials, settings, assets);
}

}  // namespace molten
