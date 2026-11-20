#!/bin/zsh
# Double-click this file in the Finder (or run ./build.command) to build Molten and run
# its tests. Everything that is printed also goes to build.log, next to this file.
cd "$(dirname "$0")" || exit 1
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

{
    echo "== Molten Metal Engine build, $(date)"
    echo "== $(sw_vers -productName) $(sw_vers -productVersion), $(uname -m)"
    if ! command -v cmake >/dev/null; then
        echo "!! cmake was not found. Install it with: brew install cmake"
        exit 1
    fi
    cmake --version | head -1
    xcrun clang++ --version | head -1

    echo "== configure"
    cmake -B build -DCMAKE_BUILD_TYPE=Release || { echo "!! configure failed"; exit 1; }

    # One target at a time, engine first: when something fails, the log says which part.
    for target in moltenengine moltentests moltenimgui molten-player molten SceneTools; do
        echo "== build $target"
        if cmake --build build -j --target $target; then
            echo "== ok $target"
        else
            echo "!! build failed: $target"
        fi
    done

    echo "== unit tests"
    if [ -x build/moltentests ]; then ./build/moltentests | tail -15; else echo "!! moltentests was not built"; fi

    echo "== the editor without a window (5 frames, saved to build/editor.png)"
    if [ -x build/molten ]; then
        ./build/molten --capture build/editor.png --frames 5 --scale 2 && echo "== ok editor"
    else
        echo "!! molten was not built"
    fi
    # The editor with a project open: a copy of a template, so that the template stays as it is.
    if [ -x build/molten ] && [ -d ../MoltenProjects/SpaceShooter ]; then
        rm -rf build/capture-project && cp -R ../MoltenProjects/SpaceShooter build/capture-project 2>/dev/null
        echo "== the editor with a project open (saved to build/editor-project.png)"
        ./build/molten build/capture-project --capture build/editor-project.png --frames 10 --scale 2 && echo "== ok editor with a project"
    fi
    echo "== the player without a window (the engine's sample scene, saved to build/player.png)"
    if [ -x build/molten-player ]; then
        ./build/molten-player --capture build/player.png --frames 30 && echo "== ok player"
    else
        echo "!! molten-player was not built"
    fi
    # The example projects (a separate download, unzipped beside the engine), each played for two seconds: the pictures show what a new
    # project made from one looks like.
    if [ -x build/molten-player ] && [ -d ../MoltenProjects ]; then
        for template in RollABall SpaceShooter BrickBreaker Driving Flying; do
            echo "== template $template (saved to build/template-$template.png)"
            ./build/molten-player "../MoltenProjects/$template" --capture "build/template-$template.png" --frames 120 && echo "== ok template $template"
        done
    fi
    echo "== done. Start the editor with: ./build/molten"
} 2>&1 | tee build.log
