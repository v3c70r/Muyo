#pragma once

// Direct-lighting view of the scene's LightData and the OPAQUE_LIGHTING push block.
//
// The C++ LightData in shaders/shared/SharedStructures.h is uploaded as raw scalar-layout bytes (the
// legacy GLSL buffers declare `layout(scalar)`): LightType at 0, vPosition at 4, vDirection at 16,
// fRange at 28, vColor at 32, fIntensity at 44, vLightData at 48 and mLightViewProjection at 64.
//
// Slang's StructuredBuffer layout aligns a float3 that follows the leading uint to 16, which would
// put vPosition at 16 and make the whole struct 144 bytes - the wrong offsets *and* the wrong stride.
// So the fields are spelled as scalars, and the matrix tail is four float4s this pass never reads but
// the stride needs. The buffer's C++ definition stays the source of truth.
struct LightDataRaw
{
    uint LightType;
    float positionX;
    float positionY;
    float positionZ;
    float directionX;
    float directionY;
    float directionZ;
    float fRange;
    float colorR;
    float colorG;
    float colorB;
    float fIntensity;
    float4 vLightData;
    float4 lightViewProjection[4];
};

struct Light
{
    uint type;
    float3 position;
    float3 direction;
    float range;
    float3 color;
    float intensity;
    float4 data;
};

static const uint LIGHT_TYPE_POINT = 0;
static const uint LIGHT_TYPE_SPOT = 1;
static const uint LIGHT_TYPE_DIRECTIONAL = 2;

Light LoadLight(StructuredBuffer<LightDataRaw> lightBuffer, uint index)
{
    const LightDataRaw raw = lightBuffer[index];
    Light light;
    light.type = raw.LightType;
    light.position = float3(raw.positionX, raw.positionY, raw.positionZ);
    light.direction = float3(raw.directionX, raw.directionY, raw.directionZ);
    light.range = raw.fRange;
    light.color = float3(raw.colorR, raw.colorG, raw.colorB);
    light.intensity = raw.fIntensity;
    light.data = raw.vLightData;
    return light;
}

// Mirrors Muyo::LightingPush in src/app/ScenePushData.h, which static_asserts the offsets.
struct LightingPush
{
    float4x4 mView;
    uint2 positionAO;
    uint2 albedoTransmittance;
    uint2 normalRoughness;
    uint2 metalnessTranslucency;
    uint2 lightCount;
    uint2 lightData;
    uint sampler;
};
