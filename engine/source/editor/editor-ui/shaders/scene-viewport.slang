struct GridVertex {
    float4 position : POSITION;
    float3 color : COLOR0;
};

struct GridFragment {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

[shader("vertex")]
GridFragment vertexMain(GridVertex vertex) {
    return {vertex.position, vertex.color};
}

[shader("fragment")]
float4 fragmentMain(GridFragment fragment) : SV_Target {
    return float4(fragment.color, 1.0F);
}
