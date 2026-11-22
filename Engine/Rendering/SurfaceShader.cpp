// SurfaceShader.cpp
#include "Rendering/SurfaceShader.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace molten {

namespace {

bool isNameStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool isNameChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

/// Where the word `properties` followed by `{` begins, outside comments. npos if there is none.
size_t findPropertiesBlock(const std::string& text, size_t& open, size_t& close) {
    bool inLineComment = false, inBlockComment = false;
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i], next = i + 1 < text.size() ? text[i + 1] : '\0';
        if (inLineComment) {
            if (c == '\n') inLineComment = false;
            continue;
        }
        if (inBlockComment) {
            if (c == '*' && next == '/') inBlockComment = false, i++;
            continue;
        }
        if (c == '/' && next == '/') {
            inLineComment = true;
            continue;
        }
        if (c == '/' && next == '*') {
            inBlockComment = true;
            continue;
        }
        if (text.compare(i, 10, "properties") == 0 && (i == 0 || !isNameChar(text[i - 1]))) {
            size_t at = i + 10;
            while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at]))) at++;
            if (at < text.size() && text[at] == '{') {
                open = at;
                close = text.find('}', at);
                return close == std::string::npos ? std::string::npos : i;
            }
        }
        // Code has begun: a properties block comes before it.
        if (c == '(' || c == ';') break;
    }
    return std::string::npos;
}

}  // namespace

SurfaceShader SurfaceShader::parse(const std::string& name, const std::string& text) {
    SurfaceShader shader;
    shader.name = name;
    shader.code = text;
    size_t open = 0, close = 0;
    size_t start = findPropertiesBlock(text, open, close);
    if (start == std::string::npos) return shader;

    std::istringstream lines(text.substr(open + 1, close - open - 1));
    std::string line;
    while (std::getline(lines, line)) {
        size_t comment = line.find("//");
        if (comment != std::string::npos) line.erase(comment);
        for (char& c : line) {
            if (c == '=' || c == ',' || c == ';') c = ' ';
        }
        std::istringstream words(line);
        std::string kind;
        if (!(words >> kind)) continue;
        SurfaceProperty property;
        if (kind == "float") property.kind = SurfaceProperty::Kind::Float;
        else if (kind == "color") property.kind = SurfaceProperty::Kind::Color, property.value = Vec4{1, 1, 1, 1};
        else if (kind == "vector") property.kind = SurfaceProperty::Kind::Vector;
        else {
            shader.error = "a property is a float, a color or a vector: \"" + kind + "\" is none of them";
            continue;
        }
        if (!(words >> property.name) || !isNameStart(property.name[0])) {
            shader.error = "a property needs a name after \"" + kind + "\"";
            continue;
        }
        bool isGoodName = true;
        for (char c : property.name) isGoodName = isGoodName && isNameChar(c);
        if (!isGoodName) {
            shader.error = "\"" + property.name + "\" is not a name: letters, digits and _ only";
            continue;
        }
        if (shader.property(property.name)) {
            shader.error = "two properties are called \"" + property.name + "\"";
            continue;
        }
        // Its numbers, then perhaps "range low high".
        std::string word;
        int count = 0;
        while (words >> word) {
            if (word == "range") {
                property.hasRange = bool(words >> property.min >> property.max);
                break;
            }
            char* end = nullptr;
            float number = std::strtof(word.c_str(), &end);
            if (end == word.c_str()) break;
            if (count < 4) property.value[count++] = number;
        }
        if (property.kind == SurfaceProperty::Kind::Color && count == 3) property.value.w = 1;
        if (int(shader.properties.size()) >= kMaxProperties) {
            shader.error = "a shader has at most " + std::to_string(kMaxProperties) + " properties: \"" + property.name + "\" is one too many";
            continue;
        }
        shader.properties.push_back(property);
    }
    // The block is not Metal: it is taken out, its lines kept so that errors name the right ones.
    for (size_t i = start; i <= close; i++) {
        if (shader.code[i] != '\n') shader.code[i] = ' ';
    }
    return shader;
}

const SurfaceProperty* SurfaceShader::property(const std::string& propertyName) const {
    for (const SurfaceProperty& property : properties) {
        if (property.name == propertyName) return &property;
    }
    return nullptr;
}

