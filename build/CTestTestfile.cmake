# CMake generated Testfile for 
# Source directory: /Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine
# Build directory: /Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test("unit" "/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/build/moltentests")
set_tests_properties("unit" PROPERTIES  LABELS "unit" _BACKTRACE_TRIPLES "/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/CMakeLists.txt;238;add_test;/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/CMakeLists.txt;0;")
add_test("editor-starts" "/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/build/molten" "--capture" "/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/build/editor.png" "--frames" "5")
set_tests_properties("editor-starts" PROPERTIES  LABELS "gpu" TIMEOUT "600" _BACKTRACE_TRIPLES "/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/CMakeLists.txt;240;add_test;/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/CMakeLists.txt;0;")
add_test("player-plays-the-sample-scene" "/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/build/molten-player" "--capture" "/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/build/player.png" "--frames" "30")
set_tests_properties("player-plays-the-sample-scene" PROPERTIES  LABELS "gpu" TIMEOUT "600" _BACKTRACE_TRIPLES "/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/CMakeLists.txt;242;add_test;/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/CMakeLists.txt;0;")
add_test("marble-demo" "/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/build/marble" "--demo" "--capture" "/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/build/marble-demo.png" "--frames" "2400")
set_tests_properties("marble-demo" PROPERTIES  LABELS "gpu" TIMEOUT "600" _BACKTRACE_TRIPLES "/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/CMakeLists.txt;245;add_test;/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/CMakeLists.txt;0;")
add_test("fpv-starts-a-race" "/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/build/fpv" "--race" "--capture" "/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/build/fpv-race.png" "--frames" "240")
set_tests_properties("fpv-starts-a-race" PROPERTIES  LABELS "gpu" TIMEOUT "600" _BACKTRACE_TRIPLES "/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/CMakeLists.txt;247;add_test;/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/CMakeLists.txt;0;")
subdirs("_deps/glfw-build")
