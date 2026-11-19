// GPUTypes.hpp
// Brings the structs of Shaders/Shared/ShaderTypes.h into C++ as molten::gpu::PassUniforms,
// molten::gpu::InstanceData and so on. The shaders include the very same file, so both
// sides always agree on every size and offset.
#pragma once

#include <simd/simd.h>

#include <cstdint>

namespace molten::gpu {
#include "ShaderTypes.h"
}
