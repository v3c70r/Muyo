#pragma once
#include "Camera.h"

// Push block shared by GBuffer.vert.heap.slang and GBuffer.frag.heap.slang.
//
// The camera arrives inline rather than through a PerViewData heap read: a heap struct-member read
// with a non-zero offset silently reads the buffer start on NVIDIA (#233), and mProj lives at byte
// 64. The layout is mirrored by Muyo::GBufferPush in src/app/ScenePushData.h, which static_asserts
// every offset; a change here has to make that assert fail rather than silently shift a field.
struct GBufferPush
{
    float4x4 mView;
    float4x4 mProj;
    uint2 perObjData;
    uint2 materialData;
    uint2 textureTable;
    uint sampler;
};