std::string surfaceShaderCode(const std::vector<SurfaceShader>& shaders) {
    if (shaders.empty()) return {};
    std::string out;
    for (size_t i = 0; i < shaders.size(); i++) {
        const SurfaceShader& shader = shaders[i];
        std::string space = "molten_surface_" + std::to_string(i + 1);
        out += "namespace " + space + " {\n";
        out += "struct Surface { float3 color; float alpha; float metallic; float roughness; float occlusion; float3 emissive; float3 normal; };\n";
        out += "struct Input { float2 uv; float3 position; float3 normal; float3 view; float time; float4 tint; float3 wind; float behind; };\n";
        out += "static float noise(float2 p) { return waterNoise(p); }\n";
        out += "struct Props {";
        for (const SurfaceProperty& property : shader.properties) {
            out += std::string(property.kind == SurfaceProperty::Kind::Float ? " float " : " float4 ") + property.name + ";";
        }
        if (shader.properties.empty()) out += " float unused;";
        out += " };\n";
        out += "#line 1 \"" + shader.name + "\"\n";
        out += shader.code;
        if (shader.code.empty() || shader.code.back() != '\n') out += "\n";
        out += "#line 1 \"(made by the engine for " + shader.name + ")\"\n";
        out += "static MaterialSample run(MaterialSample m, MeshVertexOut vin, float3 cameraPosition, float time, float3 wind, float behind) {\n";
        out += "    Surface s;\n";
        out += "    s.color = m.baseColor.rgb; s.alpha = m.baseColor.a; s.metallic = m.metallic; s.roughness = m.roughness;\n";
        out += "    s.occlusion = m.occlusion; s.emissive = m.emissive; s.normal = m.normal;\n";
        out += "    Input in;\n";
        out += "    in.uv = vin.uv; in.position = vin.worldPosition; in.normal = normalize(vin.normal);\n";
        out += "    in.view = normalize(cameraPosition - vin.worldPosition); in.time = time; in.tint = vin.tint; in.wind = wind; in.behind = behind;\n";
        out += "    Props props;\n";
        for (size_t p = 0; p < shader.properties.size(); p++) {
            const SurfaceProperty& property = shader.properties[p];
            out += "    props." + property.name + " = m.customValues[" + std::to_string(p) + "]" + (property.kind == SurfaceProperty::Kind::Float ? ".x" : "") + ";\n";
        }
        if (shader.properties.empty()) out += "    props.unused = 0.0;\n";
        out += "    s = surface(s, in, props);\n";
        out += "    m.baseColor = float4(s.color, saturate(s.alpha)); m.metallic = saturate(s.metallic); m.roughness = clamp(s.roughness, 0.045, 1.0);\n";
        out += "    m.occlusion = s.occlusion; m.emissive = s.emissive; m.normal = normalize(s.normal);\n";
        out += "    return m;\n";
        out += "}\n";
        out += "}  // namespace " + space + "\n";
    }
    out += "static MaterialSample moltenCustomSurface(MaterialSample m, MeshVertexOut vin, float3 cameraPosition, float time, float3 wind, float behind) {\n";
    out += "    switch (int(m.custom + 0.5)) {\n";
    for (size_t i = 0; i < shaders.size(); i++) {
        out += "        case " + std::to_string(i + 1) + ": return molten_surface_" + std::to_string(i + 1) + "::run(m, vin, cameraPosition, time, wind, behind);\n";
    }
    out += "        default: return m;\n";
    out += "    }\n";
    out += "}\n";
    return out;
}

int SurfaceShaderSet::use(const std::string& name) {
    for (size_t i = 0; i < shaders_.size(); i++) {
        if (shaders_[i].name == name) return int(i + 1);
    }
    shaders_.push_back(SurfaceShader{});
    shaders_.back().name = name;
    times_.push_back({});
    read(shaders_.size() - 1);
    isChanged_ = true;
    return int(shaders_.size());
}

const SurfaceShader* SurfaceShaderSet::find(const std::string& name) const {
    for (const SurfaceShader& shader : shaders_) {
        if (shader.name == name) return &shader;
    }
    return nullptr;
}

void SurfaceShaderSet::read(size_t index) {
    std::string name = shaders_[index].name;
    std::string path = pathOf ? pathOf(name) : name;
    std::error_code code;
    times_[index] = std::filesystem::last_write_time(path, code);
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        // No such file: a shader that leaves the surface as it is, so that everything still compiles.
        shaders_[index] = SurfaceShader::parse(name, "Surface surface(Surface s, Input in, Props props) { return s; }\n");
        shaders_[index].error = "cannot read " + name;
        return;
    }
    std::stringstream text;
    text << file.rdbuf();
    shaders_[index] = SurfaceShader::parse(name, text.str());
}

bool SurfaceShaderSet::takeChanges() {
    for (size_t i = 0; i < shaders_.size(); i++) {
        std::error_code code;
        auto time = std::filesystem::last_write_time(pathOf ? pathOf(shaders_[i].name) : shaders_[i].name, code);
        if (code || time == times_[i]) continue;
        read(i);
        isChanged_ = true;
    }
    bool changed = isChanged_;
    isChanged_ = false;
    return changed;
}

