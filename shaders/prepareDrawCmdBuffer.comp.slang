// GPU-driven draw-command generation + frustum culling pass.
//
// For every potential draw (one DrawSource entry) this pass:
//   1. fetches the owning object's local-space AABB from the PerObjData buffer,
//   2. transforms it into world space with the object's world matrix,
//   3. tests the resulting AABB against the world-space camera frustum planes,
//   4. appends a VkDrawIndexedIndirectCommand for surviving objects and bumps an atomic
//      counter so the graphics pass can use vkCmdDrawIndexedIndirectCount.
//
// The CPU only uploads flattened scene metadata; visibility is decided entirely on the GPU.

#include "shared/RenderGraph/DrawCommands.h"
#include "shared/RenderGraph/Camera.h"

// set 0, binding 0
[[vk::binding(0, 0)]] StructuredBuffer<DrawSource> gDrawSources;
// set 0, binding 1
[[vk::binding(1, 0)]] RWStructuredBuffer<DrawIndexedCommand> gDrawCommands;
// set 0, binding 2: atomic draw counter (single uint at offset 0).
// ByteAddressBuffer exposes InterlockedAdd, which StructuredBuffer does not in Slang.
[[vk::binding(2, 0)]] RWByteAddressBuffer gDrawCount;
// set 0, binding 3: camera / per-view data, used to build the frustum.
[[vk::binding(3, 0)]] ConstantBuffer<PerViewData> gPerView;
// set 0, binding 4: per-object data (world matrix + local AABB).
[[vk::binding(4, 0)]] StructuredBuffer<PerObjData> gPerObjData;

// Push constants (Vulkan): number of valid source entries and output capacity.
[[vk::push_constant]] ConstantBuffer<PrepareDrawCmdParams> gParams;

// Axis-aligned world-space bounds of an object.
struct WorldAABB
{
    float3 vMin;
    float3 vMax;
};

// Transform a local-space AABB into world space by transforming all 8 corners and
// taking their component-wise min/max. Cheap and handles rotation correctly.
WorldAABB TransformAABB(float3 vLocalMin, float3 vLocalMax, float4x4 mWorld)
{
    float3 vCorners[8] = {
        float3(vLocalMin.x, vLocalMin.y, vLocalMin.z),
        float3(vLocalMax.x, vLocalMin.y, vLocalMin.z),
        float3(vLocalMin.x, vLocalMax.y, vLocalMin.z),
        float3(vLocalMax.x, vLocalMax.y, vLocalMin.z),
        float3(vLocalMin.x, vLocalMin.y, vLocalMax.z),
        float3(vLocalMax.x, vLocalMin.y, vLocalMax.z),
        float3(vLocalMin.x, vLocalMax.y, vLocalMax.z),
        float3(vLocalMax.x, vLocalMax.y, vLocalMax.z),
    };

    WorldAABB result;
    result.vMin = float3( 1e30f,  1e30f,  1e30f);
    result.vMax = float3(-1e30f, -1e30f, -1e30f);
    for (uint i = 0; i < 8; ++i)
    {
        float4 vWorldPos = mul(mWorld, float4(vCorners[i], 1.0f));
        result.vMin = min(result.vMin, vWorldPos.xyz);
        result.vMax = max(result.vMax, vWorldPos.xyz);
    }
    return result;
}

// Conservative AABB vs frustum test: an object is visible unless it is fully outside
// any single plane. Uses the positive vertex (p-vertex) test against the world-space
// planes computed on the CPU and delivered through PerViewData.
bool IsAABBVisible(WorldAABB aabb, float4 vPlanes[6])
{
    for (uint i = 0; i < 6; ++i)
    {
        float4 plane = vPlanes[i];
        // Pick the AABB corner that is furthest along the plane normal.
        float3 vPositive = float3(
            plane.x >= 0.0f ? aabb.vMax.x : aabb.vMin.x,
            plane.y >= 0.0f ? aabb.vMax.y : aabb.vMin.y,
            plane.z >= 0.0f ? aabb.vMax.z : aabb.vMin.z);

        if (dot(plane.xyz, vPositive) + plane.w < 0.0f)
        {
            return false;  // fully outside this plane
        }
    }
    return true;
}

[numthreads(256, 1, 1)]
[shader("compute")]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint i = dtid.x;
    if (i >= gParams.sourceCount)
    {
        return;
    }

    DrawSource src = gDrawSources[i];

    // Skip empty draws so the compacted command list stays tight.
    if (src.indexCount == 0)
    {
        return;
    }

    // Frustum cull: transform the object's local AABB to world space and reject it
    // if it lies fully outside the camera frustum.
    PerObjData obj = gPerObjData[src.perObjId];
    WorldAABB worldAABB = TransformAABB(float3(obj.vAABBMin[0], obj.vAABBMin[1], obj.vAABBMin[2]),
                                        float3(obj.vAABBMax[0], obj.vAABBMax[1], obj.vAABBMax[2]),
                                        obj.mWorldMatrix);
    if (!IsAABBVisible(worldAABB, gPerView.vFrustumPlanes))
    {
        return;
    }

    // Atomically reserve a slot in the compacted output list.
    uint slot = 0;
    gDrawCount.InterlockedAdd(0, 1, slot);
    if (slot >= gParams.maxDrawCommands)
    {
        return;
    }

    DrawIndexedCommand cmd;
    cmd.indexCount = src.indexCount;
    cmd.instanceCount = 1;
    cmd.firstIndex = src.firstIndex;
    cmd.vertexOffset = src.vertexOffset;
    // The instance id encodes (object, submesh) so the vertex shader can fetch PerObjData.
    cmd.firstInstance = PackSubmeshObjectIndex(src.perObjId, src.submeshIndex);
    gDrawCommands[slot] = cmd;
}
