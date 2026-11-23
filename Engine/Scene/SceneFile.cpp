#include "Scene/SceneFile.hpp"

#include "Game/Component.hpp"
#include "Water/Water.hpp"

#include <algorithm>

#include "Core/Log.hpp"
#include "Core/TextFile.hpp"

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

const char* const kLightTypes[] = {"directional", "point", "spot", "area"};
const char* const kBodyTypes[] = {"static", "kinematic", "dynamic"};
const char* const kShapes[] = {"sphere", "box", "capsule", "mesh"};
const char* const kAlphaModes[] = {"opaque", "mask", "blend"};

/// The properties of a material, one per line: the same in a scene and in a .material file.
void writeMaterialProperties(std::string& out, const Material& m, int indent) {
    Line("baseColor", indent).vec(m.baseColor).to(out);
    Line("metallic", indent).number(m.metallic).to(out);
    Line("roughness", indent).number(m.roughness).to(out);
    Line("emissive", indent).vec(m.emissive).to(out);
    Line("normalScale", indent).number(m.normalScale).to(out);
    Line("occlusionStrength", indent).number(m.occlusionStrength).to(out);
    Line("alpha", indent).word(kAlphaModes[int(m.alphaMode)]).number(m.alphaCutoff).to(out);
    Line("uvScale", indent).vec(m.uvScale).to(out);
    Line("doubleSided", indent).flag(m.isDoubleSided).to(out);
    Line("unlit", indent).flag(m.isUnlit).to(out);
    if (m.sway > 0) Line("sway", indent).number(m.sway).to(out);
    if (!m.shader.empty()) {
        Line("surfaceShader", indent).string(m.shader).to(out);
        for (const Material::ShaderValue& item : m.shaderValues) Line("shaderValue", indent).word(item.name.c_str()).vec(item.value).to(out);
    }
    auto texture = [&](const char* keyword, const std::shared_ptr<Texture>& value) {
        if (value && !value->name.empty()) Line(keyword, indent).string(value->name).to(out);
    };
    texture("baseColorTexture", m.baseColorTexture);
    texture("normalTexture", m.normalTexture);
    texture("metallicRoughnessTexture", m.metallicRoughnessTexture);
    texture("occlusionTexture", m.occlusionTexture);
    texture("emissiveTexture", m.emissiveTexture);
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

using TextureLoader = std::function<std::shared_ptr<Texture>(const std::string&, TextureFormat)>;

/// One property line of a material. Lines it does not know are skipped.
void readMaterialProperty(Words& words, Material& material, const TextureLoader& loader) {
    const std::string& key = words.keyword();
    auto texture = [&](TextureFormat format) -> std::shared_ptr<Texture> {
        std::string name = words.string();
        return loader && !name.empty() ? loader(name, format) : nullptr;
    };
    if (key == "baseColor") material.baseColor = words.vec4(Vec4{1, 1, 1, 1});
    else if (key == "metallic") material.metallic = words.number();
    else if (key == "roughness") material.roughness = words.number(0.6f);
    else if (key == "emissive") material.emissive = words.vec3();
    else if (key == "normalScale") material.normalScale = words.number(1);
    else if (key == "occlusionStrength") material.occlusionStrength = words.number(1);
    else if (key == "alpha") {
        material.alphaMode = AlphaMode(words.choice(kAlphaModes, 3, 0));
        material.alphaCutoff = words.number(0.5f);
    } else if (key == "uvScale") material.uvScale = words.vec2(Vec2{1, 1});
    else if (key == "doubleSided") material.isDoubleSided = words.flag();
    else if (key == "unlit") material.isUnlit = words.flag();
    // Water as a material, before the scene's sea: what it was on becomes the sea (retireOldWater).
    else if (key == "waves") {
        material.wasWater = true;
        material.waveSize = words.number(1);
        material.waveHeight = words.number(1);
        material.foam = words.number(1);
    } else if (key == "water") {
        material.wasWater = true;
        float fineness = words.number(1);
        material.waveSize = 1.0f / std::clamp(fineness, 0.05f, 20.0f);
        material.waveHeight = words.number(1);
    }
    else if (key == "sway") material.sway = words.number(0);
    else if (key == "drawsOver") {}             // of 1.2's first days: what lies together is now apart in the scene itself
    else if (key == "surfaceShader") material.shader = words.string();
    else if (key == "shaderValue") {
        std::string name = words.string();
        material.setShaderValue(name, words.vec4());
    }
    // The shader of a .material file: which program draws the surface.
    else if (key == "shader") {
        std::string shader = words.string();
        material.isUnlit = shader == "unlit";
        if (shader == "water") material.wasWater = true;
    }
    // Colors are stored sRGB-encoded; everything else is data and must not be converted.
    else if (key == "baseColorTexture") material.baseColorTexture = texture(TextureFormat::RGBA8_sRGB);
    else if (key == "emissiveTexture") material.emissiveTexture = texture(TextureFormat::RGBA8_sRGB);
    else if (key == "normalTexture") material.normalTexture = texture(TextureFormat::RGBA8);
    else if (key == "metallicRoughnessTexture") material.metallicRoughnessTexture = texture(TextureFormat::RGBA8);
    else if (key == "occlusionTexture") material.occlusionTexture = texture(TextureFormat::RGBA8);
}

}  // namespace

