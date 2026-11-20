// Pictures of assets for the project window, drawn from what each file contains: a model
// as its shape, a sound as its wave, a scene as a map seen from above, a visual script
// as its boxes, code and text as their first lines. Each is worked out once and kept.
#include "Editor.hpp"
#include "ImGuiLayer.hpp"
#include "SoftRender.hpp"
#include "Assets/ImportSettings.hpp"
#include "Assets/MeshData.hpp"
#include "Scene/SceneFile.hpp"
#include "Audio/AudioClip.hpp"
#include "Core/TextFile.hpp"
#include "Script/Graph.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <unordered_map>

namespace molten::editor {

namespace {

struct Preview {
    enum class Kind { None, Shape, Wave, Map, Boxes, Lines, Picture } kind = Kind::None;
    std::shared_ptr<Texture> picture;           // Picture: something rendered once and kept
    // Shape: triangles in a square from -1 to 1, farthest first, each with how lit it is.
    struct Triangle {
        ImVec2 a, b, c;
        float light = 1;
    };
    std::vector<Triangle> triangles;
    std::vector<float> wave;                    // Wave: loudness, left to right, 0 to 1
    struct Dot {
        ImVec2 at;                              // -1 to 1
        float size = 0.05f;
        ImU32 color = 0;
    };
    std::vector<Dot> dots;                      // Map
    std::vector<ImVec4> boxes;                  // Boxes: x, y, width, height from 0 to 1
    std::vector<std::string> lines;             // Lines: the beginning of the text
    std::string note;                           // under some of them: "1.2 s", "340 triangles"
    uintmax_t builtFromSize = 0;
};

std::unordered_map<std::string, Preview>& cache() {
    static std::unordered_map<std::string, Preview> all;
    return all;
}

Preview makeShape(const MeshData& mesh) {
    Preview preview;
    preview.kind = Preview::Kind::Shape;
    if (mesh.positions.empty() || mesh.indices.size() < 3) return preview;
    AABB bounds = mesh.bounds();
    Vec3 center = bounds.center();
    float radius = std::max(simd::length(bounds.halfExtents()), 1e-4f);
    // Seen from a little to the right and above, the way things are held to be looked at.
    Quat turn = eulerAngles(radians(-24.0f), radians(-34.0f));
    Quat back(-turn.vector.x, -turn.vector.y, -turn.vector.z, turn.vector.w);     // the camera turned one way: the model the other
    Vec3 light = simd::normalize(Vec3{-0.45f, 0.75f, 0.5f});
    size_t triangleCount = mesh.indices.size() / 3;
    size_t stride = std::max<size_t>(1, triangleCount / 1400);     // a picture this small needs no more
    struct Sorted {
        Preview::Triangle triangle;
        float depth;
    };
    std::vector<Sorted> sorted;
    for (size_t t = 0; t < triangleCount; t += stride) {
        Vec3 p[3];
        for (int k = 0; k < 3; k++) {
            uint32_t index = mesh.indices[t * 3 + size_t(k)];
            if (index >= mesh.positions.size()) return preview;
            p[k] = rotate(back, (mesh.positions[index] - center) / radius);
        }
        Vec3 normal = simd::cross(p[1] - p[0], p[2] - p[0]);
        float area = simd::length(normal);
        if (area < 1e-9f) continue;
        normal /= area;
        if (normal.z < 0) continue;                                 // faces away
        Sorted item;
        item.triangle.a = ImVec2(p[0].x, -p[0].y);
        item.triangle.b = ImVec2(p[1].x, -p[1].y);
        item.triangle.c = ImVec2(p[2].x, -p[2].y);
        item.triangle.light = 0.32f + 0.68f * std::max(simd::dot(normal, light), 0.0f);
        item.depth = p[0].z + p[1].z + p[2].z;
        sorted.push_back(item);
    }
    std::sort(sorted.begin(), sorted.end(), [](const Sorted& a, const Sorted& b) { return a.depth < b.depth; });
    for (const Sorted& item : sorted) preview.triangles.push_back(item.triangle);
    preview.note = std::to_string(triangleCount) + (triangleCount == 1 ? " triangle" : " triangles");
    return preview;
}

Preview makeWave(const AudioClip& clip) {
    Preview preview;
    preview.kind = Preview::Kind::Wave;
    const int columns = 40;
    size_t frames = clip.samples.size() / size_t(std::max(clip.channels, 1));
    if (frames == 0) return preview;
    float loudest = 1e-4f;
    for (int column = 0; column < columns; column++) {
        size_t from = frames * size_t(column) / columns, to = std::max(from + 1, frames * size_t(column + 1) / columns);
        float peak = 0;
        size_t step = std::max<size_t>(1, (to - from) / 64);
        for (size_t frame = from; frame < to && frame < frames; frame += step) peak = std::max(peak, std::abs(clip.samples[frame * size_t(clip.channels)]));
        preview.wave.push_back(peak);
        loudest = std::max(loudest, peak);
    }
    for (float& value : preview.wave) value /= loudest;
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f s", double(clip.duration()));
    preview.note = text;
    return preview;
}

/// The triangles of a mesh where an entity puts them, at most `budget` of them.
void addTriangles(std::vector<SoftTriangle>& out, const std::vector<Vec3>& positions, const std::vector<uint32_t>& indices, const Mat4& world, Vec3 color,
                  float glow, size_t budget) {
    size_t count = indices.size() / 3;
    size_t stride = std::max<size_t>(1, count / std::max<size_t>(budget, 1));
    for (size_t t = 0; t < count; t += stride) {
        SoftTriangle triangle;
        triangle.a = transformPoint(world, positions[indices[t * 3]]);
        triangle.b = transformPoint(world, positions[indices[t * 3 + 1]]);
        triangle.c = transformPoint(world, positions[indices[t * 3 + 2]]);
        triangle.color = color;
        triangle.glow = glow;
        out.push_back(triangle);
    }
}

/// A scene or a prefab as a picture: what is in it, in its materials' colors, under its
/// own sun and sky, seen from above and to one side. Empty if nothing in it has a shape.
SoftImage renderScene(Scene& scene, const RenderSettings& settings, int width, int height, bool throughItsCamera);

SoftImage renderSceneFile(EditorContext& editor, const std::string& contents, int size) {
    Scene scene;
    std::vector<NamedMaterial> materials;
    RenderSettings settings;
    if (!readScene(contents, scene, materials, settings, sceneAssets(editor.engine.assets()))) return {};
    scene.updateTransforms();
    return renderScene(scene, settings, size, size, false);
}

/// `throughItsCamera`: as the game's own camera sees it, instead of from above and to one side.
SoftImage renderScene(Scene& scene, const RenderSettings& settings, int width, int height, bool throughItsCamera) {
    std::vector<SoftTriangle> triangles;
    std::vector<SoftThing> things;
    scene.each<MeshRenderer, Transform>([&](Entity, MeshRenderer& renderer, Transform& transform) {
        if (!renderer.mesh || !renderer.isVisible || !transform.isActiveInHierarchy || renderer.mesh->positions().empty()) return;
        Vec4 base = renderer.material ? renderer.material->baseColor * renderer.tint : renderer.tint;
        Vec3 color{base.x, base.y, base.z};
        float glow = 0;
        if (renderer.material) {
            color += renderer.material->emissive;
            glow = renderer.material->isUnlit || simd::length(renderer.material->emissive) > 0.5f ? 1.0f : 0.0f;
        }
        addTriangles(triangles, renderer.mesh->positions(), renderer.mesh->indices(), transform.world, color, glow, 5000);
        const AABB& box = renderer.mesh->bounds();
        Vec3 center = transformPoint(transform.world, box.center());
        things.push_back(SoftThing{center, simd::length(transformPoint(transform.world, box.max) - center)});
    });
    if (triangles.empty()) return {};

    SoftView view;
    // Around the game's own camera if the scene has one: that is where the game happens.
    Vec3 camera{0, 0, 0}, ahead{0, 0, -1};
    float fov = 0.75f;
    bool hasCamera = false;
    scene.each<Camera, Transform>([&](Entity entity, Camera& of, Transform& transform) {
        if (scene.has<EditorOnly>(entity) || (hasCamera && !of.isActive)) return;
        camera = transformPoint(transform.world, Vec3{0, 0, 0});
        ahead = safeNormalize(transformDirection(transform.world, Vec3{0, 0, -1}));
        fov = of.fovY;
        hasCamera = true;
    });
    lookAtThings(view, things, hasCamera ? &camera : nullptr);
    if (throughItsCamera && hasCamera) {
        view.eye = camera;
        view.target = camera + ahead;
        view.fovY = fov;
        view.fogDistance = 0;
    }
    view.skyZenith = settings.sky.zenithColor;
    view.skyHorizon = settings.sky.horizonColor;
    view.skyGround = settings.sky.groundColor;
    scene.each<Light, Transform>([&](Entity, Light& light, Transform& transform) {
        if (light.type != LightType::Directional || !light.isEnabled) return;
        view.sunDirection = safeNormalize(transformDirection(transform.world, Vec3{0, 0, -1}));
        view.sunColor = light.color;
        view.sunIntensity = light.intensity;
    });
    return renderTriangles(triangles, view, width, height);
}

/// A model as a picture: turned a little, in a plain light.
SoftImage renderModel(const MeshData& mesh, int size) {
    if (mesh.positions.empty() || mesh.indices.size() < 3) return {};
    std::vector<SoftTriangle> triangles;
    addTriangles(triangles, mesh.positions, mesh.indices, Mat4(1.0f), Vec3{0.62f, 0.64f, 0.68f}, 0, 60000);
    AABB bounds = mesh.bounds();
    float radius = std::max(simd::length(bounds.halfExtents()), 1e-4f);
    SoftView view;
    view.target = bounds.center();
    view.fovY = 0.5f;
    view.eye = view.target + simd::normalize(Vec3{0.55f, 0.42f, 0.72f}) * (radius / std::sin(view.fovY * 0.5f) * 1.05f);
    view.skyZenith = Vec3{0.12f, 0.14f, 0.18f};
    view.skyHorizon = Vec3{0.2f, 0.22f, 0.26f};
    view.skyGround = Vec3{0.09f, 0.1f, 0.12f};
    view.sunIntensity = 2.6f;
    return renderTriangles(triangles, view, size, size);
}

/// Keeps a rendered picture, if there is one and it can be kept. False otherwise.
bool keepPicture(EditorContext& editor, Preview& preview, const SoftImage& image) {
    if (image.isEmpty()) return false;
    preview.picture = editor.engine.assets().makeTexture(image.rgba, image.width, image.height);
    if (!preview.picture) return false;
    preview.kind = Preview::Kind::Picture;
    return true;
}

/// A scene or a prefab from above: one dot per entity, sized by its scale, colored by what it is.
Preview makeMap(const std::string& contents) {
    Preview preview;
    preview.kind = Preview::Kind::Map;
    struct Item {
        float x = 0, z = 0, size = 1;
        ImU32 color = IM_COL32(150, 158, 172, 255);
    };
    std::vector<Item> items;
    std::istringstream stream(contents);
    std::string line;
    while (std::getline(stream, line)) {
        std::istringstream words(line);
        std::string key;
        words >> key;
        if (key == "entity") {
            items.push_back(Item{});
        } else if (!items.empty() && key == "transform") {
            float v[10] = {0, 0, 0, 0, 0, 0, 1, 1, 1, 1};
            for (float& value : v) words >> value;
            items.back().x = v[0];
            items.back().z = v[2];
            items.back().size = std::max(std::abs(v[7]), std::abs(v[9]));
        } else if (!items.empty() && key == "mesh") {
            items.back().color = IM_COL32(120, 162, 232, 255);
        } else if (!items.empty() && key == "light") {
            items.back().color = IM_COL32(250, 205, 90, 255);
        } else if (!items.empty() && key == "camera") {
            items.back().color = IM_COL32(140, 210, 160, 255);
        } else if (!items.empty() && key == "particles") {
            items.back().color = IM_COL32(255, 150, 80, 255);
        }
    }
    if (items.empty()) return preview;
    // The middle half of the sizes decides the scale, so one huge floor does not shrink the rest to nothing.
    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (const Item& item : items) {
        minX = std::min(minX, item.x), maxX = std::max(maxX, item.x);
        minZ = std::min(minZ, item.z), maxZ = std::max(maxZ, item.z);
    }
    float span = std::max({maxX - minX, maxZ - minZ, 2.0f}) * 0.56f;
    float centerX = (minX + maxX) * 0.5f, centerZ = (minZ + maxZ) * 0.5f;
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.size > b.size; });
    for (const Item& item : items) {
        Preview::Dot dot;
        dot.at = ImVec2((item.x - centerX) / span, (item.z - centerZ) / span);
        dot.size = std::clamp(item.size * 0.5f / span, 0.035f, 0.9f);
        dot.color = item.color;
        preview.dots.push_back(dot);
    }
    preview.note = std::to_string(items.size()) + (items.size() == 1 ? " entity" : " entities");
    return preview;
}

