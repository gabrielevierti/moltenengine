#!/bin/zsh
# Double-click this file in the Finder (or run ./build.command) to build Molten.
# Everything that is printed also goes to build.log, next to this file.
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
    for target in molten-shaders moltenengine moltenimgui molten-player molten SceneTools; do
        echo "== build $target"
        if cmake --build build -j --target $target; then
            echo "== ok $target"
        else
            echo "!! build failed: $target"
        fi
    done

    echo "== done. Start the editor: open \"build/Molten Metal Engine.app\" (or ./build/molten)"
} 2>&1 | tee build.log
