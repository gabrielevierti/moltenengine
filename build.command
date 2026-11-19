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
    for target in moltenengine moltentests moltenimgui molten-player molten SceneTools marble fpv; do
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
        ./build/molten --capture build/editor.png --frames 5 && echo "== ok editor"
    else
        echo "!! molten was not built"
    fi
    echo "== the player without a window (the engine's sample scene, saved to build/player.png)"
    if [ -x build/molten-player ]; then
        ./build/molten-player --capture build/player.png --frames 30 && echo "== ok player"
    else
        echo "!! molten-player was not built"
    fi
    echo "== done. Start the editor with: ./build/molten"
} 2>&1 | tee build.log
