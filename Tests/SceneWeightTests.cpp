// What a scene weighs.
#include "Test.hpp"

#include "../Editor/SceneWeight.hpp"

using namespace molten;

TEST(a_scene_is_weighed_object_by_object) {
    int rock = 0, tree = 0, bark = 0, leaves = 0, stone = 0, wood = 0;      // their addresses stand for the assets
    std::vector<WeightPart> parts;
    parts.push_back(WeightPart{Entity{}, "Rock A", &rock, 500, 1, 30000, {{&bark, 4000}}, &stone});
    parts.push_back(WeightPart{Entity{}, "Rock B", &rock, 500, 1, 30000, {{&bark, 4000}}, &stone});
    parts.push_back(WeightPart{Entity{}, "Forest", &tree, 200, 1000, 9000, {{&bark, 4000}, {&leaves, 8000}}, &wood});
    WeightReport report = weighScene(parts);
    CHECK(report.rows.size() == 3 && report.rows[0].name == "Forest" && report.rows[0].triangles == 200000);
    CHECK(report.triangles == 201000 && report.draws == 2 && report.rows[1].drawnWith == 1);
    CHECK(report.meshBytes == 39000 && report.meshes == 2);             // the rock counted once
    CHECK(report.textureBytes == 12000 && report.textures == 2);        // the bark counted once
    CHECK(report.rows[1].sharedWith == 1 && report.rows[0].sharedWith == 0);
    CHECK(weighScene(parts, WeightOrder::Name).rows[0].name == "Forest");
    CHECK(weighScene(parts, WeightOrder::Memory).rows[0].name == "Rock A");   // 34000 against 21000
    CHECK(pictureBytes(1024, 1024, 4, false) == 4194304 && pictureBytes(1024, 1024, 4, true) == 4194304 + 1398101);
    CHECK(bytesText(1572864) == "1.5 MB" && bytesText(2048) == "2 KB" && bytesText(12) == "12 B");
}