Preview makeBoxes(const std::string& contents) {
    Preview preview;
    preview.kind = Preview::Kind::Boxes;
    std::optional<script::Graph> graph = script::Graph::fromText(contents);
    if (!graph || graph->nodes.empty()) return preview;
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (const script::Node& node : graph->nodes) {
        minX = std::min(minX, node.x), maxX = std::max(maxX, node.x + 200);
        minY = std::min(minY, node.y), maxY = std::max(maxY, node.y + 90);
    }
    float span = std::max(maxX - minX, maxY - minY);
    for (const script::Node& node : graph->nodes) {
        preview.boxes.push_back(ImVec4((node.x - minX) / span, (node.y - minY) / span, 200 / span, 90 / span));
    }
    preview.note = std::to_string(graph->nodes.size()) + " nodes";
    return preview;
}

Preview makeLines(const std::string& contents) {
    Preview preview;
    preview.kind = Preview::Kind::Lines;
    std::istringstream stream(contents);
    std::string line;
    while (preview.lines.size() < 14 && std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Tabs would be drawn as nothing: four spaces each.
        std::string shown;
        for (char c : line) shown += c == '\t' ? std::string("    ") : std::string(1, c);
        preview.lines.push_back(shown.substr(0, 60));
    }
    return preview;
}

