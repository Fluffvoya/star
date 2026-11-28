struct PreviewConstants {
    float4 positionX;
    float4 positionY;
    float4 positionZ;
    float4 positionW;
    float4 baseColor;
};

[[vk::push_constant]] ConstantBuffer<PreviewConstants> instance;

struct ModelVertex {
    float3 position : POSITION;
    float3 normal : NORMAL;
};

struct ModelFragment {
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float3 color : COLOR0;
};

[shader("vertex")]
ModelFragment vertexMain(ModelVertex vertex) {
    ModelFragment output;
    output.position = vertex.position.x * instance.positionX +
                      vertex.position.y * instance.positionY +
                      vertex.position.z * instance.positionZ + instance.positionW;
    output.normal = vertex.normal;
    output.color = instance.baseColor.rgb;
    return output;
}

[shader("fragment")]
float4 fragmentMain(ModelFragment fragment) : SV_Target {
    float3 normal = normalize(fragment.normal + float3(0.00001, 0.0, 0.0));
    float light = 0.44 + 0.56 * abs(dot(normal, normalize(float3(0.4, 0.8, 0.45))));
    return float4(fragment.color * light, 1.0);
}
