#include "Test.hpp"

#include "Assets/Gltf.hpp"
#include "Assets/ModelFormats.hpp"
#include "Core/TextFile.hpp"
#include "Physics/Physics.hpp"
#include "Rendering/Renderer.hpp"
#include "Scene/SceneFile.hpp"

#include <cstring>
#include <filesystem>

using namespace molten;
namespace fs = std::filesystem;

TEST(every_kind_of_model_file_is_known_and_an_obj_is_one_mesh_unless_a_part_is_asked) {
    for (const char* name : {"a.gltf", "a.glb", "a.fbx", "a.FBX", "a.dae", "a.obj", "a.stl", "a.ply"}) CHECK(isModelFile(name));
    CHECK(!isModelFile("a.png") && !isModelFile("a.blend"));
    // An OBJ as a whole is the mesh it always was; one of its parts is read as a model of parts.
    CHECK(!isGltfFile("Models/boat.obj") && isGltfFile("Models/boat.obj#2"));
    CHECK(isGltfFile("Models/boat.fbx") && isGltfFile("Models/boat.fbx#0") && isGltfFile("Models/boat.stl"));
}

TEST(stl_files_text_and_binary_are_read) {
    std::string text = "solid wedge\n facet normal 0 0 1\n  outer loop\n   vertex 0 0 0\n   vertex 1 0 0\n   vertex 0 1 0\n  endloop\n endfacet\n"
                       " facet normal 0 0 1\n  outer loop\n   vertex 1 0 0\n   vertex 1 1 0\n   vertex 0 1 0\n  endloop\n endfacet\nendsolid wedge\n";
    std::optional<GltfModel> read = parseStl(text);
    CHECK(read && read->meshes.size() == 1 && read->meshes[0][0].mesh.indices.size() == 6);
    if (read) CHECK_NEAR(read->meshes[0][0].mesh.normals[0].z, 1.0, 1e-5);      // counter-clockwise seen from +z: it faces +z
    // The same two triangles, binary: 80 bytes, a count, then 50 bytes each.
    std::string binary(84 + 2 * 50, '\0');
    uint32_t count = 2;
    std::memcpy(&binary[80], &count, 4);
    const float corners[2][12] = {{0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0}, {0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0}};
    for (int t = 0; t < 2; t++) std::memcpy(&binary[84 + size_t(t) * 50], corners[t], 48);
    std::optional<GltfModel> fromBinary = parseStl(binary);
    CHECK(fromBinary && fromBinary->meshes[0][0].mesh.indices.size() == 6);
    if (fromBinary) CHECK(fromBinary->assembled(fromBinary->restPose()).bounds().max.y == 1.0f);
    CHECK(!parseStl("solid nothing\nendsolid nothing\n"));
}

TEST(ply_files_are_read_with_their_polygons_made_triangles) {
    std::string text = "ply\nformat ascii 1.0\nelement vertex 4\nproperty float x\nproperty float y\nproperty float z\nproperty float s\nproperty float t\n"
                       "element face 1\nproperty list uchar int vertex_indices\nend_header\n"
                       "0 0 0 0 0\n1 0 0 1 0\n1 1 0 1 1\n0 1 0 0 1\n4 0 1 2 3\n";
    std::optional<GltfModel> read = parsePly(text);
    CHECK(read && read->meshes[0][0].mesh.indices.size() == 6);
    if (!read) return;
    const MeshData& mesh = read->meshes[0][0].mesh;
    CHECK_NEAR(mesh.uvs[0].y, 1.0, 1e-6);                       // from the bottom left to the top left
    CHECK(mesh.normals.size() == 4);                            // none in the file: worked out
    CHECK(!parsePly("ply\nformat ascii 1.0\nelement vertex 1\nproperty float x\nelement face 1\nproperty list uchar int vertex_indices\nend_header\n0\n3 0 1 2\n"));
}

