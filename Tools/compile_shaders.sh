#!/bin/sh
# Compiles the engine's shaders ahead of time into Shaders/Compiled, one library with ray tracing
# and one without, so that the engine starts without compiling them and a mistake in them shows
# here, in the build, instead of as a magenta picture.
# usage: compile_shaders.sh <molten-shader-join> <Shaders folder> <work folder>
# Without the Metal compiler (Xcode > Settings > Components > Metal Toolchain) it says so and
# stops without failing: the engine then compiles the shaders when it starts, as before.
join="$1"; shaders="$2"; work="$3"
out="$shaders/Compiled"
if ! xcrun -sdk macosx -f metal >/dev/null 2>&1; then
    echo "-- shaders: the Metal compiler is not installed; they will be compiled when the engine starts"
    echo "   (to compile them here: Xcode > Settings > Components > Metal Toolchain, or: xcodebuild -downloadComponent MetalToolchain)"
    exit 0
fi
mkdir -p "$out" "$work" || exit 1
"$join" "$shaders" "$work/Molten.metal" || exit 1
failed=0
for variant in rt plain; do
    defines="-DMOLTEN_EXTRAS=1"
    [ "$variant" = rt ] && defines="$defines -DMOLTEN_RAYTRACING=1"
    if xcrun -sdk macosx metal $defines -o "$work/Molten-macos-$variant.metallib" "$work/Molten.metal" 2> "$work/errors-$variant.txt"; then
        mv "$work/Molten-macos-$variant.metallib" "$out/"
        echo "-- shaders: compiled ($variant)"
    else
        echo "!! the shaders do not compile ($variant):"
        grep -E "error" "$work/errors-$variant.txt" | head -20
        rm -f "$out/Molten-macos-$variant.metallib"
        failed=1
    fi
done
exit $failed
