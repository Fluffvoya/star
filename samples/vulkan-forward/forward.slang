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
};

[[vk::push_constant]] ConstantBuffer<DrawConstants> draw;
static const float pi = 3.14159265;

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

[shader("fragment")]
float4 fragmentMain(Varyings input, bool frontFacing : SV_IsFrontFace) : SV_Target
{
    float3 n = normalize(input.worldNormal) * (frontFacing ? 1.0 : -1.0);
    float3 v = normalize(draw.eye.xyz - input.worldPosition);
    float3 l = normalize(float3(-0.45, 0.85, 0.35));
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
    float3 linear = (diffuse + specular) * (3.5 * nDotL) + 0.10 * draw.albedo.rgb;
    // Reinhard tone mapping. The sRGB render target performs the transfer function.
    return float4(linear / (1.0 + linear), 1.0);
}
