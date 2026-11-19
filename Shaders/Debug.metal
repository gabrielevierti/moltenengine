// Debug.metal
// Colored lines in world space, for debug drawing.

#include "Common.metal"

struct LineOut {
    float4 position [[position]];
    float4 color;
};

vertex LineOut vertex_line(uint vertexID [[vertex_id]],
                           constant LineVertex *vertices [[buffer(MMBufferVertices)]],
                           constant PassUniforms &pass [[buffer(MMBufferPass)]]) {
    LineVertex v = vertices[vertexID];
    LineOut out;
    out.position = pass.viewProjection * float4(v.position, 1.0);
    out.color = v.color;
    return out;
}

fragment float4 fragment_line(LineOut in [[stage_in]]) {
    return in.color;
}