std::string newSurfaceShaderText() {
    return R"(// A surface shader: it says what the surface is at each pixel, and the engine lights it.
// Choose it in a material (Shader), and set its properties there.
//
//   Surface s:  color  alpha  metallic  roughness  occlusion  emissive  normal (in the world)
//   Input in:   uv  position (in the world)  normal  view (toward the eye)  time (seconds)  tint
//               wind (the scene's, meters a second)  behind (meters to what is behind, seen through)
//   Props:      the properties below, by name: props.speed, props.glow
//   noise(p):   smooth noise between 0 and 1
//
// `s` arrives as the material's settings and textures made it. Save the file and the
// editor compiles it; a mistake is reported in the console and on the material.

properties {
    float speed = 1 range 0 10
    float stripes = 6 range 1 40
    color glow = 1 0.4 0.1 1
}

Surface surface(Surface s, Input in, Props props) {
    // Bands that travel up the object and glow.
    float band = 0.5 + 0.5 * sin(in.position.y * props.stripes - in.time * props.speed * 3.0);
    band = pow(band, 4.0);
    s.emissive += props.glow.rgb * band * 2.0;
    s.roughness = mix(s.roughness, 0.15, band);
    return s;
}
)";
}

std::string newWaterShaderText() {
    return R"(// A water shader: water of the game's own, a pond, a pool, a lagoon. Put it on a plane
// whose material is seen through (Alpha: Blend; New Water Shader set that). The scene's sea is
// not this: Environment > Water.
//
//   in.behind  how far below the surface what is under it is (the bottom): deep or shallow
//   in.wind    the scene's wind, with its weather: the ripples go with it
//   in.time    seconds, for what moves
//   noise(p)   smooth noise between 0 and 1
//
// Everything below is meant to be changed: the colors, how they go from shallow to deep, the
// ripples, the foam. Save the file and the editor compiles it.

properties {
    color shallow = 0.25 0.85 0.78 1
    color deep = 0.02 0.22 0.42 1
    float clearDepth = 3 range 0.3 30
    float ripples = 1 range 0 3
    float rippleSize = 1 range 0.2 5
    float foamWidth = 0.5 range 0 3
    float caustics = 0.6 range 0 2
}

// The slope of small waves at a place: bumps of noise drifting with the wind, two sizes.
float2 rippleSlope(float2 p, float t, float2 drift, float size) {
    float2 slope = float2(0.0);
    for (int i = 0; i < 2; i++) {
        float scale = (i == 0 ? 1.3 : 0.45) * size;
        float2 q = (p - drift * t * (i == 0 ? 0.6 : 1.1)) / scale + float(i) * 17.0;
        float here = noise(q);
        slope += float2(noise(q + float2(0.25, 0.0)) - here, noise(q + float2(0.0, 0.25)) - here) * (i == 0 ? 0.9 : 0.5);
    }
    return slope;
}

Surface surface(Surface s, Input in, Props props) {
    float2 p = in.position.xz;
    float2 drift = length(in.wind.xz) > 0.01 ? normalize(in.wind.xz) : float2(0.7, 0.3);
    float strength = props.ripples * clamp(length(in.wind.xz) / 6.0, 0.3, 1.5);

    // Shallow over the bottom, deep where it is far down: the color and how much of the bottom shows.
    float shallow = exp(-max(in.behind, 0.0) / props.clearDepth);
    s.color = mix(props.deep.rgb, props.shallow.rgb, shallow);
    s.alpha = mix(0.96, 0.25, shallow);

    // Ripples: they tilt the surface, which the sky and the sun are mirrored in.
    float2 slope = rippleSlope(p, in.time, drift, props.rippleSize) * strength;
    s.normal = normalize(float3(-slope.x, 1.0, -slope.y));
    s.roughness = 0.04;
    s.metallic = 0.0;

    // Light gathered into lines on a shallow bottom, seen through the water.
    float lines = pow(1.0 - abs(noise(p * 1.7 + slope * 6.0 + in.time * 0.15) * 2.0 - 1.0), 6.0);
    s.emissive += props.shallow.rgb * lines * props.caustics * shallow * 0.6;

    // Foam where it meets the shore and what stands in it: bands that come and go.
    float edge = 1.0 - smoothstep(0.0, props.foamWidth, in.behind);
    float bands = 0.55 + 0.45 * sin(in.behind * 18.0 - in.time * 2.5 + noise(p * 2.0) * 4.0);
    float foam = saturate(edge * bands * 1.4) * step(0.001, props.foamWidth);
    s.color = mix(s.color, float3(0.95), foam);
    s.alpha = max(s.alpha, foam);
    return s;
}
)";
}

}  // namespace molten
