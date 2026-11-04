#pragma once
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

#include "SharedStructures.h"

namespace Muyo
{
/// Host-side view of `GBufferPush` in shaders/shared/RenderGraph/GBuffer.h.
///
/// The shader is the source of truth; this mirrors it and the static_asserts below are the check that
/// the two did not drift. `offsetof` is used rather than trusting the compiler's padding: the shader's
/// `ConstantBuffer` layout is std140-like, where `uint2` is 8-byte aligned, and a future field added
/// without regard to that would otherwise silently shift everything after it.
struct GBufferPush
{
    glm::mat4 mView;
    glm::mat4 mProj;
    glm::uvec2 perObjData;
    glm::uvec2 submeshMaterials;
    glm::uvec2 materials;
    glm::uvec2 textureTable;
    uint32_t sampler;
    // The shader's std140 block rounds the structure up to 16 bytes; the push must cover the whole
    // reflected range, so the tail is explicit rather than left to the C++ struct's own padding.
    uint32_t padding0;
    uint32_t padding1;
    uint32_t padding2;
};

static_assert(offsetof(GBufferPush, mView) == 0, "GBufferPush::mView must match the shader's offset");
static_assert(offsetof(GBufferPush, mProj) == 64, "GBufferPush::mProj must match the shader's offset");
static_assert(offsetof(GBufferPush, perObjData) == 128, "GBufferPush::perObjData must match the shader's offset");
static_assert(offsetof(GBufferPush, submeshMaterials) == 136,
              "GBufferPush::submeshMaterials must match the shader's offset");
static_assert(offsetof(GBufferPush, materials) == 144, "GBufferPush::materials must match the shader's offset");
static_assert(offsetof(GBufferPush, textureTable) == 152, "GBufferPush::textureTable must match the shader's offset");
static_assert(offsetof(GBufferPush, sampler) == 160, "GBufferPush::sampler must match the shader's offset");
static_assert(offsetof(GBufferPush, padding0) == 164, "GBufferPush::padding0 must match the shader's offset");
static_assert(sizeof(GBufferPush) == 176, "GBufferPush must cover the shader's padded size");

/// Host-side view of `GBufferMaterial` in shaders/shared/RenderGraph/GBuffer.h.
///
/// Deliberately has no array members: a heap `StructuredBuffer` read of an array-typed member returns
/// garbage on NVIDIA's descriptor-heap/untyped-pointer lowering (#262), which is why the GBUFFER node
/// does not read `PBRMaterial::textureIds`/`UVIndices` directly.
struct GBufferMaterial
{
    glm::vec4 vBaseColorFactors;
    float fRoughness;
    float fMetalness;
    uint32_t textureAlbedo;
    uint32_t textureNormal;
    uint32_t textureMetalness;
    uint32_t textureRoughness;
    uint32_t textureAO;
    uint32_t textureEmissive;
    uint32_t uvAlbedo;
    uint32_t uvNormal;
    uint32_t uvMetalness;
    uint32_t uvRoughness;
    uint32_t uvAO;
    uint32_t uvEmissive;
    uint32_t padding0;
    uint32_t padding1;
};

static_assert(offsetof(GBufferMaterial, vBaseColorFactors) == 0,
              "GBufferMaterial::vBaseColorFactors must match the shader's offset");
static_assert(offsetof(GBufferMaterial, fRoughness) == 16,
              "GBufferMaterial::fRoughness must match the shader's offset");
static_assert(offsetof(GBufferMaterial, fMetalness) == 20,
              "GBufferMaterial::fMetalness must match the shader's offset");
static_assert(offsetof(GBufferMaterial, textureAlbedo) == 24,
              "GBufferMaterial::textureAlbedo must match the shader's offset");
static_assert(offsetof(GBufferMaterial, textureEmissive) == 44,
              "GBufferMaterial::textureEmissive must match the shader's offset");
static_assert(offsetof(GBufferMaterial, uvAlbedo) == 48, "GBufferMaterial::uvAlbedo must match the shader's offset");
static_assert(offsetof(GBufferMaterial, uvEmissive) == 68,
              "GBufferMaterial::uvEmissive must match the shader's offset");
static_assert(offsetof(GBufferMaterial, padding0) == 72, "GBufferMaterial::padding0 must match the shader's offset");
static_assert(sizeof(GBufferMaterial) == 80, "GBufferMaterial must cover the shader's padded size");

/// Flatten a `PBRMaterial` into the scalar-only form the heap shader reads.
inline GBufferMaterial MakeGBufferMaterial(const PBRMaterial& material)
{
    GBufferMaterial out{};
    out.vBaseColorFactors = material.vBaseColorFactors;
    out.fRoughness = material.fRoughness;
    out.fMetalness = material.fMetalness;
    out.textureAlbedo = material.textureIds[TEX_ALBEDO];
    out.textureNormal = material.textureIds[TEX_NORMAL];
    out.textureMetalness = material.textureIds[TEX_METALNESS];
    out.textureRoughness = material.textureIds[TEX_ROUGHNESS];
    out.textureAO = material.textureIds[TEX_AO];
    out.textureEmissive = material.textureIds[TEX_EMISSIVE];
    out.uvAlbedo = material.UVIndices[TEX_ALBEDO];
    out.uvNormal = material.UVIndices[TEX_NORMAL];
    out.uvMetalness = material.UVIndices[TEX_METALNESS];
    out.uvRoughness = material.UVIndices[TEX_ROUGHNESS];
    out.uvAO = material.UVIndices[TEX_AO];
    out.uvEmissive = material.UVIndices[TEX_EMISSIVE];
    return out;
}

/// Flatten every object's submesh material indices, indexed by `PackSubmeshObjectIndex`.
///
/// 32 slots per object (`MAX_NUM_SUBMESHES`); slots past `nSubmeshCount` stay 0 and are never read.
inline std::vector<uint32_t> MakeSubmeshMaterialIndices(const std::vector<PerObjData>& objects)
{
    std::vector<uint32_t> indices(objects.size() * MAX_NUM_SUBMESHES, 0);
    for (size_t object = 0; object < objects.size(); ++object)
    {
        for (uint32_t submesh = 0; submesh < objects[object].nSubmeshCount; ++submesh)
        {
            indices[object * MAX_NUM_SUBMESHES + submesh] = objects[object].vSubmeshDatas[submesh].nMaterialIndex;
        }
    }
    return indices;
}

/// Host-side view of `LightingPush` in shaders/shared/RenderGraph/Lights.h.
///
/// The four GBuffer image handles and the sampler are two-component heap references; the light count
/// is a uniform buffer and the light data a storage buffer, also passed as heap indices. The view
/// matrix is inline for the same #233 reason as GBufferPush.
struct LightingPush
{
    glm::mat4 mView;
    glm::uvec2 positionAO;
    glm::uvec2 albedoTransmittance;
    glm::uvec2 normalRoughness;
    glm::uvec2 metalnessTranslucency;
    glm::uvec2 lightCount;
    glm::uvec2 lightData;
    uint32_t sampler;
    // See GBufferPush::padding: the shader block is padded to 128 bytes.
    uint32_t padding[3];
};

static_assert(offsetof(LightingPush, mView) == 0, "LightingPush::mView must match the shader's offset");
static_assert(offsetof(LightingPush, positionAO) == 64, "LightingPush::positionAO must match the shader's offset");
static_assert(offsetof(LightingPush, albedoTransmittance) == 72,
              "LightingPush::albedoTransmittance must match the shader's offset");
static_assert(offsetof(LightingPush, normalRoughness) == 80,
              "LightingPush::normalRoughness must match the shader's offset");
static_assert(offsetof(LightingPush, metalnessTranslucency) == 88,
              "LightingPush::metalnessTranslucency must match the shader's offset");
static_assert(offsetof(LightingPush, lightCount) == 96, "LightingPush::lightCount must match the shader's offset");
static_assert(offsetof(LightingPush, lightData) == 104, "LightingPush::lightData must match the shader's offset");
static_assert(offsetof(LightingPush, sampler) == 112, "LightingPush::sampler must match the shader's offset");
static_assert(offsetof(LightingPush, padding) == 116, "LightingPush::padding must match the shader's offset");
static_assert(sizeof(LightingPush) == 128, "LightingPush must cover the shader's padded size");
}  // namespace Muyo
