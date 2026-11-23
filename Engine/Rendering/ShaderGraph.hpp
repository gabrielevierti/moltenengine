// ShaderGraph.hpp
// Shaders drawn as boxes and wires instead of written: a `.shadergraph` in Assets is a graph
// (the visual scripts' own format, Script/Graph.hpp) made of the nodes below, and the engine
// turns it into the code of a surface shader (SurfaceShader.hpp) when it reads it. A material
// chooses one exactly as it chooses a `.shader`, and sets its properties the same way.
//
//     [UV] ──▶ [Scroll UV] ──▶ [Noise] ──▶ [Multiply] ──▶ Emissive [Surface]
//                                  [Color Property "hot"] ──▶
//
// Every value is a float3 or a float: a float goes where a float3 is wanted as three of
// itself, a float3 where a float is wanted as its first number. The Surface node's inputs
// replace what the material made of the surface; one with nothing wired and nothing typed in
// leaves it as it was.
#pragma once

#include "Script/Graph.hpp"

#include <string>

namespace molten {

/// The kinds of node a shader graph is made of (not those of visual scripts).
const script::NodeLibrary& shaderNodes();

/// The `.shader` text of a shader graph: its properties block and its surface function.
/// Empty, with `error` said, if it cannot be made (no Surface node, a wire in a loop).
std::string shaderTextFromGraph(const script::Graph& graph, const std::string& name, std::string* error = nullptr);

/// What a new `.shadergraph` starts as: the Surface node, a Color Property into its color and
/// a Number into its roughness, ready to be changed.
std::string newShaderGraphText();

}  // namespace molten
