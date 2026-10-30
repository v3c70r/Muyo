// OPAQUE_LIGHTING fragment stage on the descriptor-heap path (#20).
//
// Four GBuffer targets, the light count uniform buffer and the light storage buffer arrive as
// function-local heap handles; the view matrix arrives in the node's push data. This replaces
// shaders/lighting.frag, which also carried the IBL and RSM terms.
//
// Direct lighting only: no irradiance/prefiltered/BRDF-LUT ambient and no RSM visibility or
// irradiance. Both are out of scope for this port (#20, #49) and the difference is stated in the PR.

#include "shared/RenderGraph/DirectLighting.h"

[[vk::push_constant]]
ConstantBuffer<LightingPush> push;

[shader("fragment")]
float4 main(float2 uv : TEXCOORD0) : SV_Target
{
    const float4 positionAO = Sampler2D.Handle(uint2(push.positionAO.x, push.sampler)).SampleLevel(uv, 0.0);
    const float4 albedoTransmittance =
        Sampler2D.Handle(uint2(push.albedoTransmittance.x, push.sampler)).SampleLevel(uv, 0.0);
    const float4 normalRoughness =
        Sampler2D.Handle(uint2(push.normalRoughness.x, push.sampler)).SampleLevel(uv, 0.0);
    const float4 metalnessTranslucency =
        Sampler2D.Handle(uint2(push.metalnessTranslucency.x, push.sampler)).SampleLevel(uv, 0.0);

    const float3 worldPos = positionAO.xyz;
    const float3 viewPos = mul(push.mView, float4(worldPos, 1.0)).xyz;
    const float3 worldNormal = normalRoughness.xyz;
    const float3 viewNormal = normalize(mul(push.mView, float4(worldNormal, 0.0)).xyz);

    DirectMaterial material;
    material.albedo = albedoTransmittance.xyz;
    material.ao = positionAO.w;
    material.metalness = metalnessTranslucency.r;
    material.roughness = normalRoughness.w;
    material.emissive = float3(0.0);

    ConstantBuffer<uint> lightCount = ConstantBuffer<uint>.Handle(push.lightCount);
    StructuredBuffer<LightDataRaw> lightBuffer = StructuredBuffer<LightDataRaw>.Handle(push.lightData);

    float3 direct = float3(0.0);
    for (uint i = 0; i < lightCount; ++i)
    {
        Light light = LoadLight(lightBuffer, i);

        // The BRDF is evaluated in view space, matching the legacy shader.
        const float4 lightPosition = mul(push.mView, float4(light.position, 1.0));
        light.position = lightPosition.xyz / lightPosition.w;
        light.direction = mul(push.mView, float4(light.direction, 0.0)).xyz;

        direct += ComputeDirectLighting(light, material, viewPos, viewNormal);
    }

    return float4(direct, 1.0);
}
