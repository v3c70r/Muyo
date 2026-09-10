// GPU-driven draw-command generation pass.
//
// Reads one DrawSource entry per *potential* draw (produced by the scene / culling stage)
// and expands it into a VkDrawIndexedIndirectCommand, publishing the visible command count
// through an atomic counter so the graphics pass can use vkCmdDrawIndexedIndirectCount.
//
// This is the compute half of the GPU-driven pipeline: the CPU only supplies flattened
// scene metadata, while the actual draw commands are built entirely on the GPU (and can
// later be extended with frustum / occlusion culling without touching the CPU path).

#include "shared/RenderGraph/DrawCommands.h"
#include "shared/RenderGraph/Camera.h"

// set 0, binding 0
[[vk::binding(0, 0)]] StructuredBuffer<DrawSource> gDrawSources;
// set 0, binding 1
[[vk::binding(1, 0)]] RWStructuredBuffer<DrawIndexedCommand> gDrawCommands;
// set 0, binding 2: atomic draw counter (single uint at offset 0).
// ByteAddressBuffer exposes InterlockedAdd, which StructuredBuffer does not in Slang.
[[vk::binding(2, 0)]] RWByteAddressBuffer gDrawCount;

// Push constants (Vulkan): number of valid source entries and output capacity.
[[vk::push_constant]] ConstantBuffer<PrepareDrawCmdParams> gParams;

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
