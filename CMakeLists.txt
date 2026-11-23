# Molten Metal Engine: a game engine for Apple silicon, built on Metal.
#
# On a Mac:
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j
#   ./build/molten                  the editor
#   ctest --test-dir build -L unit
#
# For iPhone and iPad (makes an Xcode project; see Docs/iOS.md):
#   cmake -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DMOLTEN_IOS_PROJECT=/path/to/MyGame
#
# The libraries it uses are downloaded by CMake the first time (see below).
cmake_minimum_required(VERSION 3.24)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(MOLTEN_IOS ON)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 16.0 CACHE STRING "Oldest iOS the programs run on")
else()
    set(MOLTEN_IOS OFF)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0 CACHE STRING "Oldest macOS the programs run on")
endif()
project(MoltenMetalEngine VERSION 1.3.0 LANGUAGES C CXX OBJC OBJCXX)

if(NOT APPLE)
    message(FATAL_ERROR "Metal only exists on Apple platforms: Molten builds on macOS, for macOS and iOS.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(MOLTEN_BUILD_EXAMPLE_PLUGIN "Build the example editor plugin (Plugins/SceneTools): a Tools menu, to learn from" OFF)
option(MOLTEN_BUILD_TESTS "Build the unit tests (off: only the engine, the editor and the player are built)" OFF)

# ---------------------------------------------------------------- dependencies
include(FetchContent)

# metal-cpp: Apple's C++ interface to Metal. Apple publishes it as a download rather
# than a repository, so this fetches a mirror at a fixed commit. To use the copy from
# https://developer.apple.com/metal/cpp/ instead, pass -DMOLTEN_METAL_CPP_DIR=/path/to/metal-cpp.
set(MOLTEN_METAL_CPP_DIR "" CACHE PATH "A metal-cpp folder to use instead of downloading the mirror")
if(MOLTEN_METAL_CPP_DIR)
    set(metalcpp_SOURCE_DIR ${MOLTEN_METAL_CPP_DIR})
else()
    FetchContent_Declare(metalcpp GIT_REPOSITORY https://github.com/bkaradzic/metal-cpp.git
                         GIT_TAG 9a8bc57c0318b84863bec2faf6605594877c5f5d SOURCE_SUBDIR do-not-build)
    FetchContent_MakeAvailable(metalcpp)
endif()

# stb_image and stb_image_write: reading PNG and JPEG files, writing PNG files.
FetchContent_Declare(stb GIT_REPOSITORY https://github.com/nothings/stb.git
                     GIT_TAG 9600522de53973336fa033a289858a4354cbb04c SOURCE_SUBDIR do-not-build)
# miniaudio: sound output and audio file decoding.
FetchContent_Declare(miniaudio GIT_REPOSITORY https://github.com/mackron/miniaudio.git GIT_TAG 0.11.21 GIT_SHALLOW TRUE
                     SOURCE_SUBDIR do-not-build)
# ufbx: reading FBX files (and OBJ with its materials) into the engine's models.
FetchContent_Declare(ufbx GIT_REPOSITORY https://github.com/ufbx/ufbx.git GIT_TAG v0.23.1 GIT_SHALLOW TRUE SOURCE_SUBDIR do-not-build)
FetchContent_MakeAvailable(stb miniaudio ufbx)

if(NOT MOLTEN_IOS)
    # GLFW: the window, keyboard, mouse and gamepads, on the Mac.
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW TRUE)
    # Dear ImGui (the branch with docking) and ImGuizmo: the editor's interface and its 3D handles.
    FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.93.0-docking GIT_SHALLOW TRUE
                         SOURCE_SUBDIR do-not-build)
    FetchContent_Declare(imguizmo GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
                         GIT_TAG 105e4fde45e4e74aae8973d98750820ef7aca7e2 SOURCE_SUBDIR do-not-build)
    FetchContent_MakeAvailable(glfw imgui imguizmo)
endif()

