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

struct ShadowView
{
    float4 clipX;
    float4 clipY;
    float4 clipZ;
};

struct ShadowFrame
{
    ShadowView views[STAR_SHADOW_CASCADES];
    float4 splitDistances;
    float4 lightDirection;
};

[[vk::binding(3, 0)]] ConstantBuffer<ShadowFrame> shadowFrame;
[[vk::binding(4, 0)]] Texture2DArray<float> shadowDepth;

float shadowVisibility(float3 world, float depth, float nDotL)
{
    float3 worldDx = ddx(world);
    float3 worldDy = ddy(world);
    uint cascade = 0;
    for (; cascade + 1 < STAR_SHADOW_CASCADES && depth > shadowFrame.splitDistances[cascade]; ++cascade)
    {
    }
    ShadowView view = shadowFrame.views[cascade];
    float4 position = float4(world, 1.0);
    float2 uv = float2(dot(view.clipX, position), dot(view.clipY, position)) * 0.5 + 0.5;
    float z = dot(view.clipZ, position);
    if (any(uv < 0.0) || any(uv >= 1.0) || z < 0.0 || z > 1.0)
    {
        return 1.0;
    }
    uint width;
    uint height;
    uint layers;
    shadowDepth.GetDimensions(width, height, layers);
    int2 maximum = int2(width, height) - 1;
    float2 resolution = float2(width, height);
    int2 texel = int2(uv * resolution);
    // Project screen-space surface derivatives to the shadow plane.
    float3 dx = float3(dot(view.clipX.xyz, worldDx) * 0.5, dot(view.clipY.xyz, worldDx) * 0.5,
                       dot(view.clipZ.xyz, worldDx));
    float3 dy = float3(dot(view.clipX.xyz, worldDy) * 0.5, dot(view.clipY.xyz, worldDy) * 0.5,
                       dot(view.clipZ.xyz, worldDy));
    float determinant = dx.x * dy.y - dx.y * dy.x;
    float2 slope = float2(0.0);
    if (abs(determinant) > 1e-10)
    {
        slope = float2(dx.z * dy.y - dy.z * dx.y, dy.z * dx.x - dx.z * dy.x) / determinant;
    }
    float bias = max(0.00010, 0.00030 * (1.0 - saturate(nDotL)));
    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            int2 tap = clamp(texel + int2(x, y), int2(0), maximum);
            float depthAtTap = z + dot(slope, (float2(tap) + 0.5) / resolution - uv);
            visibility += depthAtTap - bias <= shadowDepth.Load(int4(tap, cascade, 0)) ? 1.0 : 0.0;
        }
    }
    return visibility / 9.0;
}

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
    float3 l = shadowFrame.lightDirection.xyz;
    float depth = dot(draw.clipW, float4(input.worldPosition, 1.0));
    float shadow = shadowVisibility(input.worldPosition, depth, dot(n, l));
    float3 linear = evaluateBrdf(n, v, l) * (3.5 * shadow) + 0.10 * draw.albedo.rgb;

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
