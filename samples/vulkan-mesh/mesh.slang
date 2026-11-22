struct Vertex
{
    [[vk::location(0)]] float2 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
};

struct Varyings
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

struct Transform
{
    float cosine;
    float sine;
    float aspectScale;
};

[[vk::push_constant]] ConstantBuffer<Transform> transform;
[[vk::binding(0, 0)]] Texture2D<float4> checkerboard;
[[vk::binding(1, 0)]] SamplerState checkerboardSampler;

[shader("vertex")]
Varyings vertexMain(Vertex vertex)
{
    Varyings result;
    float x = transform.cosine * vertex.position.x - transform.sine * vertex.position.y;
    float y = transform.sine * vertex.position.x + transform.cosine * vertex.position.y;
    result.position = float4(x * transform.aspectScale, y, 0.0, 1.0);
    result.uv = vertex.uv;
    return result;
}

[shader("fragment")]
float4 fragmentMain(Varyings input) : SV_Target
{
    return checkerboard.Sample(checkerboardSampler, input.uv);
}
