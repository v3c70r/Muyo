#pragma once
// GPU-driven draw-command structures shared between C++ and Slang shaders.
// Uses the GPU_* type aliases from Types.h so the same code compiles on both sides.

#include "../Types.h"

// Matches VkDrawIndexedIndirectCommand.
struct DrawIndexedCommand
{
    GPU_UINT indexCount;
    GPU_UINT instanceCount;
    GPU_UINT firstIndex;
    GPU_INT  vertexOffset;
    GPU_UINT firstInstance;
};

// Per-draw source metadata consumed by the GPU-driven draw-command generation pass.
// One entry per submesh that *may* be drawn; the compute shader compacts these into
// actual draw commands and culls the empty ones.
struct DrawSource
{
    GPU_UINT indexCount;   // mesh index count for this submesh
    GPU_UINT firstIndex;   // offset into the shared index buffer
    GPU_INT  vertexOffset; // base vertex (0 for a single shared vertex buffer)
    GPU_UINT perObjId;     // index into the per-object storage buffer (PerObjData)
    GPU_UINT submeshIndex; // submesh slot inside PerObjData
    GPU_UINT _padding0;
    GPU_UINT _padding1;
    GPU_UINT _padding2;
};

// Push-constant parameters for the draw-command generation pass.
struct PrepareDrawCmdParams
{
    GPU_UINT sourceCount;     // number of valid DrawSource entries
    GPU_UINT maxDrawCommands; // capacity of the output command buffer
    GPU_UINT _padding0;
    GPU_UINT _padding1;
};