std::string writeMaterial(const Material& material) {
    std::string out = "molten-material 1\n";
    // The shader comes first: it decides what the other lines mean to the renderer.
    Line("shader", 0).word(material.isUnlit ? "unlit" : "lit").to(out);
    std::string properties;
    writeMaterialProperties(properties, material, 0);
    // "unlit" is said by the shader line here.
    size_t at = properties.find("unlit ");
    if (at != std::string::npos) properties.erase(at, properties.find('\n', at) - at + 1);
    return out + properties;
}

bool readMaterial(const std::string& text, Material& material, const TextureLoader& texture) {
    std::istringstream stream(text);
    std::string line;
    if (!std::getline(stream, line) || line.compare(0, 15, "molten-material") != 0) return false;
    if (std::string why = text::checkHeader(line, "molten-material"); !why.empty()) {
        logError("the material %s can't be read: %s", material.assetName.c_str(), why.c_str());
        return false;
    }
    std::string assetName = material.assetName;
    material = Material{};
    material.assetName = assetName;
    while (std::getline(stream, line)) {
        Words words(line);
        if (words.keyword().empty() || words.keyword()[0] == '#') continue;
        readMaterialProperty(words, material, texture);
    }
    return true;
}

std::string writeScene(Scene& scene, const std::vector<NamedMaterial>& materials, const RenderSettings& settings,
                       const std::vector<Entity>* only) {
    std::string out = "molten-scene 1\n";

    // ---- settings: the look of the scene. A part of a scene (a prefab) has none.
    if (!only) {
        out += "settings\n";
        Line("skyZenith").vec(settings.sky.zenithColor).to(out);
        Line("skyHorizon").vec(settings.sky.horizonColor).to(out);
        Line("skyGround").vec(settings.sky.groundColor).to(out);
        Line("skyIntensity").number(settings.sky.intensity).to(out);
        Line("environmentIntensity").number(settings.environmentIntensity).to(out);
        Line("fog").vec(settings.fogColor).number(settings.fogDensity).to(out);
        Line("shadows").flag(settings.shadowsEnabled).number(settings.cascades.shadowDistance).to(out);
        Line("rayTracedShadows").flag(settings.rayTracedShadows).integer(settings.shadowRayCount).to(out);
        Line("rayTracedLighting").flag(settings.rayTracedOcclusion).integer(settings.occlusionRayCount).flag(settings.rayTracedReflections)
            .flag(settings.rayTracedBounce).number(settings.bounceIntensity).to(out);
        Line("rayTracedReflections").number(settings.reflectionMaxRoughness).number(settings.reflectionStrength).to(out);
        Line("fogShape").number(settings.fogHeightFalloff).number(settings.fogSunScatter).flag(settings.fogLitBySun).to(out);
        if (settings.weather.rain > 0 || settings.weather.storm > 0 || settings.weather.snow > 0 || settings.weather.snowSettles) {
            Line weather("weather");
            weather.number(settings.weather.rain).number(settings.weather.storm);
            if (settings.weather.snow > 0 || settings.weather.snowSettles) weather.number(settings.weather.snow);      // (a file of before snow has two)
            if (settings.weather.snowSettles) weather.flag(true);
            weather.to(out);
        }
        Line("wind").vec(settings.wind).number(settings.windGustiness).to(out);
        // The sea: a scene without this line has none (scenes of before it).
        if (settings.sea.enabled) {
            const SeaSettings& sea = settings.sea;
            Line("sea").flag(true).number(sea.level).vec(sea.color).vec(sea.shallowColor).number(sea.clarity).number(sea.scattering)
                .number(sea.reflections).number(sea.ripples).number(sea.caustics).number(sea.foam).number(sea.whitecaps)
                .flag(sea.lensThroughSurface).to(out);
        }
        Line("ambientOcclusion").flag(settings.ambientOcclusionEnabled).number(settings.ambientOcclusionRadius)
            .number(settings.ambientOcclusionIntensity).to(out);
        Line("bloom").flag(settings.bloomEnabled).number(settings.bloomIntensity).number(settings.bloomThreshold).to(out);
        Line("exposure").number(settings.exposure).to(out);
        Line("resolution").number(settings.renderScale).flag(settings.metalFXUpscaling).flag(settings.dynamicResolution)
            .number(settings.targetFrameRate).number(settings.lowestDynamicScale).to(out);
        Line("toneMapper").integer(settings.toneMapper).to(out);
        Line("vignette").number(settings.vignette).to(out);
        Line("clouds").number(settings.sky.cloudCoverage).number(settings.sky.cloudScale).number(settings.sky.cloudHeight).number(settings.sky.cloudThickness).to(out);
        Line("volumetrics").flag(settings.sky.cloudsHaveDepth).integer(settings.fogShaftSteps).to(out);
        Line("lens").number(settings.lensDistortion).number(settings.chromaticAberration).number(settings.filmGrain).to(out);
        if (!settings.sky.sunsetColors) Line("sunsetColors").flag(false).to(out);
        if (settings.sky.stars != SkySettings{}.stars || settings.sky.moon != SkySettings{}.moon) Line("night").number(settings.sky.stars).number(settings.sky.moon).to(out);
        if (!settings.colorGrading.empty()) Line("colorGrading").string(settings.colorGrading).number(settings.colorGradingAmount).to(out);
        if (!settings.bakedLighting.empty()) Line("bakedLighting").string(settings.bakedLighting).to(out);
        if (settings.autoExposure)
            Line("autoExposure").flag(true).number(settings.exposureCompensation).number(settings.exposureAdaptSpeed)
                .number(settings.darkestExposure).number(settings.brightestExposure).to(out);
        if (settings.depthOfField)
            Line("depthOfField").flag(true).number(settings.focusDistance).number(settings.focusRange).number(settings.focusBlur).to(out);
        if (settings.motionBlur) Line("motionBlur").flag(true).number(settings.motionBlurAmount).to(out);
        if (settings.contrast != 1 || settings.saturation != 1 || settings.temperature != 0 || settings.tint != 0 || settings.sharpen != 0)
            Line("color").number(settings.contrast).number(settings.saturation).number(settings.temperature).number(settings.tint).number(settings.sharpen).to(out);
        if (!settings.lensDirt.empty()) Line("lensDirt").string(settings.lensDirt).number(settings.lensDirtAmount).to(out);
        if (settings.interference > 0) Line("interference").number(settings.interference).to(out);
    }

    // ---- materials: the named ones, then any other that an entity uses
    auto isWritten = [&](Entity entity) {
        if (scene.has<EditorOnly>(entity) || scene.has<FromPrefab>(entity)) return false;
        return !only || std::find(only->begin(), only->end(), entity) != only->end();
    };
    std::vector<NamedMaterial> all;
    if (!only) {
        all = materials;
    } else {
        // Of the named materials, a part of a scene takes only the ones it uses.
        for (const NamedMaterial& named : materials) {
            bool isUsed = false;
            scene.each<MeshRenderer, Transform>([&](Entity entity, MeshRenderer& renderer, Transform&) {
                if (isWritten(entity) && renderer.material == named.material) isUsed = true;
            });
            if (isUsed) all.push_back(named);
        }
    }
    std::unordered_map<const Material*, int> materialIndex;
    // A material that is an asset has a file of its own: the scene only names it.
    all.erase(std::remove_if(all.begin(), all.end(), [](const NamedMaterial& named) { return !named.material || !named.material->assetName.empty(); }),
              all.end());
    for (size_t i = 0; i < all.size(); i++) materialIndex[all[i].material.get()] = int(i);
    scene.each<MeshRenderer, Transform>([&](Entity entity, MeshRenderer& renderer, Transform&) {
        if (!isWritten(entity) || !renderer.material || !renderer.material->assetName.empty() || materialIndex.count(renderer.material.get())) return;
        materialIndex[renderer.material.get()] = int(all.size());
        all.push_back(NamedMaterial{"Material " + std::to_string(all.size() + 1), renderer.material});
    });
    for (const NamedMaterial& named : all) {
        const Material& m = *named.material;
        Line("material", 0).string(named.name).to(out);
        writeMaterialProperties(out, m, 1);
    }

    // ---- entities, in the order the scene stores their transforms. A parent is written
    // as its position in this list.
    std::vector<Entity> entities;
    std::unordered_map<uint32_t, int> fileIndex;
    scene.each<Transform>([&](Entity entity, Transform&) {
        if (!isWritten(entity)) return;
        fileIndex[entity.index] = int(entities.size());
        entities.push_back(entity);
    });
    for (Entity entity : entities) {
        const Name* name = scene.get<Name>(entity);
        Line("entity", 0).string(name ? name->value : std::string()).to(out);

        const Transform& t = *scene.get<Transform>(entity);
        Line("transform").vec(t.position).vec(t.rotation.vector).vec(t.scale).to(out);
        if (scene.isAlive(t.parent) && fileIndex.count(t.parent.index)) Line("parent").integer(fileIndex[t.parent.index]).to(out);
        if (!t.isActive) Line("inactive").to(out);
        if (const PrefabInstance* instance = scene.get<PrefabInstance>(entity)) {
            Line("prefab").string(instance->prefab).to(out);
            for (const PrefabOverride& change : instance->overrides) {
                Line("override").string(change.path).string(change.component).string(change.field).string(change.value).to(out);
            }
        }
        if (const Tag* tag = scene.get<Tag>(entity); tag && (tag->value != "Untagged" || tag->layer != 0)) {
            Line("tag").string(tag->value).integer(tag->layer).to(out);
        }
        if (const DisabledComponents* disabled = scene.get<DisabledComponents>(entity); disabled && !disabled->names.empty()) {
            Line line("disabled");
            for (const std::string& name : disabled->names) line.string(name);
            line.to(out);
        }

        if (const Camera* c = scene.get<Camera>(entity)) {
            Line("camera").number(c->fovY).number(c->nearPlane).number(c->farPlane).flag(c->isOrthographic)
                .number(c->orthographicHeight).flag(c->isActive).to(out);
        }
        if (const Light* l = scene.get<Light>(entity)) {
            Line("light").word(kLightTypes[int(l->type)]).vec(l->color).number(l->intensity).number(l->range)
                .number(l->innerConeAngle).number(l->outerConeAngle).flag(l->castsShadows).flag(l->isEnabled).number(l->areaSize.x).number(l->areaSize.y).to(out);
            // What came later, only when it is not as it was: in the air, how much, and its cookie.
            if (l->isVolumetric || l->volumetricStrength != 1.0f || !l->cookie.empty())
                Line("lightAir").flag(l->isVolumetric).number(l->volumetricStrength).string(l->cookie).to(out);
        }
        if (const MeshRenderer* r = scene.get<MeshRenderer>(entity)) {
            bool isAsset = r->material && !r->material->assetName.empty();
            int material = r->material && !isAsset ? materialIndex[r->material.get()] : -1;
            Line("mesh").string(r->mesh ? r->mesh->name : r->meshName).integer(material).vec(r->tint)
                .flag(r->castsShadows).flag(r->isVisible).to(out);
            if (isAsset) Line("materialAsset").string(r->material->assetName).to(out);
        }
        if (const Collider* c = scene.get<Collider>(entity)) {
            Line line("collider");
            line.word(kShapes[int(c->shape)]);
            if (c->shape == ColliderShape::Box) line.vec(c->halfExtents);
            else if (c->shape == ColliderShape::Mesh) line.string(c->meshName);
            else if (c->shape == ColliderShape::Sphere) line.number(c->radius);
            else line.number(c->radius).number(c->halfHeight);
            line.vec(c->offset).flag(c->isTrigger);
            if (c->shape == ColliderShape::Mesh) line.flag(c->isConvex);
            line.to(out);
        }
        if (const RigidBody* b = scene.get<RigidBody>(entity)) {
            Line("body").word(kBodyTypes[int(b->type)]).number(b->mass).number(b->restitution).number(b->friction)
                .number(b->linearDamping).number(b->gravityScale).flag(b->rotates).number(b->angularDamping).flag(b->canSleep).to(out);
        }
        if (const ParticleEmitter* e = scene.get<ParticleEmitter>(entity)) {
            Line("particles").number(e->rate).number(e->lifetimeMin).number(e->lifetimeMax).number(e->speedMin).number(e->speedMax)
                .vec(e->direction).number(e->spread).number(e->spawnRadius).vec(e->gravity).number(e->drag).to(out);
            Line("particleLook").number(e->sizeStart).number(e->sizeEnd).vec(e->colorStart).vec(e->colorEnd)
                .flag(e->isAdditive).flag(e->isEmitting).integer(e->maxParticles).number(e->windInfluence).integer(e->burstAtStart).to(out);
            // What the two lines above do not hold (shape, a middle color, bursts...), by name, where it is not the usual.
            for (const std::string& extra : effectExtras(*e)) out += "  particleSet " + extra + "\n";
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

namespace {

/// How deep prefabs inside prefabs (and variants of variants) go: a prefab that holds itself stops here.
thread_local int gPrefabDepth = 0;


}  // namespace

/// An entity under `root` by the path of names below it ("Body/Hat"); "" is the root itself.
Entity entityAtPath(Scene& scene, Entity root, const std::string& path) {
    Entity at = root;
    size_t start = 0;
    while (start < path.size() && at.isValid()) {
        size_t slash = path.find('/', start);
        std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        Entity found;
        scene.each<Transform>([&](Entity child, Transform& place) {
            if (!found.isValid() && place.parent == at) {
                const Name* name = scene.get<Name>(child);
                if (name && name->value == part) found = child;
            }
        });
        at = found;
        start = slash == std::string::npos ? path.size() : slash + 1;
    }
    return at;
}

bool pathBelow(const Scene& scene, Entity root, Entity entity, std::string& path) {
    path.clear();
    std::vector<std::string> names;
    Entity at = entity;
    for (int depth = 0; depth < 64; depth++) {
        if (at == root) {
            for (size_t k = names.size(); k-- > 0;) path += names[k] + (k > 0 ? "/" : "");
            return true;
        }
        const Transform* place = scene.get<Transform>(at);
        const Name* name = scene.get<Name>(at);
        if (!place || !scene.isAlive(place->parent)) break;
        names.push_back(name ? name->value : std::string());
        at = place->parent;
    }
    path.clear();
    return false;
}

bool PrefabInstance::setOverride(const PrefabOverride& change) {
    for (PrefabOverride& existing : overrides) {
        if (existing.path == change.path && existing.component == change.component && existing.field == change.field) {
            if (existing.value == change.value) return false;
            existing.value = change.value;
            return true;
        }
    }
    overrides.push_back(change);
    return true;
}

bool PrefabInstance::removeOverride(const std::string& path, const std::string& component, const std::string& field) {
    size_t before = overrides.size();
    overrides.erase(std::remove_if(overrides.begin(), overrides.end(),
                                   [&](const PrefabOverride& o) { return o.path == path && o.component == component && o.field == field; }),
                    overrides.end());
    return overrides.size() != before;
}

const PrefabOverride* PrefabInstance::findOverride(const std::string& path, const std::string& component, const std::string& field) const {
    for (const PrefabOverride& existing : overrides) {
        if (existing.path == path && existing.component == component && existing.field == field) return &existing;
    }
    return nullptr;
}

bool applyPrefabValue(Scene& scene, Entity root, const PrefabOverride& change, const SceneAssets& assets) {
    Entity target = entityAtPath(scene, root, change.path);
    if (!target.isValid()) return false;
    const std::string &component = change.component, &field = change.field, &value = change.value;
    if (component == "Transform") {
        Transform* place = scene.get<Transform>(target);
        Words numbers("value " + value);         // the first word of a line is its key
        if (!place) return false;
        if (field == "position") place->position = numbers.vec3(place->position);
        else if (field == "scale") place->scale = numbers.vec3(place->scale);
        else if (field == "rotation") {
            Vec3 degrees = numbers.vec3();
            place->rotation = simd::normalize(axisAngle(Vec3{0, 1, 0}, radians(degrees.y)) * axisAngle(Vec3{1, 0, 0}, radians(degrees.x)) *
                                              axisAngle(Vec3{0, 0, 1}, radians(degrees.z)));
        } else if (field == "active") {
            place->isActive = value != "0";
        } else {
            return false;
        }
        return true;
    }
    if (component == "MeshRenderer" && field == "material") {
        MeshRenderer* renderer = scene.get<MeshRenderer>(target);
        if (!renderer) return false;
        if (assets.material) {
            if (std::shared_ptr<Material> material = assets.material(value)) renderer->material = material;
        }
        return true;
    }
    const ComponentType* type = ComponentRegistry::instance().find(component);
    if (!type) return false;
    const Field* declared = type->findField(field);
    void* instance = type->get ? type->get(scene, target) : nullptr;
    if (!declared || !instance) return false;
    declared->setValueText(instance, value);
    return true;
}

namespace {

bool readVariant(const std::string& text, Scene& scene, std::vector<NamedMaterial>& materials, RenderSettings& settings, const SceneAssets& assets,
                 std::vector<Entity>* created);

}  // namespace

bool isPrefabVariant(const std::string& text) {
    std::istringstream stream(text);
    std::string line;
    return std::getline(stream, line) && Words(line).keyword() == "molten-variant";
}

namespace {
bool readSceneAsWritten(const std::string& text, Scene& scene, std::vector<NamedMaterial>& materials, RenderSettings& settings,
                        const SceneAssets& assets, std::vector<Entity>* created);
}

bool readScene(const std::string& text, Scene& scene, std::vector<NamedMaterial>& materials, RenderSettings& settings,
               const SceneAssets& assets, std::vector<Entity>* created) {
    bool isRead = readSceneAsWritten(text, scene, materials, settings, assets, created);
    // Scenes from before the sea: what their water materials and Water components said is said where it is said now.
    retireOldWater(scene, settings);
    return isRead;
}

namespace {
bool readSceneAsWritten(const std::string& text, Scene& scene, std::vector<NamedMaterial>& materials, RenderSettings& settings,
                        const SceneAssets& assets, std::vector<Entity>* created) {
    if (isPrefabVariant(text)) return readVariant(text, scene, materials, settings, assets, created);
    std::istringstream stream(text);
    std::string line;
    // "learnmetal-scene" is what the engine wrote before it had this name: still read.
    std::string header = std::getline(stream, line) ? std::string(Words(line).keyword()) : std::string();
    if (header != "molten-scene" && header != "learnmetal-scene") {
        logError("not a scene file: the first line must be \"molten-scene 1\"");
        return false;
    }
    if (header == "molten-scene") {
        if (std::string why = text::checkHeader(line, "molten-scene"); !why.empty()) {
            logError("this scene can't be read: %s", why.c_str());
            return false;
        }
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

    while (std::getline(stream, line)) {
        Words words(line);
        const std::string& key = words.keyword();
        if (key.empty() || key[0] == '#') continue;

        // ---- the lines that start a block
        if (key == "settings") {
            block = Block::Settings;
            settings.sea.enabled = false;             // unless its "sea" line says so: a scene of before has no sea
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
            } else if (key == "sea") {
                SeaSettings& sea = settings.sea;
                sea = SeaSettings{};
                sea.enabled = words.flag(true);
                sea.level = words.number(0);
                sea.color = words.vec3(sea.color);
                if (words.items.size() <= 8) {
                    // As it was written for its first hours: a murkiness, and the foam.
                    sea.clarity = SeaSettings::clarityOfMurk(words.number(0.5f));
                    sea.foam = std::max(words.number(sea.foam), 0.0f);
                } else {
                    sea.shallowColor = words.vec3(sea.shallowColor);
                    sea.clarity = std::max(words.number(sea.clarity), 0.2f);
                    sea.scattering = std::max(words.number(sea.scattering), 0.0f);
                    sea.reflections = std::max(words.number(sea.reflections), 0.0f);
                    sea.ripples = std::max(words.number(sea.ripples), 0.0f);
                    sea.caustics = std::max(words.number(sea.caustics), 0.0f);
                    sea.foam = std::max(words.number(sea.foam), 0.0f);
                    sea.whitecaps = std::max(words.number(sea.whitecaps), 0.0f);
                    sea.lensThroughSurface = words.flag(true);
                }
            } else if (key == "wind") {
                settings.wind = words.vec3(settings.wind);
                settings.windGustiness = words.number(settings.windGustiness);
            } else if (key == "rayTracedLighting") {
                settings.rayTracedOcclusion = words.flag(false);
                settings.occlusionRayCount = int(words.integer(4));
                settings.rayTracedReflections = words.flag(false);
                settings.rayTracedBounce = words.flag(false);
                settings.bounceIntensity = words.number(1);
            } else if (key == "rayTracedReflections") {
                settings.reflectionMaxRoughness = words.number(0.3f);
                settings.reflectionStrength = words.number(1);
            } else if (key == "weather") {
                settings.weather.rain = std::clamp(words.number(0), 0.0f, 1.0f);
                settings.weather.storm = std::clamp(words.number(0), 0.0f, 1.0f);
                settings.weather.snow = std::clamp(words.number(0), 0.0f, 1.0f);
                settings.weather.snowSettles = words.flag(false);
            } else if (key == "fogShape") {
                settings.fogHeightFalloff = words.number(0);
                settings.fogSunScatter = words.number(0);
                settings.fogLitBySun = words.flag(true);           // scenes of before: lit by the sun, as every scene now starts
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
            else if (key == "resolution") {
                settings.renderScale = words.number(1);
                settings.metalFXUpscaling = words.flag(true);
                settings.dynamicResolution = words.flag(false);
                settings.targetFrameRate = words.number(60);
                settings.lowestDynamicScale = words.number(0.6f);
            }
            else if (key == "toneMapper") settings.toneMapper = int(words.integer());
            else if (key == "color") {
                settings.contrast = words.number(1);
                settings.saturation = words.number(1);
                settings.temperature = words.number(0);
                settings.tint = words.number(0);
                settings.sharpen = words.number(0);
            }
            else if (key == "lensFlare") {}            // of before: the lens flare was taken out
            else if (key == "lensDirt") {
                settings.lensDirt = words.string();
                settings.lensDirtAmount = words.number(1);
            }
            else if (key == "interference") settings.interference = words.number(0);
            else if (key == "vignette") settings.vignette = words.number();
            else if (key == "volumetrics") {
                settings.sky.cloudsHaveDepth = words.flag(false);
                settings.fogShaftSteps = int(words.integer(0));
            } else if (key == "clouds") {
                settings.sky.cloudCoverage = words.number();
                settings.sky.cloudScale = words.number(1);
                settings.sky.cloudHeight = words.number(1500);
                settings.sky.cloudThickness = words.number(1200);
            } else if (key == "sunsetColors") {
                settings.sky.sunsetColors = words.flag(true);
            } else if (key == "night") {
                settings.sky.stars = words.number(0);
                settings.sky.moon = words.number(1);
            } else if (key == "physicalSky") {
                // From before 1.1, when the sky could be simulated: only its stars are kept.
                words.flag(false);
                words.number(0.3f);
                settings.sky.stars = words.number(0);
            } else if (key == "bakedLighting") {
                settings.bakedLighting = words.string();
            } else if (key == "colorGrading") {
                settings.colorGrading = words.string();
                settings.colorGradingAmount = words.number(1);
            } else if (key == "autoExposure") {
                settings.autoExposure = words.flag(false);
                settings.exposureCompensation = words.number(0);
                settings.exposureAdaptSpeed = words.number(1.5f);
                settings.darkestExposure = words.number(0.25f);
                settings.brightestExposure = words.number(8);
            } else if (key == "depthOfField") {
                settings.depthOfField = words.flag(false);
                settings.focusDistance = words.number(10);
                settings.focusRange = words.number(4);
                settings.focusBlur = words.number(8);
            } else if (key == "motionBlur") {
                settings.motionBlur = words.flag(false);
                settings.motionBlurAmount = words.number(0.5f);
            } else if (key == "lens") {
                settings.lensDistortion = words.number();
                settings.chromaticAberration = words.number();
                settings.filmGrain = words.number();
            }

        // ---- a material
        } else if (block == Block::Material && material) {
            readMaterialProperty(words, *material, assets.texture);

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
            } else if (key == "inactive") {
                if (Transform* t = scene.get<Transform>(entity)) t->isActive = false;
            } else if (key == "tag") {
                Tag tag;
                tag.value = words.string();
                tag.layer = int(words.integer(0));
                scene.add(entity, tag);
            } else if (key == "disabled") {
                DisabledComponents disabled;
                while (words.hasMore()) disabled.names.push_back(words.string());
                scene.add(entity, disabled);
            } else if (key == "parent") {
                parents.push_back({entity, words.integer(-1)});
            } else if (key == "prefab") {
                scene.add(entity, PrefabInstance{words.string(), {}});
            } else if (key == "override") {
                if (PrefabInstance* instance = scene.get<PrefabInstance>(entity)) {
                    PrefabOverride change;
                    change.path = words.string();
                    change.component = words.string();
                    change.field = words.string();
                    change.value = words.string();
                    instance->overrides.push_back(change);
                }
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
                l.type = LightType(words.choice(kLightTypes, 4, 1));
                l.color = words.vec3(Vec3{1, 1, 1});
                l.intensity = words.number(1);
                l.range = words.number(l.range);
                l.innerConeAngle = words.number(l.innerConeAngle);
                l.outerConeAngle = words.number(l.outerConeAngle);
                l.castsShadows = words.flag();
                l.isEnabled = words.flag(true);
                l.areaSize.x = words.number(l.areaSize.x);
                l.areaSize.y = words.number(l.areaSize.y);
                scene.add(entity, l);
            } else if (key == "lightAir") {
                if (Light* l = scene.get<Light>(entity)) {
                    l->isVolumetric = words.flag(false);
                    l->volumetricStrength = std::max(words.number(1.0f), 0.0f);
                    l->cookie = words.string();
                }
            } else if (key == "mesh") {
                MeshRenderer r;
                std::string name = words.string();
                if (assets.mesh && !name.empty()) r.mesh = assets.mesh(name);
                if (!r.mesh) r.meshName = name;         // not found, or nobody to ask: kept, to be written back
                long index = words.integer(-1);
                if (index >= 0 && size_t(index) < materials.size()) r.material = materials[size_t(index)].material;
                r.tint = words.vec4(Vec4{1, 1, 1, 1});
                r.castsShadows = words.flag(true);
                r.isVisible = words.flag(true);
                scene.add(entity, r);
            } else if (key == "materialAsset") {
                std::string name = words.string();
                if (MeshRenderer* r = scene.get<MeshRenderer>(entity)) {
                    r->material = assets.material ? assets.material(name) : nullptr;
                    if (!r->material) {
                        // Not found, or nobody to ask: the name is kept, so that saving the
                        // scene again does not lose what it pointed to.
                        r->material = std::make_shared<Material>();
                        r->material->assetName = name;
                    }
                }
            } else if (key == "collider") {
                Collider c;
                c.shape = ColliderShape(words.choice(kShapes, 4, 1));
                if (c.shape == ColliderShape::Mesh) {
                    c.meshName = words.string();
                } else if (c.shape == ColliderShape::Box) {
                    c.halfExtents = words.vec3(c.halfExtents);
                } else if (c.shape == ColliderShape::Sphere) {
                    c.radius = words.number(c.radius);
                } else {
                    c.radius = words.number(c.radius);
                    c.halfHeight = words.number(c.halfHeight);
                }
                c.offset = words.vec3();
                c.isTrigger = words.flag();
                if (c.shape == ColliderShape::Mesh) c.isConvex = words.flag();
                // A mesh collider is its model where the model is: it has no offset of its own. (Its
                // first days gave it the model's middle when it was added, which put it beside the model.)
                if (c.shape == ColliderShape::Mesh) c.offset = Vec3{0, 0, 0};
                scene.add(entity, c);
            } else if (key == "body") {
                RigidBody b;
                b.type = BodyType(words.choice(kBodyTypes, 3, 2));
                b.mass = words.number(b.mass);
                b.restitution = words.number(b.restitution);
                b.friction = words.number(b.friction);
                b.linearDamping = words.number(b.linearDamping);
                b.gravityScale = words.number(b.gravityScale);
                b.rotates = words.flag(false);
                b.angularDamping = words.number(b.angularDamping);
                b.canSleep = words.flag(true);              // files from before sleeping: they may
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
            } else if (key == "particleSet") {
                if (ParticleEmitter* e = scene.get<ParticleEmitter>(entity)) {
                    std::string name = words.string();
                    if (name == "texture") {
                        setEffectText(*e, name, words.string());
                        continue;
                    }
                    std::vector<float> values;
                    while (words.hasMore()) values.push_back(words.number(0));
                    setEffectValue(*e, name, values);
                }
            } else if (key == "particleLook") {
                if (ParticleEmitter* e = scene.get<ParticleEmitter>(entity)) {
                    e->sizeStart = words.number(e->sizeStart);
                    e->sizeEnd = words.number(e->sizeEnd);
                    e->colorStart = words.vec4(e->colorStart);
                    e->colorEnd = words.vec4(e->colorEnd);
                    e->isAdditive = words.flag(true);
                    e->isEmitting = words.flag(true);
                    e->maxParticles = int(words.integer(e->maxParticles));
                    e->windInfluence = words.number(0);
                    e->burstAtStart = int(words.integer(0));
                }
            }
        }
    }

    for (const auto& [child, index] : parents) {
        if (index >= 0 && size_t(index) < entities.size() && entities[size_t(index)] != child) {
            scene.get<Transform>(child)->parent = entities[size_t(index)];
        }
    }
    // Copies of prefabs: what each prefab holds, put under its entity, marked as the prefab's.
    std::vector<Entity> nested;
    for (Entity instance : entities) {
        const PrefabInstance* reference = scene.get<PrefabInstance>(instance);
        if (!reference) continue;
        std::string path = reference->prefab;
        std::optional<std::string> prefab = assets.prefabText ? assets.prefabText(path) : std::nullopt;
        if (!prefab) {
            logError("the prefab %s is not there: its copy is left empty", path.c_str());
            continue;
        }
        if (gPrefabDepth >= 8) {
            logError("prefabs inside prefabs too deep (does %s hold itself?)", path.c_str());
            continue;
        }
        gPrefabDepth++;
        std::vector<NamedMaterial> itsMaterials;            // kept alive by the meshes that use them
        RenderSettings ignored;
        std::vector<Entity> inside;
        bool read = readScene(*prefab, scene, itsMaterials, ignored, assets, &inside);
        gPrefabDepth--;
        if (!read) continue;
        for (Entity part : inside) {
            scene.add(part, FromPrefab{});
            Transform* place = scene.get<Transform>(part);
            // Its root (or roots) under the copy, where the copy is.
            if (place && !scene.isAlive(place->parent)) {
                place->parent = instance;
                place->position = Vec3{0, 0, 0};
            }
            nested.push_back(part);
        }
        // What this copy changes of its prefab.
        if (const PrefabInstance* changed = scene.get<PrefabInstance>(instance)) {
            std::vector<PrefabOverride> overrides = changed->overrides;
            scene.updateTransforms();
            for (const PrefabOverride& change : overrides) {
                if (!applyPrefabValue(scene, instance, change, assets)) logError("the copy of %s has nothing at \"%s\" for its change", path.c_str(), change.path.c_str());
            }
        }
    }
    if (created) {
        *created = entities;
        created->insert(created->end(), nested.begin(), nested.end());
    }
    // A level of ray tracing saved when the levels sent several times the rays: the same level, as it is now.
    bringLightingQualityUpToDate(settings);
    return true;
}
}  // namespace

namespace {

bool readVariant(const std::string& text, Scene& scene, std::vector<NamedMaterial>& materials, RenderSettings& settings, const SceneAssets& assets,
                 std::vector<Entity>* created) {
    std::istringstream stream(text);
    std::string line;
    std::getline(stream, line);
    if (std::string why = text::checkHeader(line, "molten-variant"); !why.empty()) {
        logError("this prefab variant can't be read: %s", why.c_str());
        return false;
    }
    std::string base, name;
    std::vector<std::vector<std::string>> sets;
    while (std::getline(stream, line)) {
        Words words(line);
        const std::string& key = words.keyword();
        if (key == "base") base = words.string();
        else if (key == "name") name = words.string();
        else if (key == "set") {
            std::vector<std::string> parts;
            for (int i = 0; i < 4; i++) parts.push_back(words.string());
            sets.push_back(parts);
        }
    }
    std::optional<std::string> baseText = assets.prefabText && !base.empty() ? assets.prefabText(base) : std::nullopt;
    if (!baseText) {
        logError("a prefab variant's base %s is not there", base.c_str());
        return false;
    }
    if (gPrefabDepth >= 8) {
        logError("prefab variants too deep (is %s its own base?)", base.c_str());
        return false;
    }
    gPrefabDepth++;
    std::vector<Entity> made;
    bool read = readScene(*baseText, scene, materials, settings, assets, &made);
    gPrefabDepth--;
    if (!read || made.empty()) return false;
    Entity root = made.front();
    for (Entity entity : made) {
        const Transform* place = scene.get<Transform>(entity);
        if (place && !scene.isAlive(place->parent)) {
            root = entity;
            break;
        }
    }
    if (!name.empty()) {
        if (Name* named = scene.get<Name>(root)) named->value = name;
    }
    for (const std::vector<std::string>& set : sets) {
        PrefabOverride change{set[0], set[1], set[2], set[3]};
        if (!applyPrefabValue(scene, root, change, assets)) logError("prefab variant: the base has no \"%s\" with that", set[0].c_str());
    }
    if (created) *created = made;
    return true;
}

}  // namespace

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
