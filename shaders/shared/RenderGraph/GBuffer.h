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
    // Flat host-built buffers. Array-typed struct members are not read here: on NVIDIA's
    // descriptor-heap/untyped-pointer lowering they return garbage (#262). `submeshMaterials` is one
    // uint per (object, submesh) slot, indexed by PackSubmeshObjectIndex; `materials` is a flat
    // GBufferMaterial per material with scalar members only.
    uint2 submeshMaterials;
    uint2 materials;
    uint2 textureTable;
    uint sampler;
    uint padding0;
    uint padding1;
    uint padding2;
};

// One material in the form the heap path reads: every field is a scalar or vector, so no member-array
// access is emitted. Mirrors Muyo::GBufferMaterial in src/app/ScenePushData.h; the offsets there are
// static_asserted against this definition.
struct GBufferMaterial
{
    float4 vBaseColorFactors;
    float fRoughness;
    float fMetalness;
    uint textureAlbedo;
    uint textureNormal;
    uint textureMetalness;
    uint textureRoughness;
    uint textureAO;
    uint textureEmissive;
    uint uvAlbedo;
    uint uvNormal;
    uint uvMetalness;
    uint uvRoughness;
    uint uvAO;
    uint uvEmissive;
    uint padding0;
    uint padding1;
};
