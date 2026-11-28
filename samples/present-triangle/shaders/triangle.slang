// Minimal RHI presentation shader: draws one triangle without any vertex buffer.
struct TriangleVaryings {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

[shader("vertex")]
TriangleVaryings vertexMain(uint vertex_id : SV_VertexID) {
    const float2 positions[3] = {float2(0.0F, -0.6F), float2(0.6F, 0.6F), float2(-0.6F, 0.6F)};
    const float3 colors[3] = {float3(0.9F, 0.2F, 0.2F), float3(0.2F, 0.8F, 0.3F), float3(0.2F, 0.4F, 0.9F)};
    TriangleVaryings output;
    output.position = float4(positions[vertex_id], 0.0F, 1.0F);
    output.color = colors[vertex_id];
    return output;
}

[shader("fragment")]
float4 fragmentMain(TriangleVaryings input) : SV_Target {
    return float4(input.color, 1.0F);
}