const Preview& previewOf(EditorContext& editor, const std::string& path, AssetType type, uintmax_t size) {
    Preview& preview = cache()[path];
    if (preview.kind != Preview::Kind::None && preview.builtFromSize == size) return preview;
    std::string file = editor.assets.absolutePath(path);
    Preview made;
    made.kind = Preview::Kind::Lines;           // so that a file that cannot be read is not read again every frame
    std::string extension = std::filesystem::path(path).extension().string();
    if (type == AssetType::Audio) {
        if (std::shared_ptr<AudioClip> clip = editor.engine.assets().sound(path)) made = makeWave(*clip);
    } else if (size < 8 * 1024 * 1024) {
        std::optional<std::string> contents = text::readFile(file);
        if (contents) {
            if (type == AssetType::Mesh && extension == ".obj") {
                if (std::optional<MeshData> mesh = MeshData::parseOBJ(*contents)) {
                    ImportSettings::load(file).apply(*mesh);
                    if (!keepPicture(editor, made, renderModel(*mesh, 192))) made = makeShape(*mesh);
                }
            } else if (type == AssetType::Scene || type == AssetType::Prefab) {
                // A picture of it; the plan of dots only if nothing in it has a shape to show.
                if (!keepPicture(editor, made, renderSceneFile(editor, *contents, 224))) made = makeMap(*contents);
            } else if (type == AssetType::Graph) {
                made = makeBoxes(*contents);
            } else if (type == AssetType::Script || type == AssetType::Text || type == AssetType::Shader) {
                made = makeLines(*contents);
            }
        }
    }
    made.builtFromSize = size;
    preview = std::move(made);
    return preview;
}

}  // namespace

