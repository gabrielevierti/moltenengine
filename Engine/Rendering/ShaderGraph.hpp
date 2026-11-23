// ShaderGraph.hpp
// Shaders drawn as boxes and wires instead of written. A `.shader` in Assets is either code (a
// surface shader, SurfaceShader.hpp) or a graph (the visual scripts' own format, Script/Graph.hpp)
// made of the nodes below, which the engine turns into that code when it reads it. + New > Shader
// makes a graph, opened in the Shader Graph window; a material chooses either kind the same way,
// and sets its properties the same way. (Before 1.3 a graph was a `.shadergraph`: still read.)
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

/// What a new shader starts as: the Surface node, a Color Property into its color and a Number
/// into its roughness, ready to be changed.
std::string newShaderGraphText();

/// Whether a shader's text is a graph (it starts "molten-graph") rather than code.
bool isShaderGraphText(const std::string& text);
/// Whether the shader file at this path is a graph: a `.shader` whose text is one, or a
/// `.shadergraph` (as they were named before 1.3). Reads only its first line.
bool isShaderGraphFile(const std::string& absolutePath);

}  // namespace molten
