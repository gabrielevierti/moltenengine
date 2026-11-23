// Shader graphs (Rendering/ShaderGraph.hpp): drawn as boxes, read as a surface shader.
#include "Test.hpp"

#include "Rendering/ShaderGraph.hpp"
#include "Rendering/SurfaceShader.hpp"

#include <cstdlib>
#include <filesystem>
#include "Core/TextFile.hpp"
#include <fstream>

using namespace molten;
using script::Graph;

namespace {

Graph shaderGraph() {
    Graph graph;
    graph.library = &shaderNodes();
    return graph;
}

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

}  // namespace

TEST(a_new_shader_graph_makes_a_shader_with_its_property) {
    std::optional<Graph> graph = Graph::fromText(newShaderGraphText());
    CHECK(graph.has_value());
    if (!graph) return;
    graph->library = &shaderNodes();
    std::string error;
    std::string text = shaderTextFromGraph(*graph, "Shaders/New.shadergraph", &error);
    CHECK(error.empty());
    CHECK(has(text, "s.color = props.color.rgb;") || has(text, "s.color = n"));
    CHECK(has(text, "s.roughness ="));
    CHECK(!has(text, "s.metallic"));                 // nothing wired or typed there: the material's stays
    SurfaceShader shader = SurfaceShader::parse("Shaders/New.shadergraph", text);
    CHECK(shader.error.empty() && shader.properties.size() == 1 && shader.properties[0].name == "color");
}

TEST(a_shader_graph_of_lava_writes_the_code_for_it) {
    // Moving noise, colored by a property, glowing; a rim of light where the surface turns away.
    Graph graph = shaderGraph();
    const int surface = graph.addNode("SG.Surface", 600, 100).id;
    const int scroll = graph.addNode("SG.ScrollUV", 100, 100).id;
    const int noise = graph.addNode("SG.Noise", 250, 100).id;
    const int hot = graph.addNode("SG.ColorProperty", 250, 250).id;
    const int glow = graph.addNode("SG.Multiply", 400, 150).id;
    const int rim = graph.addNode("SG.Fresnel", 400, 300).id;
    const int speed = graph.addNode("SG.FloatProperty", 100, 300).id;
    graph.find(hot)->values["name"] = "hot color";                   // made a name the code can use
    graph.find(speed)->values["name"] = "speed";
    graph.find(speed)->values["max"] = "4";
    CHECK(graph.connect(scroll, "uv", noise, "uv"));
    CHECK(graph.connect(noise, "value", glow, "a"));                // a number where three are wanted
    CHECK(graph.connect(hot, "color", glow, "b"));
    CHECK(graph.connect(glow, "result", surface, "emissive"));
    CHECK(graph.connect(rim, "value", surface, "alpha"));
    CHECK(graph.connect(speed, "value", noise, "scale"));
    CHECK(!graph.connect(surface, "color", noise, "uv"));          // the Surface node has no outputs
    std::string error;
    std::string text = shaderTextFromGraph(graph, "Shaders/Lava.shadergraph", &error);
    CHECK(error.empty());
    CHECK(has(text, "color hot_color = 1.0 1.0 1.0 1"));
    CHECK(has(text, "float speed = 1.0 range 0.0 4.0"));
    CHECK(has(text, "in.time"));
    CHECK(has(text, "noise("));
    CHECK(has(text, "s.emissive ="));
    CHECK(has(text, "s.alpha ="));
    SurfaceShader shader = SurfaceShader::parse("Shaders/Lava.shadergraph", text);
    CHECK(shader.error.empty() && shader.properties.size() == 2);
    // For the Metal check (softmetal/customcheck.sh): the code as a .shader file.
    if (const char* out = std::getenv("MOLTEN_SHADER_GRAPH_OUT")) std::ofstream(out) << text;
    // Saved and read back: the same code.
    std::optional<Graph> again = Graph::fromText(graph.toText());
    CHECK(again.has_value());
    if (again) {
        again->library = &shaderNodes();
        CHECK(shaderTextFromGraph(*again, "Shaders/Lava.shadergraph") == text);
    }
}

TEST(a_shader_graph_in_a_loop_or_without_a_surface_says_why) {
    Graph graph = shaderGraph();
    const int a = graph.addNode("SG.Add", 0, 0).id;
    const int b = graph.addNode("SG.Add", 200, 0).id;
    std::string error;
    CHECK(shaderTextFromGraph(graph, "x", &error).empty() && !error.empty());
    const int surface = graph.addNode("SG.Surface", 400, 0).id;
    graph.connect(a, "result", b, "a");
    graph.connect(b, "result", a, "a");
    graph.connect(b, "result", surface, "color");
    error.clear();
    CHECK(shaderTextFromGraph(graph, "x", &error).empty());
    CHECK(error.find("loop") != std::string::npos);
}

TEST(a_shader_file_can_be_a_graph_or_code) {
    CHECK(isShaderGraphText(newShaderGraphText()));
    CHECK(!isShaderGraphText(newSurfaceShaderText()));
    std::string folder = std::filesystem::temp_directory_path().string() + "/molten-shader-kinds";
    std::filesystem::create_directories(folder);
    text::writeFile(folder + "/Drawn.shader", newShaderGraphText());
    text::writeFile(folder + "/Written.shader", newSurfaceShaderText());
    text::writeFile(folder + "/Old.shadergraph", newShaderGraphText());
    CHECK(isShaderGraphFile(folder + "/Drawn.shader"));
    CHECK(!isShaderGraphFile(folder + "/Written.shader"));
    CHECK(isShaderGraphFile(folder + "/Old.shadergraph"));
    CHECK(!isShaderGraphFile(folder + "/Missing.shader"));
    // A material's shader set reads the graph in a .shader as code made from it.
    SurfaceShaderSet set;
    set.pathOf = [folder](const std::string& name) { return folder + "/" + name; };
    set.use("Drawn.shader");
    CHECK(set.code().find("Surface surface") != std::string::npos);
    std::filesystem::remove_all(folder);
}
