// Canvas.metal
// 2D drawing: solid shapes, images and text, in points with the origin at the top left.
// Vertices are read straight from a buffer by vertex index, so no vertex descriptor is needed.

#include "Common.metal"

struct CanvasOut {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

vertex CanvasOut vertex_canvas(uint vertexID [[vertex_id]],
                               constant CanvasVertex *vertices [[buffer(MMBufferVertices)]],
                               constant CanvasUniforms &canvas [[buffer(MMBufferPass)]]) {
    CanvasVertex v = vertices[vertexID];
    CanvasOut out;
    out.position = canvas.projection * float4(v.position, 0.0, 1.0);
    out.uv = v.uv;
    // Colors arrive as the sRGB values a designer picks; shading and blending are linear.
    out.color = float4(srgbToLinear(v.color.rgb), v.color.a);
    return out;
}

fragment float4 fragment_canvas(CanvasOut in [[stage_in]],
                                texture2d<float> image [[texture(0)]],
                                sampler imageSampler [[sampler(0)]]) {
    return in.color * image.sample(imageSampler, in.uv);
}

// Text from a signed distance field atlas. The atlas stores 0.5 on a glyph's outline,
// more inside and less outside. Turning the distance into coverage here, per pixel,
// gives sharp edges at any size.
fragment float4 fragment_canvas_text(CanvasOut in [[stage_in]],
                                     constant CanvasUniforms &canvas [[buffer(MMBufferPass)]],
                                     texture2d<float> atlas [[texture(0)]],
                                     sampler atlasSampler [[sampler(0)]]) {
    float distance = atlas.sample(atlasSampler, in.uv).r;

    // How many atlas texels one screen pixel covers, from the screen-space derivatives.
    float2 atlasSize = float2(float(atlas.get_width()), float(atlas.get_height()));
    float2 texelsPerPixel = fwidth(in.uv) * atlasSize;
    float pixelRange = canvas.distanceRange / max(0.5 * (texelsPerPixel.x + texelsPerPixel.y), 0.0001);

    // Signed distance to the outline in screen pixels: half a pixel on each side is the anti-aliasing band.
    float coverage = saturate((distance - 0.5) * pixelRange + 0.5);
    return float4(in.color.rgb, in.color.a * coverage);
}
