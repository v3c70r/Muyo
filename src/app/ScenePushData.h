#pragma once
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>

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
    glm::uvec2 materialData;
    glm::uvec2 textureTable;
    uint32_t sampler;
    // The shader's std140 block rounds the structure up to 16 bytes; the push must cover the whole
    // reflected range, so the tail is explicit rather than left to the C++ struct's own padding.
    uint32_t padding;
};

static_assert(offsetof(GBufferPush, mView) == 0, "GBufferPush::mView must match the shader's offset");
static_assert(offsetof(GBufferPush, mProj) == 64, "GBufferPush::mProj must match the shader's offset");
static_assert(offsetof(GBufferPush, perObjData) == 128, "GBufferPush::perObjData must match the shader's offset");
static_assert(offsetof(GBufferPush, materialData) == 136, "GBufferPush::materialData must match the shader's offset");
static_assert(offsetof(GBufferPush, textureTable) == 144, "GBufferPush::textureTable must match the shader's offset");
static_assert(offsetof(GBufferPush, sampler) == 152, "GBufferPush::sampler must match the shader's offset");
static_assert(offsetof(GBufferPush, padding) == 156, "GBufferPush::padding must match the shader's offset");
static_assert(sizeof(GBufferPush) == 160, "GBufferPush must cover the shader's padded size");

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