# ---------------------------------------------------------------- the engine
file(GLOB_RECURSE MOLTEN_ENGINE_SOURCES CONFIGURE_DEPENDS Engine/*.cpp Engine/*.hpp)
if(MOLTEN_IOS)
    # The window and the start of the program are UIKit's business there.
    list(FILTER MOLTEN_ENGINE_SOURCES EXCLUDE REGEX "Engine/Platform/(Window|Application)\\.cpp$")
    set(MOLTEN_PLATFORM_SOURCES Engine/Platform/iOS/PlatformiOS.mm Engine/Platform/DeviceState.mm)
else()
    set(MOLTEN_PLATFORM_SOURCES Engine/Platform/MetalLayerBridge.mm Engine/Platform/DeviceState.mm)
endif()
add_library(moltenengine STATIC ${MOLTEN_ENGINE_SOURCES} ${MOLTEN_PLATFORM_SOURCES} ${ufbx_SOURCE_DIR}/ufbx.c Shaders/Shared/ShaderTypes.h)
target_include_directories(moltenengine PUBLIC Engine Shaders/Shared)
target_include_directories(moltenengine SYSTEM PUBLIC ${metalcpp_SOURCE_DIR} ${stb_SOURCE_DIR} ${miniaudio_SOURCE_DIR} ${ufbx_SOURCE_DIR})
set_source_files_properties(${ufbx_SOURCE_DIR}/ufbx.c PROPERTIES COMPILE_OPTIONS -w)
# While developing, shaders and assets are read from this source tree (see Core/Paths.hpp).
target_compile_definitions(moltenengine PRIVATE MOLTEN_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_options(moltenengine PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
set_source_files_properties(${MOLTEN_PLATFORM_SOURCES} PROPERTIES COMPILE_OPTIONS -fobjc-arc)
if(MOLTEN_IOS)
    # miniaudio talks to the audio session in Objective-C on iOS.
    set_source_files_properties(Engine/Platform/ThirdParty.cpp PROPERTIES LANGUAGE OBJCXX)
    target_link_libraries(moltenengine PUBLIC
        "-framework Metal" "-framework MetalFX" "-framework Foundation" "-framework QuartzCore" "-framework UIKit" "-framework CoreGraphics"
        "-framework GameController"
        "-framework AVFoundation" "-framework AudioToolbox" "-framework CoreFoundation")
else()
    target_link_libraries(moltenengine PUBLIC glfw
        "-framework Metal" "-framework MetalFX" "-framework Foundation" "-framework QuartzCore" "-framework Cocoa"
        "-framework CoreAudio" "-framework AudioToolbox" "-framework AudioUnit" "-framework CoreFoundation")
endif()

# ================================================================ iPhone and iPad
# One application: the player, with a project's code compiled into it (iOS does not let
# an application load libraries of its own making) and the project's assets beside it.
if(MOLTEN_IOS)
    set(MOLTEN_IOS_PROJECT "" CACHE PATH "The folder of the project to build into the application; empty for the engine's sample scene")
    set(MOLTEN_IOS_BUNDLE_ID "com.example.moltenplayer" CACHE STRING "The application's bundle identifier")
    set(MOLTEN_IOS_TEAM "" CACHE STRING "Your Apple developer team identifier, for signing")
    set(MOLTEN_IOS_NAME "Molten" CACHE STRING "The name under the application's icon")
    set(MOLTEN_IOS_VERSION "${PROJECT_VERSION}" CACHE STRING "The game's version, as players see it: 1.3.0")
    set(MOLTEN_IOS_BUILD_NUMBER "1" CACHE STRING "The build's number: higher for each one sent to the App Store")
    set(MOLTEN_IOS_ICON "${CMAKE_SOURCE_DIR}/Assets/Branding/icon.png" CACHE FILEPATH "A square picture for the application's icon")

    set(MOLTEN_IOS_GAME_SOURCES "")
    set(MOLTEN_IOS_RESOURCES ${CMAKE_SOURCE_DIR}/Shaders ${CMAKE_SOURCE_DIR}/Assets)
    if(MOLTEN_IOS_PROJECT)
        file(GLOB_RECURSE MOLTEN_IOS_GAME_SOURCES CONFIGURE_DEPENDS ${MOLTEN_IOS_PROJECT}/Source/*.cpp ${MOLTEN_IOS_PROJECT}/Assets/Scripts/*.cpp)
        # The project as the player expects to find it: a folder called Project with the
        # .molten file and Assets. Copied when CMake runs: run it again after changing assets.
        set(MOLTEN_IOS_STAGING ${CMAKE_BINARY_DIR}/staging/Project)
        file(REMOVE_RECURSE ${MOLTEN_IOS_STAGING})
        file(GLOB MOLTEN_IOS_PROJECT_FILE ${MOLTEN_IOS_PROJECT}/*.molten)
        file(COPY ${MOLTEN_IOS_PROJECT_FILE} DESTINATION ${MOLTEN_IOS_STAGING})
        # The assets the editor made smaller for the phone (Build/iOS/Assets), or the project's own.
        set(MOLTEN_IOS_ASSETS "" CACHE PATH "A copy of the project's Assets made for the phone; empty: the project's own")
        if(MOLTEN_IOS_ASSETS AND EXISTS "${MOLTEN_IOS_ASSETS}")
            file(COPY ${MOLTEN_IOS_ASSETS}/ DESTINATION ${MOLTEN_IOS_STAGING}/Assets)
        else()
            file(COPY ${MOLTEN_IOS_PROJECT}/Assets DESTINATION ${MOLTEN_IOS_STAGING})
        endif()
        list(APPEND MOLTEN_IOS_RESOURCES ${MOLTEN_IOS_STAGING})
    endif()

    # The icon: an asset catalog with one picture of 1024 points, which Xcode turns into every
    # size the home screen, Settings and Spotlight ask for. (Loose pictures named in Info.plist
    # are not shown by today's iOS.) Made when CMake runs; without alpha, as Apple asks.
    set(MOLTEN_IOS_ICONS "")
    if(EXISTS "${MOLTEN_IOS_ICON}")
        set(MOLTEN_IOS_CATALOG ${CMAKE_BINARY_DIR}/Icon.xcassets)
        set(MOLTEN_IOS_ICON_SET ${MOLTEN_IOS_CATALOG}/AppIcon.appiconset)
        file(REMOVE_RECURSE ${MOLTEN_IOS_CATALOG})
        file(MAKE_DIRECTORY ${MOLTEN_IOS_ICON_SET})
        file(WRITE ${MOLTEN_IOS_CATALOG}/Contents.json "{ \"info\" : { \"author\" : \"xcode\", \"version\" : 1 } }\n")
        file(WRITE ${MOLTEN_IOS_ICON_SET}/Contents.json
             "{ \"images\" : [ { \"filename\" : \"icon-1024.png\", \"idiom\" : \"universal\", \"platform\" : \"ios\", \"size\" : \"1024x1024\" } ],\n  \"info\" : { \"author\" : \"xcode\", \"version\" : 1 } }\n")
        # Through JPEG and back: the simplest way sips has to take the alpha away.
        execute_process(COMMAND sips -s format jpeg -z 1024 1024 "${MOLTEN_IOS_ICON}" --out "${CMAKE_BINARY_DIR}/icon-1024.jpg" OUTPUT_QUIET ERROR_QUIET)
        execute_process(COMMAND sips -s format png "${CMAKE_BINARY_DIR}/icon-1024.jpg" --out "${MOLTEN_IOS_ICON_SET}/icon-1024.png" OUTPUT_QUIET ERROR_QUIET)
        if(EXISTS "${MOLTEN_IOS_ICON_SET}/icon-1024.png")
            set(MOLTEN_IOS_ICONS ${MOLTEN_IOS_CATALOG})
        else()
            message(WARNING "The icon ${MOLTEN_IOS_ICON} could not be read: the application will have none")
        endif()
    else()
        message(WARNING "No icon at ${MOLTEN_IOS_ICON}: the application will have none")
    endif()

    add_executable(MoltenPlayer MACOSX_BUNDLE Player/Main.cpp ${MOLTEN_IOS_GAME_SOURCES} ${MOLTEN_IOS_RESOURCES} ${MOLTEN_IOS_ICONS})
    # The game's own components take the place of the engine's of the same name, as they do in
    # the editor: they are marked as the game's, since here they are declared before the engine's.
    if(MOLTEN_IOS_GAME_SOURCES)
        set_source_files_properties(${MOLTEN_IOS_GAME_SOURCES} PROPERTIES COMPILE_DEFINITIONS MOLTEN_GAME_CODE=1)
    endif()
    # The asset catalog goes to Copy Bundle Resources, where Xcode compiles it (Assets.car): a
    # source with no place in the bundle would only sit in the project.
    if(MOLTEN_IOS_ICONS)
        set_source_files_properties(${MOLTEN_IOS_ICONS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    endif()
    target_link_libraries(MoltenPlayer PRIVATE moltenengine)
    if(MOLTEN_IOS_PROJECT)
        target_include_directories(MoltenPlayer PRIVATE ${MOLTEN_IOS_PROJECT}/Source)
    endif()
    # Folders added as they are: Xcode copies each, whole, into the application.
    set_source_files_properties(${MOLTEN_IOS_RESOURCES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(MoltenPlayer PROPERTIES
        OUTPUT_NAME "${MOLTEN_IOS_NAME}"
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/Engine/Platform/iOS/Info.plist.in
        MACOSX_BUNDLE_GUI_IDENTIFIER "${MOLTEN_IOS_BUNDLE_ID}"
        MACOSX_BUNDLE_BUNDLE_NAME "${MOLTEN_IOS_NAME}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${MOLTEN_IOS_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${MOLTEN_IOS_BUILD_NUMBER}"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${MOLTEN_IOS_BUNDLE_ID}"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${MOLTEN_IOS_TEAM}"
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon")
    return()
endif()

# ================================================================ the Mac

# ---------------------------------------------------------------- the editor
# Dear ImGui with the two backends the editor uses, and ImGuizmo, as one library.
add_library(moltenimgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_demo.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_metal.mm
    ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp)
target_include_directories(moltenimgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imguizmo_SOURCE_DIR}/src
                           ${metalcpp_SOURCE_DIR})
# Lets the Metal backend be called with metal-cpp objects, as the engine has them.
target_compile_definitions(moltenimgui PUBLIC IMGUI_IMPL_METAL_CPP)
# With IMGUI_IMPL_METAL_CPP the Metal backend releases its objects by hand, so this one
# file is compiled without automatic reference counting.
set_source_files_properties(${imgui_SOURCE_DIR}/backends/imgui_impl_metal.mm PROPERTIES COMPILE_OPTIONS -fno-objc-arc)
target_compile_options(moltenimgui PRIVATE -w)
target_link_libraries(moltenimgui PUBLIC glfw "-framework Metal" "-framework MetalKit" "-framework QuartzCore" "-framework Cocoa"
                      "-framework GameController")

# What a game's code and an editor plugin are compiled against. The editor compiles a
# project's Source folder itself, so it is told where these are ('|' between folders).
set(MOLTEN_SDK_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/Engine ${CMAKE_SOURCE_DIR}/Shaders/Shared ${metalcpp_SOURCE_DIR}
    ${stb_SOURCE_DIR} ${miniaudio_SOURCE_DIR})
string(JOIN "|" MOLTEN_SDK_INCLUDE_DIRS_TEXT ${MOLTEN_SDK_INCLUDE_DIRS})

file(GLOB_RECURSE MOLTEN_EDITOR_SOURCES CONFIGURE_DEPENDS Editor/*.cpp Editor/*.hpp)
add_executable(molten ${MOLTEN_EDITOR_SOURCES} Editor/Platform.mm)
set_source_files_properties(Editor/Platform.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
target_include_directories(molten PRIVATE Editor)
target_compile_definitions(molten PRIVATE "MOLTEN_GAME_INCLUDE_DIRS=\"${MOLTEN_SDK_INCLUDE_DIRS_TEXT}\"")
target_compile_options(molten PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
# The whole of both libraries goes into the editor, not only what the editor itself calls:
# a plugin or a game may call any function of the engine or of ImGui, and finds it in the editor.
target_link_libraries(molten PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,moltenengine>" "$<LINK_LIBRARY:WHOLE_ARCHIVE,moltenimgui>"
                      "-framework UniformTypeIdentifiers")
set_target_properties(molten PROPERTIES ENABLE_EXPORTS ON)
# Its name for the Mac when it is started as it is (./build/molten), not from its .app: the menu
# bar's first menu is "Molten Engine v1.3", with the version of this project.
set(MOLTEN_VERSION_SHORT "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
configure_file(${CMAKE_SOURCE_DIR}/Editor/Info.plist ${CMAKE_BINARY_DIR}/EditorInfo.plist @ONLY)
target_link_options(molten PRIVATE "LINKER:-sectcreate,__TEXT,__info_plist,${CMAKE_BINARY_DIR}/EditorInfo.plist")
set_property(TARGET molten APPEND PROPERTY LINK_DEPENDS ${CMAKE_BINARY_DIR}/EditorInfo.plist)

# The player: what an exported game is. It loads a project and the library its code was built into.
add_executable(molten-player Player/Main.cpp)
target_link_libraries(molten-player PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,moltenengine>")
set_target_properties(molten-player PROPERTIES ENABLE_EXPORTS ON)
# The editor exports the player it finds beside itself.
add_dependencies(molten molten-player)
# An application beside the program, "Molten Metal Engine.app": the editor with its icon, to
# double-click or keep in the Dock instead of starting ./build/molten from the Terminal.
add_custom_command(TARGET molten POST_BUILD
    COMMAND sh ${CMAKE_SOURCE_DIR}/Tools/make_editor_app.sh $<TARGET_FILE:molten> ${CMAKE_SOURCE_DIR}/Assets/Branding/icon.png
            ${PROJECT_VERSION} "${CMAKE_BINARY_DIR}/Molten Metal Engine.app"
    VERBATIM)

# The shaders compiled ahead of time (Shaders/Compiled), when the Metal compiler is installed:
# the engine starts without compiling them, and a mistake in them stops here, in the build.
# Without them, or with sources newer than them, the engine compiles the sources as it starts.
add_executable(molten-shader-join Tools/ShaderJoin.cpp Engine/Rendering/ShaderSource.cpp)
target_include_directories(molten-shader-join PRIVATE Engine)
# Part of every build, and done again only when a shader file changes. It never stops the build:
# what the Metal compiler says is kept in build/shader-work (errors-rt.txt, errors-plain.txt,
# result.txt), and the engine compiles the sources itself when it starts if there is no library.
file(GLOB MOLTEN_SHADER_FILES CONFIGURE_DEPENDS Shaders/*.metal Shaders/Shared/*.h)
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/shader-work/compiled.stamp
    COMMAND sh ${CMAKE_SOURCE_DIR}/Tools/compile_shaders.sh $<TARGET_FILE:molten-shader-join> ${CMAKE_SOURCE_DIR}/Shaders ${CMAKE_BINARY_DIR}/shader-work
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_BINARY_DIR}/shader-work/compiled.stamp
    DEPENDS molten-shader-join ${MOLTEN_SHADER_FILES} ${CMAKE_SOURCE_DIR}/Tools/compile_shaders.sh
    COMMENT "Compiling the shaders ahead of time"
    VERBATIM)
add_custom_target(molten-shaders ALL DEPENDS ${CMAKE_BINARY_DIR}/shader-work/compiled.stamp)

# What an editor plugin compiles against: the editor's headers, the engine's, ImGui's.
add_library(molten_editor_api INTERFACE)
target_include_directories(molten_editor_api INTERFACE Editor Engine Shaders/Shared)
target_include_directories(molten_editor_api SYSTEM INTERFACE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends
                           ${imguizmo_SOURCE_DIR}/src ${metalcpp_SOURCE_DIR} ${stb_SOURCE_DIR} ${miniaudio_SOURCE_DIR}
                           ${glfw_SOURCE_DIR}/include)
target_compile_definitions(molten_editor_api INTERFACE IMGUI_IMPL_METAL_CPP)

# molten_add_editor_plugin(Name source.cpp ...) builds Plugins/Name.dylib next to the editor,
# where it is loaded at start. The plugin takes the engine and ImGui from the editor itself.
function(molten_add_editor_plugin name)
    add_library(${name} MODULE ${ARGN})
    target_link_libraries(${name} PRIVATE molten_editor_api molten)
    set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX ".dylib"
                          LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Plugins)
endfunction()

if(MOLTEN_BUILD_EXAMPLE_PLUGIN)
    molten_add_editor_plugin(SceneTools Plugins/SceneTools/SceneTools.cpp)
else()
    # Built before, when it was on by default: not loaded any more.
    file(REMOVE ${CMAKE_BINARY_DIR}/Plugins/SceneTools.dylib)
endif()

# ---------------------------------------------------------------- tests
#   ctest --test-dir build -L unit     the unit tests: no GPU needed
#   ctest --test-dir build -L gpu      whole programs on the GPU, without a window
if(MOLTEN_BUILD_TESTS)
    enable_testing()
    file(GLOB MOLTEN_TEST_SOURCES CONFIGURE_DEPENDS Tests/*.cpp Tests/*.hpp)
    add_executable(moltentests ${MOLTEN_TEST_SOURCES} Editor/PaletteSearch.cpp Editor/ProjectSummary.cpp Editor/Commands.cpp Editor/Shortcuts.cpp Editor/Git.cpp Editor/MultiEdit.cpp Editor/HierarchySearch.cpp Editor/Snapping.cpp Editor/SceneWeight.cpp)     # the palette's search, without the editor
    target_link_libraries(moltentests PRIVATE moltenengine)
    add_test(NAME unit COMMAND moltentests)
    set_tests_properties(unit PROPERTIES LABELS unit)
    add_test(NAME editor-starts COMMAND molten --capture ${CMAKE_BINARY_DIR}/editor.png --frames 5)
    set_tests_properties(editor-starts PROPERTIES LABELS gpu TIMEOUT 600)
    add_test(NAME player-plays-the-sample-scene COMMAND molten-player --capture ${CMAKE_BINARY_DIR}/player.png --frames 30)
    set_tests_properties(player-plays-the-sample-scene PROPERTIES LABELS gpu TIMEOUT 600)
endif()