TEST(collada_files_are_read_with_their_nodes_materials_and_axes) {
    std::string text = R"(<?xml version="1.0" encoding="utf-8"?>
<COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">
  <asset><unit name="centimeter" meter="0.01"/><up_axis>Z_UP</up_axis></asset>
  <library_images><image id="paint"><init_from>Textures/paint.png</init_from></image></library_images>
  <library_effects>
    <effect id="red-fx"><profile_COMMON>
      <newparam sid="surface"><surface type="2D"><init_from>paint</init_from></surface></newparam>
      <newparam sid="sampler"><sampler2D><source>surface</source></sampler2D></newparam>
      <technique sid="common"><lambert><diffuse><texture texture="sampler" texcoord="UV"/></diffuse></lambert></technique>
    </profile_COMMON></effect>
  </library_effects>
  <library_materials><material id="red" name="Red"><instance_effect url="#red-fx"/></material></library_materials>
  <library_geometries><geometry id="tri"><mesh>
    <source id="tri-p"><float_array id="tri-pa" count="12">0 0 0 100 0 0 100 0 200 0 0 200</float_array>
      <technique_common><accessor source="#tri-pa" count="4" stride="3"/></technique_common></source>
    <vertices id="tri-v"><input semantic="POSITION" source="#tri-p"/></vertices>
    <polylist material="mat" count="1"><input semantic="VERTEX" source="#tri-v" offset="0"/><vcount>4</vcount><p>0 1 2 3</p></polylist>
  </mesh></geometry></library_geometries>
  <library_visual_scenes><visual_scene id="scene">
    <node id="wall" name="Wall"><translate>0 0 50</translate>
      <instance_geometry url="#tri"><bind_material><technique_common><instance_material symbol="mat" target="#red"/></technique_common></bind_material></instance_geometry>
    </node>
  </visual_scene></library_visual_scenes>
  <scene><instance_visual_scene url="#scene"/></scene>
</COLLADA>)";
    std::string error;
    std::optional<GltfModel> read = parseCollada(text, &error);
    CHECK(read);
    if (!read) return;
    std::vector<GltfModel::Part> parts = read->parts();
    CHECK(parts.size() == 1 && parts[0].name == "Wall" && parts[0].material == 0);
    CHECK(read->materials.size() == 1 && read->materials[0].name == "Red" && read->materials[0].colorMap.file == "Textures/paint.png");
    // Centimeters made meters, and what stood along z stands along y: 2 meters up from half a meter.
    AABB box = read->part(parts[0]).bounds();
    CHECK_NEAR(box.min.y, 0.5, 1e-4);
    CHECK_NEAR(box.max.y, 2.5, 1e-4);
    CHECK_NEAR(box.max.x, 1.0, 1e-4);
    CHECK(read->part(parts[0]).indices.size() == 6);
    CHECK(!parseCollada("<notcollada/>"));
}

TEST(obj_files_with_their_materials_and_fbx_files_are_read_as_models_of_parts) {
    fs::path folder = fs::temp_directory_path() / "molten-model-formats";
    fs::create_directories(folder);
    text::writeFile((folder / "boat.mtl").string(),
                    "newmtl Hull\nNs 250\nKd 0.9 0.9 0.9\nd 0\nmap_Kd Hull_baseColor.png\n\nnewmtl Lamp\nNs 1\nKd 0.8 0 0\nKe 1 0.1 0\nd 1\n");
    text::writeFile((folder / "boat.obj").string(), "mtllib boat.mtl\no Body\nv 0 0 0\nv 1 0 0\nv 1 0 -1\nv 0 0 -1\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
                                                    "usemtl Hull\nf 1/1 2/2 3/3 4/4\no Light\nv 0 1 0\nv 0.2 1 0\nv 0 1.2 0\nusemtl Lamp\nf 5 6 7\n");
    std::optional<GltfModel> obj = GltfModel::load((folder / "boat.obj").string());
    CHECK(obj);
    if (obj) {
        std::vector<GltfModel::Part> parts = obj->parts();
        CHECK(parts.size() == 2 && obj->materials.size() == 2);
        if (parts.size() == 2 && obj->materials.size() == 2) {
            const GltfMaterial& hull = obj->materials[size_t(parts[0].material)];
            const GltfMaterial& lamp = obj->materials[size_t(parts[1].material)];
            CHECK(hull.name == "Hull" && hull.colorMap.file == "Hull_baseColor.png" && !hull.isBlended);     // "d 0": a mistake, taken as opaque
            CHECK_NEAR(hull.roughness, 0.5, 1e-3);                  // Ns 250, as Blender writes 0.5
            CHECK(lamp.name == "Lamp" && lamp.emissive.x > 0.9f && lamp.baseColor.x > 0.7f);
            CHECK(obj->part(parts[0]).indices.size() == 6 && obj->part(parts[1]).indices.size() == 3);
            CHECK_NEAR(obj->part(parts[0]).uvs[0].y, 1.0, 1e-5);   // from the bottom left to the top left
        }
    }
    // FBX: in meters, Y up; the model's node where the file puts it, its material's color.
    text::writeFile((folder / "square.fbx").string(), R"(; FBX 7.4.0 project file
FBXHeaderExtension:  {
	FBXHeaderVersion: 1003
	FBXVersion: 7400
}
GlobalSettings:  {
	Version: 1000
	Properties70:  {
		P: "UpAxis", "int", "Integer", "",1
		P: "UpAxisSign", "int", "Integer", "",1
		P: "FrontAxis", "int", "Integer", "",2
		P: "FrontAxisSign", "int", "Integer", "",1
		P: "CoordAxis", "int", "Integer", "",0
		P: "CoordAxisSign", "int", "Integer", "",1
		P: "UnitScaleFactor", "double", "Number", "",1
	}
}
Objects:  {
	Geometry: 100, "Geometry::Square", "Mesh" {
		Vertices: *12 {
			a: 0,0,0,100,0,0,100,0,-100,0,0,-100
		}
		PolygonVertexIndex: *4 {
			a: 0,1,2,-4
		}
	}
	Model: 200, "Model::Square", "Mesh" {
		Version: 232
		Properties70:  {
			P: "Lcl Translation", "Lcl Translation", "", "A",0,50,0
		}
	}
	Material: 300, "Material::Red", "" {
		Version: 102
		ShadingModel: "phong"
		Properties70:  {
			P: "DiffuseColor", "Color", "", "A",1,0,0
		}
	}
}
Connections:  {
	C: "OO",200,0
	C: "OO",100,200
	C: "OO",300,200
}
)");
    std::string error;
    std::optional<GltfModel> fbx = GltfModel::load((folder / "square.fbx").string(), &error);
    CHECK(fbx);
    if (fbx) {
        std::vector<GltfModel::Part> parts = fbx->parts();
        CHECK(parts.size() == 1 && parts[0].name == "Square" && fbx->materials.size() == 1);
        if (!parts.empty()) {
            AABB box = fbx->part(parts[0]).bounds();
            CHECK_NEAR(box.min.y, 0.5, 1e-4);                       // centimeters (a unit of 1): 50 up is half a meter
            CHECK_NEAR(box.max.x, 1.0, 1e-4);
            CHECK(fbx->part(parts[0]).normals[0].y > 0.99f);        // counter-clockwise from above: facing up
        }
        if (!fbx->materials.empty()) CHECK(fbx->materials[0].name == "Red" && fbx->materials[0].baseColor.x > 0.99f && fbx->materials[0].baseColor.y < 0.01f);
    }
    CHECK(!GltfModel::load((folder / "missing.fbx").string()));
    fs::remove_all(folder);
}

