// Vertex Shader Input Structure

#include "shared/RenderGraph/bindings.h"

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
    [[vk::location(2)]] float4 outWorldPos;
    [[vk::location(3)]] float4 outWorldNormal;
    [[vk::location(4)]] uint2 outObjSubmeshIndex;
};

CAMERA()
OBJECT_DATA()

[shader("vertex")]
VS_OUTPUT main(VS_INPUT input, uint instanceID : SV_InstanceID, uint startInstance : SV_StartInstanceLocation)
{
    VS_OUTPUT output = (VS_OUTPUT)0;

    // SV_InstanceID is relative to the draw's start instance, so add it back to recover the absolute
    // instance index. The draw's firstInstance encodes (object, submesh) as PackSubmeshObjectIndex.
    const uint absoluteInstanceID = instanceID + startInstance;

    output.outTexCoords0 = input.inTexCoord.xy;
    output.outTexCoords1 = input.inTexCoord.zw;
    output.outObjSubmeshIndex = uint2(GetObjectIndex(absoluteInstanceID), GetSubmeshIndex(absoluteInstanceID));
    matrix<float, 4, 4>  mWorldMatrix = perObjData[output.outObjSubmeshIndex.x].mWorldMatrix;
    output.outWorldPos = mul(mWorldMatrix, float4(input.inPos, 1.0));
    output.outWorldNormal = mul(mWorldMatrix, float4(input.inNormal, 0.0));
    
    output.pos = mul(perView.mProj, mul(perView.mView, output.outWorldPos));
    
    return output;
}


