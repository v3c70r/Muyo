// GBUFFER fragment stage on the descriptor-heap path (#20).
//
// The four colour targets are the legacy GBuffer layout, unchanged: position+AO, albedo+transmittance,
// normal+roughness, metalness+translucency. The material and per-object buffers are function-local heap
// handles, and a material's texture is reached through a static heap-index table because
// PBRMaterial::textureIds are TextureResourceManager indices, not heap indices. See the design note on
// #20: the table is the only declared use; the image descriptors behind it are static and read-only.
//
// This is the heap successor of shaders/GBuffer.frag (deleted with RenderPassGBuffer).

#include "shared/RenderGraph/GBuffer.h"

[[vk::push_constant]]
ConstantBuffer<GBufferPush> push;

struct PS_INPUT
{
    [[vk::location(0)]] float2 inTexCoords0 : TEXCOORD0;
    [[vk::location(1)]] float2 inTexCoords1 : TEXCOORD1;
    [[vk::location(2)]] float4 inWorldPos : TEXCOORD2;
    [[vk::location(3)]] float4 inWorldNormal : TEXCOORD3;
    nointerpolation [[vk::location(4)]] uint2 inObjSubmeshIndex : TEXCOORD4;
};

struct PS_OUTPUT
{
    float4 positionAO : SV_Target0;
    float4 albedoTransmittance : SV_Target1;
    float4 normalRoughness : SV_Target2;
    float4 metalnessTranslucency : SV_Target3;
};

[shader("fragment")]
PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output = (PS_OUTPUT)0;

    // Supports up to 2 sets of UVs.
    const float2 texCoords[2] = {input.inTexCoords0, input.inTexCoords1};

    StructuredBuffer<PerObjData> perObjData = StructuredBuffer<PerObjData>.Handle(push.perObjData);
    StructuredBuffer<PBRMaterial> materials = StructuredBuffer<PBRMaterial>.Handle(push.materialData);
    StructuredBuffer<uint> textureTable = StructuredBuffer<uint>.Handle(push.textureTable);

    const uint objectIndex = input.inObjSubmeshIndex.x;
    const uint submeshIndex = input.inObjSubmeshIndex.y;
    const uint materialIndex = perObjData[objectIndex].vSubmeshDatas[submeshIndex].nMaterialIndex;
    const PBRMaterial material = materials[materialIndex];

    // Manager texture index -> this heap's static sampled-image index -> combined with the sampler.
    const float3 textureNormal = Sampler2D.Handle(uint2(textureTable[material.textureIds[TEX_NORMAL]], push.sampler))
                                     .SampleLevel(texCoords[material.uvIndices[TEX_NORMAL]], 0.0)
                                     .xyz;
    const float3 worldNormal = normalize(input.inWorldNormal.xyz + textureNormal);

    output.positionAO = input.inWorldPos / input.inWorldPos.w;
    output.positionAO.w = Sampler2D.Handle(uint2(textureTable[material.textureIds[TEX_AO]], push.sampler))
                              .SampleLevel(texCoords[material.uvIndices[TEX_AO]], 0.0)
                              .r;

    output.albedoTransmittance.xyz =
        Sampler2D.Handle(uint2(textureTable[material.textureIds[TEX_ALBEDO]], push.sampler))
            .SampleLevel(texCoords[material.uvIndices[TEX_ALBEDO]], 0.0)
            .xyz *
        material.vBaseColorFactors.xyz;
    output.albedoTransmittance.w = 1.0;  // Transmittance

    const float roughness = Sampler2D.Handle(uint2(textureTable[material.textureIds[TEX_ROUGHNESS]], push.sampler))
                                .SampleLevel(texCoords[material.uvIndices[TEX_ROUGHNESS]], 0.0)
                                .g *
                            material.fRoughness;
    output.normalRoughness = float4(worldNormal, roughness);

    output.metalnessTranslucency = float4(
        Sampler2D.Handle(uint2(textureTable[material.textureIds[TEX_METALNESS]], push.sampler))
                .SampleLevel(texCoords[material.uvIndices[TEX_METALNESS]], 0.0)
                .r *
            material.fMetalness,
        1.0, 0.0, 0.0);

    return output;
}
