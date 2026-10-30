// GBUFFER vertex stage on the descriptor-heap path (#20). The per-view matrices arrive inline in the
// node's push data (root data, not a descriptor); the per-object buffer is a function-local heap
// handle. No DescriptorSet/Binding is declared.
//
// This is the heap successor of shaders/GBuffer.vert. That file stays because RenderPassTransparent
// still builds its pipeline from it and TRANSPARENT is out of scope for this port.

#include "shared/RenderGraph/GBuffer.h"

[[vk::push_constant]]
ConstantBuffer<GBufferPush> push;

struct VS_INPUT
{
    [[vk::location(0)]] float3 inPos : POSITION;
    [[vk::location(1)]] float3 inNormal : NORMAL;
    [[vk::location(2)]] float4 inTexCoord : TEXCOORD0;
};

struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
    [[vk::location(0)]] float2 outTexCoords0 : TEXCOORD0;
    [[vk::location(1)]] float2 outTexCoords1 : TEXCOORD1;
    [[vk::location(2)]] float4 outWorldPos : TEXCOORD2;
    [[vk::location(3)]] float4 outWorldNormal : TEXCOORD3;
    // Integer varyings must be flat; the GLSL original declared this `flat in uvec2`.
    nointerpolation [[vk::location(4)]] uint2 outObjSubmeshIndex : TEXCOORD4;
};

[shader("vertex")]
VS_OUTPUT main(VS_INPUT input, uint instanceID : SV_InstanceID, uint startInstance : SV_StartInstanceLocation)
{
    VS_OUTPUT output = (VS_OUTPUT)0;

    // SV_InstanceID is relative to the draw's start instance, so add it back to recover the absolute
    // index. The draw's firstInstance encodes (object, submesh) as PackSubmeshObjectIndex.
    const uint absoluteInstanceID = instanceID + startInstance;

    output.outTexCoords0 = input.inTexCoord.xy;
    output.outTexCoords1 = input.inTexCoord.zw;
    output.outObjSubmeshIndex = uint2(GetObjectIndex(absoluteInstanceID), GetSubmeshIndex(absoluteInstanceID));

    StructuredBuffer<PerObjData> perObjData = StructuredBuffer<PerObjData>.Handle(push.perObjData);
    const matrix<float, 4, 4> worldMatrix = perObjData[output.outObjSubmeshIndex.x].mWorldMatrix;
    output.outWorldPos = mul(worldMatrix, float4(input.inPos, 1.0));
    // Matches the legacy GBuffer.vert: the world matrix transforms the normal (no inverse transpose).
    output.outWorldNormal = normalize(mul(worldMatrix, float4(input.inNormal, 0.0)));

    output.pos = mul(push.mProj, mul(push.mView, output.outWorldPos));

    return output;
}
