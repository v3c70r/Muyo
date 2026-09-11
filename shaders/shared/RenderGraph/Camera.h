#pragma once
#include "../Types.h"

struct PerViewData
{
    GPU_MAT4 mView = GPU_MAT4(1.0);
    GPU_MAT4 mProj = GPU_MAT4(1.0);
    GPU_MAT4 mProjInv = GPU_MAT4(1.0);
    GPU_MAT4 mViewInv = GPU_MAT4(1.0);
    GPU_FLOAT2 vScreenExtent = GPU_FLOAT2(0.0);
    GPU_FLOAT2 vPadding = GPU_FLOAT2(0.0);
    // Left top ray and bottom right ray directions
    GPU_FLOAT3 vLT = GPU_FLOAT3(0.0);
    GPU_FLOAT fNear = 0.0F;
    GPU_FLOAT3 vRB = GPU_FLOAT3(0.0);
    GPU_FLOAT fFar = 1.0F;

    GPU_UINT uFrameId = 0;
    GPU_FLOAT fAperture = 3.0F;
    GPU_FLOAT fFocalDistance = 10.0F;
    GPU_FLOAT fLeftSplitScreenRatio = 0.5F;

    // World-space frustum planes (left, right, bottom, top, near, far), packed as
    // (normal.xyz, distance). Used by the GPU frustum culling pass.
    GPU_FLOAT4 vFrustumPlanes[6];
};

struct PerSubmeshData
{
    GPU_UINT nMaterialIndex;
    // Explicit 32-bit padding instead of float3: a float3 would be 16-byte aligned under
    // std430, making the shader-side stride 32 bytes while the C++ struct is 16 bytes.
    GPU_FLOAT vPadding[3];
};

static const GPU_UINT MAX_NUM_SUBMESHES = 32;

struct PerObjData
{
    GPU_MAT4 mWorldMatrix;
    GPU_UINT nSubmeshCount;
    // Scalar float padding instead of float3: std430 aligns float3 to 16 bytes, which would
    // desynchronise this struct from the C++ definition used to upload it.
    GPU_FLOAT vPadding[3];
    PerSubmeshData vSubmeshDatas[MAX_NUM_SUBMESHES];
    // Object-space (local) axis aligned bounding box, used by GPU frustum culling.
    GPU_FLOAT vAABBMin[3];
    GPU_FLOAT fAABBPadding0;
    GPU_FLOAT vAABBMax[3];
    GPU_FLOAT fAABBPadding1;
};

// Material

static const GPU_UINT TEX_ALBEDO = 0;
static const GPU_UINT TEX_NORMAL = 1;
static const GPU_UINT TEX_METALNESS = 2;
static const GPU_UINT TEX_ROUGHNESS = 3;
static const GPU_UINT TEX_AO = 4;
static const GPU_UINT TEX_EMISSIVE = 5;
static const GPU_UINT TEX_COUNT = 6;


struct PBRMaterial
{
    GPU_FLOAT4 vBaseColorFactors;
    float fRoughness;
    float fMetalness;
    GPU_UINT uvIndices[TEX_COUNT];
    GPU_FLOAT3 vEmissiveFactor;
    GPU_UINT textureIds[TEX_COUNT];
    GPU_FLOAT3 vPadding;
};

// Instance Id: 
//  * low 5 bits: 0-31, submesh idx
//  * high 17 bits: object idx

inline GPU_UINT GetSubmeshIndex(GPU_UINT nInstanceId)
{
    return nInstanceId & 0x1f;
}

inline GPU_UINT GetObjectIndex(GPU_UINT nInstanceId)
{
    return nInstanceId >> 5;
}

inline GPU_UINT PackSubmeshObjectIndex(GPU_UINT nObjectIndex, GPU_UINT nSubmeshIndex)
{
    return (nObjectIndex << 5) | nSubmeshIndex;
}


