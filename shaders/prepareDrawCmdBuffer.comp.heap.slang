// B1b-5b: GPU draw-command generation on the descriptor-heap path. The parameter block is followed by
// five heap indices; no DescriptorSet/Binding is declared.

#include "shared/RenderGraph/Camera.h"
#include "shared/RenderGraph/DrawCommands.h"

struct Push
{
    PrepareDrawCmdParams params;
    uint2 drawSources;
    uint2 drawCommands;
    uint2 drawCount;
    uint2 perView;
    uint2 perObjData;
};

[[vk::push_constant]]
ConstantBuffer<Push> push;

struct WorldAABB
{
    float3 vMin;
    float3 vMax;
};

WorldAABB TransformAABB(float3 vLocalMin, float3 vLocalMax, float4x4 mWorld)
{
    float3 vCorners[8] = {
        float3(vLocalMin.x, vLocalMin.y, vLocalMin.z), float3(vLocalMax.x, vLocalMin.y, vLocalMin.z),
        float3(vLocalMin.x, vLocalMax.y, vLocalMin.z), float3(vLocalMax.x, vLocalMax.y, vLocalMin.z),
        float3(vLocalMin.x, vLocalMin.y, vLocalMax.z), float3(vLocalMax.x, vLocalMin.y, vLocalMax.z),
        float3(vLocalMin.x, vLocalMax.y, vLocalMax.z), float3(vLocalMax.x, vLocalMax.y, vLocalMax.z),
    };

    WorldAABB result;
    result.vMin = float3(1e30f, 1e30f, 1e30f);
    result.vMax = float3(-1e30f, -1e30f, -1e30f);
    for (uint i = 0; i < 8; ++i)
    {
        float4 vWorldPos = mul(mWorld, float4(vCorners[i], 1.0f));
        result.vMin = min(result.vMin, vWorldPos.xyz);
        result.vMax = max(result.vMax, vWorldPos.xyz);
    }
    return result;
}

bool IsAABBVisible(WorldAABB aabb, float4 vPlanes[6])
{
    for (uint i = 0; i < 6; ++i)
    {
        float4 plane = vPlanes[i];
        float3 vPositive =
            float3(plane.x >= 0.0f ? aabb.vMax.x : aabb.vMin.x, plane.y >= 0.0f ? aabb.vMax.y : aabb.vMin.y,
                   plane.z >= 0.0f ? aabb.vMax.z : aabb.vMin.z);
        if (dot(plane.xyz, vPositive) + plane.w < 0.0f) return false;
    }
    return true;
}

[numthreads(256, 1, 1)][shader("compute")] void main(uint3 dtid
                                                     : SV_DispatchThreadID)
{
    uint i = dtid.x;
    if (i >= push.params.sourceCount) return;

    StructuredBuffer<DrawSource> drawSources = StructuredBuffer<DrawSource>.Handle(push.drawSources);
    RWStructuredBuffer<DrawIndexedCommand> drawCommands =
        RWStructuredBuffer<DrawIndexedCommand>.Handle(push.drawCommands);
    RWStructuredBuffer<uint> drawCount = RWStructuredBuffer<uint>.Handle(push.drawCount);
    // Read the six frustum planes as a float4 slice at PerViewData offset 320 (320 / 16 = element 20).
    // A StructuredBuffer<PerViewData> read of the same bytes culls everything on NVIDIA while passing on
    // RADV; this form avoids the large-stride structured read. The test has one view; a multi-view pass
    // must add the view's plane offset here rather than pinning view 0.
    StructuredBuffer<float4> perViewPlanes = StructuredBuffer<float4>.Handle(push.perView);
    float4 vPlanes[6];
    for (uint plane = 0; plane < 6; ++plane)
    {
        vPlanes[plane] = perViewPlanes[20 + plane];
    }
    StructuredBuffer<PerObjData> perObjData = StructuredBuffer<PerObjData>.Handle(push.perObjData);

    DrawSource src = drawSources[i];
    if (src.indexCount == 0) return;

    PerObjData obj = perObjData[src.perObjId];
    WorldAABB worldAABB = TransformAABB(float3(obj.vAABBMin[0], obj.vAABBMin[1], obj.vAABBMin[2]),
                                        float3(obj.vAABBMax[0], obj.vAABBMax[1], obj.vAABBMax[2]), obj.mWorldMatrix);
    if (!IsAABBVisible(worldAABB, vPlanes)) return;

    uint slot = 0;
    InterlockedAdd(drawCount[0], 1, slot);
    if (slot >= push.params.maxDrawCommands) return;

    DrawIndexedCommand cmd;
    cmd.indexCount = src.indexCount;
    cmd.instanceCount = 1;
    cmd.firstIndex = src.firstIndex;
    cmd.vertexOffset = src.vertexOffset;
    cmd.firstInstance = PackSubmeshObjectIndex(src.perObjId, src.submeshIndex);
    drawCommands[slot] = cmd;
}