TEST(a_mesh_collider_is_where_its_model_is_whatever_offset_it_was_given) {
    // Scenes of before gave a mesh collider the model's middle as an offset when it was added:
    // read now, it has none, and its shape is placed where the entity is.
    std::string text = "molten-scene 1\nentity \"Rock\"\n  transform 2 0 0 0 0 0 1 1 1 1\n  collider mesh \"cube\" 0 1.5 0 0 1\n";
    Scene scene;
    std::vector<NamedMaterial> none;
    RenderSettings settings;
    CHECK(readScene(text, scene, none, settings, SceneAssets{}));
    bool found = false;
    scene.each<Collider>([&](Entity, Collider& collider) {
        found = true;
        CHECK(collider.shape == ColliderShape::Mesh && collider.isConvex);
        CHECK(collider.offset.x == 0 && collider.offset.y == 0 && collider.offset.z == 0);
    });
    CHECK(found);
    Collider given = Collider::meshOf("cube");
    given.offset = Vec3{0, 3, 0};
    ShapeInstance placed = Physics::makeShape(given, Vec3{2, 0, 0}, Quat{});
    CHECK(placed.center.x == 2.0f && placed.center.y == 0.0f);
    Collider box = Collider::box(Vec3{1, 1, 1});
    box.offset = Vec3{0, 3, 0};
    CHECK(Physics::makeShape(box, Vec3{2, 0, 0}, Quat{}).center.y == 3.0f);                  // other shapes keep theirs
}

TEST(the_sea_says_whether_the_camera_shows_going_through_its_surface) {
    RenderSettings settings;
    settings.sea.enabled = true;
    CHECK(settings.sea.lensThroughSurface);                     // on unless turned off
    settings.sea.lensThroughSurface = false;
    Scene empty, read;
    std::vector<NamedMaterial> none;
    RenderSettings back;
    CHECK(readScene(writeScene(empty, none, settings), read, none, back, SceneAssets{}) && back.sea.enabled && !back.sea.lensThroughSurface);
    settings.sea.lensThroughSurface = true;
    CHECK(readScene(writeScene(empty, none, settings), read, none, back, SceneAssets{}) && back.sea.lensThroughSurface);
}
