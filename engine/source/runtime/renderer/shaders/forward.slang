#include "cluster_sizes.h"

struct InputVertex
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
};

struct Varyings
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 worldPosition : TEXCOORD0;
    [[vk::location(1)]] float3 worldNormal : TEXCOORD1;
    [[vk::location(2)]] float2 uv : TEXCOORD2;
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

struct EnvironmentFrame
{
    float4 diffuseCoefficients[9];
};

[[vk::binding(5, 0)]] ConstantBuffer<EnvironmentFrame> environmentFrame;
[[vk::binding(6, 0)]] Texture2D<float4> environmentMap;
[[vk::binding(7, 0)]] SamplerState environmentSampler;

[[vk::binding(0, 1)]] Texture2D<float4> baseColorMap;
[[vk::binding(1, 1)]] Texture2D<float4> metallicRoughnessMap;
[[vk::binding(2, 1)]] SamplerState materialSampler;

struct PbrSurface
{
    float3 albedo;
    float roughness;
    float metallic;
};

float3 evaluateEnvironment(float3 n, float3 v, PbrSurface surface)
{
    float basis[9] = {
        0.282095, 0.488603 * n.y, 0.488603 * n.z, 0.488603 * n.x, 1.092548 * n.x * n.y,
        1.092548 * n.y * n.z, 0.315392 * (3.0 * n.z * n.z - 1.0), 1.092548 * n.x * n.z,
        0.546274 * (n.x * n.x - n.y * n.y)
    };
    float3 irradiance = float3(0.0);
    for (uint i = 0; i < 9; ++i)
    {
        irradiance += basis[i] * environmentFrame.diffuseCoefficients[i].rgb;
    }
    float roughness = surface.roughness;
    float metallic = surface.metallic;
    float3 f0 = lerp(float3(0.04), surface.albedo, metallic);
    float3 fresnel = f0 + (max(float3(1.0 - roughness), f0) - f0) * pow(1.0 - saturate(dot(n, v)), 5.0);
    float3 reflected = reflect(-v, n);
    float2 uv = float2(atan2(reflected.z, reflected.x) / (2.0 * pi) + 0.5,
                       acos(clamp(reflected.y, -1.0, 1.0)) / pi);
    uint width;
    uint height;
    uint levels;
    environmentMap.GetDimensions(0, width, height, levels);
    float3 specular = environmentMap.SampleLevel(environmentSampler, uv, roughness * (levels - 1)).rgb;
    return max(0.0, irradiance) * surface.albedo * (1.0 - metallic) * (1.0 - fresnel) + specular * fresnel;
}

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
    output.uv = input.uv;
    return output;
}

float schlickGeometry(float nDotDirection, float k)
{
    return nDotDirection / (nDotDirection * (1.0 - k) + k);
}

float3 evaluateBrdf(float3 n, float3 v, float3 l, PbrSurface surface)
{
    float3 h = normalize(l + v);
    float nDotL = saturate(dot(n, l));
    float nDotV = saturate(dot(n, v));
    float nDotH = saturate(dot(n, h));
    float hDotV = saturate(dot(h, v));
    float roughness = max(surface.roughness, 0.045);
    float metallic = surface.metallic;
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float d = nDotH * nDotH * (alpha2 - 1.0) + 1.0;
    float distribution = alpha2 / (pi * d * d);
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float visibility = schlickGeometry(nDotV, k) * schlickGeometry(nDotL, k);
    float3 f0 = lerp(float3(0.04), surface.albedo, metallic);
    float3 fresnel = f0 + (1.0 - f0) * pow(1.0 - hDotV, 5.0);
    float3 specular = distribution * visibility * fresnel / max(4.0 * nDotL * nDotV, 1e-5);
    float3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * surface.albedo / pi;
    return (diffuse + specular) * nDotL;
}

[shader("fragment")]
float4 fragmentMain(Varyings input, bool frontFacing : SV_IsFrontFace) : SV_Target
{
    float4 channels = metallicRoughnessMap.Sample(materialSampler, input.uv);
    float4 base = baseColorMap.Sample(materialSampler, input.uv);
    float alpha = draw.albedo.a * base.a;
    if (draw.factors.z > 0.0 && alpha < draw.factors.z)
    {
        discard;
    }
    PbrSurface surface;
    surface.albedo = draw.albedo.rgb * base.rgb;
    surface.roughness = saturate(draw.factors.x * channels.g);
    surface.metallic = saturate(draw.factors.y * channels.b);
    float3 n = normalize(input.worldNormal) * (frontFacing ? 1.0 : -1.0);
    float3 v = normalize(draw.eye.xyz - input.worldPosition);
    float3 l = shadowFrame.lightDirection.xyz;
    float depth = dot(draw.clipW, float4(input.worldPosition, 1.0));
    float shadow = shadowVisibility(input.worldPosition, depth, dot(n, l));
    float3 linear = evaluateBrdf(n, v, l, surface) * (shadowFrame.lightDirection.w * shadow) + evaluateEnvironment(n, v, surface);

    float z = saturate(log(depth / draw.clusterParams.x) / log(draw.clusterParams.y / draw.clusterParams.x));
    uint2 tile = min(uint2(input.position.xy / draw.clusterParams.zw * float2(STAR_CLUSTER_COLUMNS, STAR_CLUSTER_ROWS)),
                     uint2(STAR_CLUSTER_COLUMNS - 1, STAR_CLUSTER_ROWS - 1));
    uint offset = clusterOffset(uint3(tile, min(uint(z * STAR_CLUSTER_SLICES), STAR_CLUSTER_SLICES - 1)));
    uint lightCount = clusterLists[offset];
    for (uint index = 0; index < lightCount; ++index)
    {
        PointLight light = pointLights[clusterLists[offset + index + 1]];
        float3 delta = light.positionRadius.xyz - input.worldPosition;
        float radiusSquared = light.positionRadius.w * light.positionRadius.w;
        float distanceSquared = max(dot(delta, delta), radiusSquared * 1e-6);
        float falloff = saturate(1.0 - distanceSquared / radiusSquared);
        float3 radiance = light.colorIntensity.rgb * (light.colorIntensity.w * falloff * falloff / distanceSquared);
        linear += evaluateBrdf(n, v, delta * rsqrt(distanceSquared), surface) * radiance;
    }

    // Reinhard tone mapping. The sRGB render target performs the transfer function.
    return float4(linear / (1.0 + linear), alpha);
}

[shader("fragment")]
float4 unlitMain(Varyings input) : SV_Target
{
    float4 base = baseColorMap.Sample(materialSampler, input.uv);
    float alpha = draw.albedo.a * base.a;
    if (draw.factors.z > 0.0 && alpha < draw.factors.z)
    {
        discard;
    }
    return float4(draw.albedo.rgb * base.rgb, alpha);
}