void forgetAssetPreviews() {
    cache().clear();
    forgetMaterialPreviews();
}

std::shared_ptr<Texture> makeScenePicture(EditorContext& editor, int width, int height) {
    editor.engine.scene.updateTransforms();
    SoftImage image = renderScene(editor.engine.scene, editor.engine.renderer().settings, width, height, true);
    return image.isEmpty() ? nullptr : editor.engine.assets().makeTexture(image.rgba, image.width, image.height);
}

bool drawAssetPreview(EditorContext& editor, ImDrawList* drawList, ImVec2 min, ImVec2 max, const std::string& path, AssetType type, uintmax_t size,
                      ImU32 accent) {
    if (type != AssetType::Mesh && type != AssetType::Audio && type != AssetType::Scene && type != AssetType::Prefab && type != AssetType::Graph &&
        type != AssetType::Script && type != AssetType::Text && type != AssetType::Shader) {
        return false;
    }
    const Preview& preview = previewOf(editor, path, type, size);
    const float width = max.x - min.x, height = max.y - min.y;
    const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    const float half = std::min(width, height) * 0.5f;
    auto background = [&](ImU32 color) { drawList->AddRectFilled(min, max, color, 6.0f); };
    drawList->PushClipRect(min, max, true);
    bool drew = true;
    switch (preview.kind) {
        case Preview::Kind::Shape: {
            if (preview.triangles.empty()) {
                drew = false;
                break;
            }
            background(IM_COL32(44, 49, 58, 255));
            float scale = half * 0.86f;
            for (const Preview::Triangle& triangle : preview.triangles) {
                int value = int(triangle.light * 255.0f);
                ImU32 color = IM_COL32(int(value * 0.74f), int(value * 0.8f), int(value * 0.9f), 255);
                drawList->AddTriangleFilled(ImVec2(center.x + triangle.a.x * scale, center.y + triangle.a.y * scale),
                                            ImVec2(center.x + triangle.b.x * scale, center.y + triangle.b.y * scale),
                                            ImVec2(center.x + triangle.c.x * scale, center.y + triangle.c.y * scale), color);
            }
            break;
        }
        case Preview::Kind::Wave: {
            if (preview.wave.empty()) {
                drew = false;
                break;
            }
            // The colors of the wave in the Viewer: orange on near-black.
            background(IM_COL32(24, 26, 31, 255));
            accent = IM_COL32(255, 122, 47, 255);
            drawList->AddLine(ImVec2(min.x + 4, center.y), ImVec2(max.x - 4, center.y), IM_COL32(255, 122, 47, 70));
            float step = (width - 12) / float(preview.wave.size());
            for (size_t i = 0; i < preview.wave.size(); i++) {
                float x = min.x + 6 + (float(i) + 0.5f) * step;
                float reach = std::max(preview.wave[i] * height * 0.4f, 1.0f);
                drawList->AddLine(ImVec2(x, center.y - reach), ImVec2(x, center.y + reach), accent, std::max(step * 0.6f, 1.0f));
            }
            break;
        }
        case Preview::Kind::Map: {
            if (preview.dots.empty()) {
                drew = false;
                break;
            }
            background(IM_COL32(30, 34, 42, 255));
            // A faint grid: this is a plan.
            for (int i = 1; i < 4; i++) {
                float t = float(i) / 4.0f;
                drawList->AddLine(ImVec2(min.x + width * t, min.y), ImVec2(min.x + width * t, max.y), IM_COL32(255, 255, 255, 12));
                drawList->AddLine(ImVec2(min.x, min.y + height * t), ImVec2(max.x, min.y + height * t), IM_COL32(255, 255, 255, 12));
            }
            for (const Preview::Dot& dot : preview.dots) {
                ImVec2 at(center.x + dot.at.x * half, center.y + dot.at.y * half);
                float radius = dot.size * half;
                ImU32 color = dot.size > 0.3f ? (dot.color & 0x00FFFFFF) | 0x50000000 : dot.color;      // big things are floors: faint
                drawList->AddRectFilled(ImVec2(at.x - radius, at.y - radius), ImVec2(at.x + radius, at.y + radius), color, std::min(radius * 0.3f, 3.0f));
            }
            if (type == AssetType::Prefab) drawList->AddRect(min, max, accent, 6.0f, ImDrawFlags_None, 1.5f);
            break;
        }
        case Preview::Kind::Boxes: {
            if (preview.boxes.empty()) {
                drew = false;
                break;
            }
            background(IM_COL32(30, 34, 42, 255));
            float inner = std::min(width, height) - 14;
            ImVec2 origin(center.x - inner * 0.5f, center.y - inner * 0.5f);
            for (const ImVec4& box : preview.boxes) {
                ImVec2 a(origin.x + box.x * inner, origin.y + box.y * inner), b(a.x + std::max(box.z * inner, 6.0f), a.y + std::max(box.w * inner, 4.0f));
                drawList->AddRectFilled(a, b, IM_COL32(58, 64, 76, 255), 2.0f);
                drawList->AddRectFilled(a, ImVec2(b.x, a.y + std::max((b.y - a.y) * 0.3f, 2.0f)), accent, 2.0f);
            }
            break;
        }
        case Preview::Kind::Lines: {
            if (preview.lines.empty()) {
                drew = false;
                break;
            }
            // A sheet of paper with the beginning of what is written on it.
            ImVec2 pageMin(min.x + width * 0.12f, min.y + 2), pageMax(max.x - width * 0.12f, max.y - 2);
            drawList->AddRectFilled(pageMin, pageMax, IM_COL32(36, 40, 48, 255), 4.0f);
            drawList->AddRectFilled(pageMin, ImVec2(pageMax.x, pageMin.y + 4), accent, 4.0f, ImDrawFlags_RoundCornersTop);
            float fontSize = std::clamp(height / 11.0f, 5.5f, 9.0f);
            drawList->PushClipRect(ImVec2(pageMin.x + 4, pageMin.y + 6), ImVec2(pageMax.x - 3, pageMax.y - 3), true);
            float y = pageMin.y + 8;
            for (const std::string& line : preview.lines) {
                if (y > pageMax.y) break;
                bool isComment = line.find("//") != std::string::npos || (!line.empty() && line[0] == '#');
                drawList->AddText(ImGui::GetFont(), fontSize, ImVec2(pageMin.x + 5, y), isComment ? IM_COL32(110, 150, 120, 255) : IM_COL32(170, 178, 192, 255),
                                  line.c_str());
                y += fontSize * 1.25f;
            }
            drawList->PopClipRect();
            break;
        }
        case Preview::Kind::Picture: {
            if (!preview.picture) {
                drew = false;
                break;
            }
            drawList->AddImageRounded(ImGuiLayer::textureId(preview.picture.get()), min, max, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 6.0f);
            if (type == AssetType::Prefab) drawList->AddRect(min, max, accent, 6.0f, ImDrawFlags_None, 1.5f);
            break;
        }
        case Preview::Kind::None: drew = false; break;
    }
    drawList->PopClipRect();
    return drew;
}

std::string assetPreviewNote(EditorContext& editor, const std::string& path, AssetType type, uintmax_t size) {
    if (type != AssetType::Mesh && type != AssetType::Audio && type != AssetType::Scene && type != AssetType::Prefab && type != AssetType::Graph) return {};
    return previewOf(editor, path, type, size).note;
}

}  // namespace molten::editor
