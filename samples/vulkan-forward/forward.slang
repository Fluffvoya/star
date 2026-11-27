#include "cluster_sizes.h"

struct InputVertex
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
};

struct Varyings
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 worldPosition : TEXCOORD0;
    [[vk::location(1)]] float3 worldNormal : TEXCOORD1;
};

struct DrawConstants
{
    float4 clipX;
    float4 clipY;
    float4 clipZ;
    float4 clipW;
    float4 eye;
    float4 albedo;
    float4 factors;
    float4 clusterParams;
};

[[vk::push_constant]] ConstantBuffer<DrawConstants> draw;
static const float pi = 3.14159265;

struct PointLight
{
    float4 positionRadius;
    float4 colorIntensity;
};

[[vk::binding(0, 0)]] StructuredBuffer<PointLight> pointLights;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> clusterOutput;
[[vk::binding(2, 0)]] StructuredBuffer<uint> clusterLists;

uint clusterOffset(uint3 cell)
{
    return ((cell.z * STAR_CLUSTER_ROWS + cell.y) * STAR_CLUSTER_COLUMNS + cell.x) *
           (1 + STAR_CLUSTER_MAX_LIGHTS);
}

float sliceDepth(float index)
{
    return draw.clusterParams.x * pow(draw.clusterParams.y / draw.clusterParams.x, index / STAR_CLUSTER_SLICES);
}

[shader("compute")]
[numthreads(STAR_CLUSTER_WORKGROUP_X, STAR_CLUSTER_WORKGROUP_Y, STAR_CLUSTER_WORKGROUP_Z)]
void clusterMain(uint3 cell : SV_DispatchThreadID)
{
    float2 dimensions = float2(STAR_CLUSTER_COLUMNS, STAR_CLUSTER_ROWS);
    float2 ndcMin = float2(cell.xy) / dimensions * 2.0 - 1.0;
    float2 ndcMax = (float2(cell.xy) + 1.0) / dimensions * 2.0 - 1.0;
    float nearDepth = sliceDepth(float(cell.z));
    float farDepth = sliceDepth(float(cell.z + 1));
    float3 lower = float3(min(ndcMin * nearDepth, ndcMin * farDepth), nearDepth);
    float3 upper = float3(max(ndcMax * nearDepth, ndcMax * farDepth), farDepth);
    float3 scale = float3(length(draw.clipX.xyz), length(draw.clipY.xyz), 1.0);
    uint offset = clusterOffset(cell);
    uint count = 0;
    uint lightCount;
    uint stride;
    pointLights.GetDimensions(lightCount, stride);
    for (uint index = 0; index < lightCount && count < STAR_CLUSTER_MAX_LIGHTS; ++index)
    {
        PointLight light = pointLights[index];
        float4 position = float4(light.positionRadius.xyz, 1.0);
        float3 view = float3(dot(draw.clipX, position), dot(draw.clipY, position), dot(draw.clipW, position));
        float3 extent = scale * light.positionRadius.w;
        if (all(view + extent >= lower) && all(view - extent <= upper))
        {
            clusterOutput[offset + 1 + count++] = index;
        }
    }
    clusterOutput[offset] = count;
}

[shader("vertex")]
Varyings vertexMain(InputVertex input)
{
    Varyings output;
    float4 world = float4(input.position, 1.0);
    output.position = float4(dot(draw.clipX, world), dot(draw.clipY, world),
                             dot(draw.clipZ, world), dot(draw.clipW, world));
    output.worldPosition = input.position;
    output.worldNormal = input.normal;
    return output;
}

float schlickGeometry(float nDotDirection, float k)
{
    return nDotDirection / (nDotDirection * (1.0 - k) + k);
}

float3 evaluateBrdf(float3 n, float3 v, float3 l)
{
    float3 h = normalize(l + v);
    float nDotL = saturate(dot(n, l));
    float nDotV = saturate(dot(n, v));
    float nDotH = saturate(dot(n, h));
    float hDotV = saturate(dot(h, v));
    float roughness = clamp(draw.factors.x, 0.045, 1.0);
    float metallic = saturate(draw.factors.y);
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float d = nDotH * nDotH * (alpha2 - 1.0) + 1.0;
    float distribution = alpha2 / (pi * d * d);
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float visibility = schlickGeometry(nDotV, k) * schlickGeometry(nDotL, k);
    float3 f0 = lerp(float3(0.04), draw.albedo.rgb, metallic);
    float3 fresnel = f0 + (1.0 - f0) * pow(1.0 - hDotV, 5.0);
    float3 specular = distribution * visibility * fresnel / max(4.0 * nDotL * nDotV, 1e-5);
    float3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * draw.albedo.rgb / pi;
    return (diffuse + specular) * nDotL;
}

[shader("fragment")]
float4 fragmentMain(Varyings input, bool frontFacing : SV_IsFrontFace) : SV_Target
{
    float3 n = normalize(input.worldNormal) * (frontFacing ? 1.0 : -1.0);
    float3 v = normalize(draw.eye.xyz - input.worldPosition);
    float3 l = normalize(float3(-0.45, 0.85, 0.35));
    float3 linear = evaluateBrdf(n, v, l) * 3.5 + 0.10 * draw.albedo.rgb;

    float depth = dot(draw.clipW, float4(input.worldPosition, 1.0));
    float z = saturate(log(depth / draw.clusterParams.x) / log(draw.clusterParams.y / draw.clusterParams.x));
    uint2 tile = min(uint2(input.position.xy / draw.clusterParams.zw * float2(STAR_CLUSTER_COLUMNS, STAR_CLUSTER_ROWS)),
                     uint2(STAR_CLUSTER_COLUMNS - 1, STAR_CLUSTER_ROWS - 1));
    uint offset = clusterOffset(uint3(tile, min(uint(z * STAR_CLUSTER_SLICES), STAR_CLUSTER_SLICES - 1)));
    uint lightCount = clusterLists[offset];
    for (uint index = 0; index < lightCount; ++index)
    {
        PointLight light = pointLights[clusterLists[offset + index + 1]];
        float3 delta = light.positionRadius.xyz - input.worldPosition;
        float distanceSquared = max(dot(delta, delta), 1e-5);
        float falloff = saturate(1.0 - distanceSquared / (light.positionRadius.w * light.positionRadius.w));
        float3 radiance = light.colorIntensity.rgb * (light.colorIntensity.w * falloff * falloff / distanceSquared);
        linear += evaluateBrdf(n, v, delta * rsqrt(distanceSquared)) * radiance;
    }

    // Reinhard tone mapping. The sRGB render target performs the transfer function.
    return float4(linear / (1.0 + linear), 1.0);
}
