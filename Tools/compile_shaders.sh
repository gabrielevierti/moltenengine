#!/bin/sh
# Compiles the engine's shaders ahead of time into Shaders/Compiled, one library with ray tracing
# and one without, so that the engine starts without compiling them and a mistake in them shows
# here, in the build, instead of as a picture with things missing.
# usage: compile_shaders.sh <molten-shader-join> <Shaders folder> <work folder>
#
# It never fails the build. What the Metal compiler said is kept in the work folder:
#   result.txt          one line for each library: "rt ok", "plain failed", or "no compiler"
#   errors-rt.txt       the compiler's words for the library with ray tracing
#   errors-plain.txt    and for the one without
#   Molten.metal        the shaders joined into the one text that was compiled
# A library that did not compile is removed, and the engine then compiles the sources when it
# starts, with as much of them as does compile.
# Without the Metal compiler (Xcode > Settings > Components > Metal Toolchain) it says so:
# the engine compiles the shaders when it starts, as before.
join="$1"; shaders="$2"; work="$3"
out="$shaders/Compiled"
mkdir -p "$work"
: > "$work/result.txt"
if ! xcrun -sdk macosx -f metal >/dev/null 2>&1; then
    echo "-- shaders: the Metal compiler is not installed; they will be compiled when the engine starts"
    echo "   (to compile them here: Xcode > Settings > Components > Metal Toolchain, or: xcodebuild -downloadComponent MetalToolchain)"
    echo "no compiler" > "$work/result.txt"
    exit 0
fi
mkdir -p "$out"
if ! "$join" "$shaders" "$work/Molten.metal"; then
    echo "!! the shader files could not be joined"
    echo "join failed" > "$work/result.txt"
    exit 0
fi
for variant in rt plain; do
    defines="-DMOLTEN_EXTRAS=1"
    [ "$variant" = rt ] && defines="$defines -DMOLTEN_RAYTRACING=1"
    if xcrun -sdk macosx metal $defines -o "$work/Molten-macos-$variant.metallib" "$work/Molten.metal" 2> "$work/errors-$variant.txt"; then
        mv "$work/Molten-macos-$variant.metallib" "$out/"
        echo "-- shaders: compiled ($variant)"
        echo "$variant ok" >> "$work/result.txt"
    else
        echo "!! the shaders do not compile ($variant): the engine will draw with less. The first errors:"
        grep -E "error" "$work/errors-$variant.txt" | head -20
        rm -f "$out/Molten-macos-$variant.metallib"
        echo "$variant failed" >> "$work/result.txt"
    fi
done
exit 0
