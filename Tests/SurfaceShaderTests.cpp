// SurfaceShaderTests.cpp
// Shaders written by hand: reading a `.shader` file and writing the code the GPU compiles.
#include "Test.hpp"

#include "Rendering/SurfaceShader.hpp"
#include "Scene/SceneFile.hpp"

#include <filesystem>
#include <fstream>

using namespace molten;

TEST(surface_shader_properties_are_read_and_taken_out_of_the_code) {
    std::string text = "// lava\nproperties {\n    float speed = 2 range 0 10   // how fast\n    color hot = 1 0.5 0.25\n    vector flow = 1, 0, 0, 0\n}\n"
                       "Surface surface(Surface s, Input in, Props props) { return s; }\n";
    SurfaceShader shader = SurfaceShader::parse("Shaders/Lava.shader", text);
    CHECK(shader.error.empty());
    CHECK(shader.properties.size() == 3);
    if (shader.properties.size() == 3) {
        CHECK(shader.properties[0].name == "speed" && shader.properties[0].kind == SurfaceProperty::Kind::Float);
        CHECK_NEAR(shader.properties[0].value.x, 2.0f, 1e-6f);
        CHECK(shader.properties[0].hasRange && shader.properties[0].max == 10.0f);
        CHECK(shader.properties[1].kind == SurfaceProperty::Kind::Color);
        CHECK_NEAR(shader.properties[1].value.y, 0.5f, 1e-6f);
        CHECK_NEAR(shader.properties[1].value.w, 1.0f, 1e-6f);        // a color given three numbers is opaque
        CHECK(shader.properties[2].kind == SurfaceProperty::Kind::Vector && shader.properties[2].value.x == 1.0f);
    }
    // The block is gone from the code, and every line is where it was.
    CHECK(shader.code.find("properties") == std::string::npos);
    CHECK(shader.code.find("range") == std::string::npos);
    CHECK(std::count(shader.code.begin(), shader.code.end(), '\n') == std::count(text.begin(), text.end(), '\n'));
    CHECK(shader.code.find("Surface surface(") != std::string::npos);
    // A word "properties" in a comment or in code is not a block.
    SurfaceShader plain = SurfaceShader::parse("a", "// no properties { here }\nSurface surface(Surface s, Input in, Props props) { return s; }\n");
    CHECK(plain.properties.empty() && plain.code.find("no properties") != std::string::npos);
    // Mistakes are said, not swallowed.
    CHECK(!SurfaceShader::parse("a", "properties {\n  texture map\n}\n").error.empty());
    CHECK(!SurfaceShader::parse("a", "properties {\n  float a\n  float a\n}\n").error.empty());
}

TEST(surface_shader_code_runs_each_by_its_place) {
    std::vector<SurfaceShader> shaders;
    shaders.push_back(SurfaceShader::parse("Shaders/A.shader", "properties {\n float speed = 1\n color glow = 1 0 0 1\n}\nSurface surface(Surface s, Input in, Props props) { return s; }\n"));
    shaders.push_back(SurfaceShader::parse("Shaders/B.shader", "Surface surface(Surface s, Input in, Props props) { return s; }\n"));
    std::string code = surfaceShaderCode(shaders);
    CHECK(code.find("namespace molten_surface_1 {") != std::string::npos);
    CHECK(code.find("namespace molten_surface_2 {") != std::string::npos);
    CHECK(code.find("struct Props { float speed; float4 glow; };") != std::string::npos);
    CHECK(code.find("props.speed = m.customValues[0].x;") != std::string::npos);
    CHECK(code.find("props.glow = m.customValues[1];") != std::string::npos);
    CHECK(code.find("case 2: return molten_surface_2::run(") != std::string::npos);
    CHECK(code.find("#line 1 \"Shaders/A.shader\"") != std::string::npos);
    CHECK(surfaceShaderCode({}).empty());
    // What a new shader starts as is itself a shader with properties.
    SurfaceShader fresh = SurfaceShader::parse("New.shader", newSurfaceShaderText());
    CHECK(fresh.error.empty() && fresh.properties.size() == 3);
}

TEST(surface_shaders_keep_their_place_and_notice_a_saved_file) {
    namespace fs = std::filesystem;
    fs::path folder = fs::temp_directory_path() / "molten-surface-test";
    fs::remove_all(folder);
    fs::create_directories(folder);
    auto write = [&](const char* name, const std::string& text) { std::ofstream(folder / name) << text; };
    write("A.shader", "properties {\n float a = 1\n}\nSurface surface(Surface s, Input in, Props props) { return s; }\n");
    write("B.shader", "Surface surface(Surface s, Input in, Props props) { return s; }\n");
    SurfaceShaderSet set;
    set.pathOf = [&](const std::string& name) { return (folder / name).string(); };
    CHECK(set.use("A.shader") == 1);
    CHECK(set.use("B.shader") == 2);
    CHECK(set.use("A.shader") == 1);
    CHECK(set.takeChanges());
    CHECK(!set.takeChanges());
    CHECK(set.shader(1) && set.shader(1)->properties.size() == 1);
    // A file that is not there has a place too, and does nothing.
    CHECK(set.use("Missing.shader") == 3);
    CHECK(set.shader(3) && !set.shader(3)->error.empty());
    CHECK(set.takeChanges());
    // Saved with another property: read again.
    write("A.shader", "properties {\n float a = 1\n float b = 2\n}\nSurface surface(Surface s, Input in, Props props) { return s; }\n");
    fs::last_write_time(folder / "A.shader", fs::file_time_type::clock::now() + std::chrono::seconds(5));
    CHECK(set.takeChanges());
    CHECK(set.shader(1)->properties.size() == 2);
    fs::remove_all(folder);
}

TEST(materials_remember_their_shader_and_its_values) {
    Material material;
    material.shader = "Shaders/Lava.shader";
    material.setShaderValue("speed", Vec4{3, 0, 0, 0});
    material.setShaderValue("hot", Vec4{1, 0.5f, 0.25f, 1});
    material.setShaderValue("speed", Vec4{4, 0, 0, 0});
    std::string text = writeMaterial(material);
    Material back;
    CHECK(readMaterial(text, back));
    CHECK(back.shader == "Shaders/Lava.shader");
    CHECK(back.shaderValues.size() == 2);
    CHECK(back.shaderValue("speed") && back.shaderValue("speed")->x == 4.0f);
    CHECK(back.shaderValue("hot") && back.shaderValue("hot")->y == 0.5f);
    CHECK(!back.shaderValue("nothing"));
    // A material without one says nothing about it.
    CHECK(writeMaterial(Material{}).find("surfaceShader") == std::string::npos);
}
