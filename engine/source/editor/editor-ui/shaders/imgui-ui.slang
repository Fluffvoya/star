// ImGui draw data shader for the engine RHI backend.
struct UiUniforms {
    float2 scale;
    float2 translate;
};

[[vk::binding(0, 0)]] ConstantBuffer<UiUniforms> uniforms;
[[vk::combinedImageSampler]] [[vk::binding(1, 0)]] Texture2D<float4> atlasTexture;
[[vk::combinedImageSampler]] [[vk::binding(1, 0)]] SamplerState atlasSampler;

struct UiVertexInput {
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct UiVertexOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

[shader("vertex")]
UiVertexOutput vertexMain(UiVertexInput input) {
    UiVertexOutput output;
    output.position = float4(input.position * uniforms.scale + uniforms.translate, 0.0F, 1.0F);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(UiVertexOutput input) : SV_Target {
    return input.color * atlasTexture.Sample(atlasSampler, input.uv);
}
