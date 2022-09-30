// This file contains structures that shared between shaders and c++ code
// SHADER_CODE defines the shader code, which is passed in while compiling the shader in CMakeLists.txt
// -------------------------------------------------------------------------------------------------------------
#ifndef SHARED_STRUCTURES_H
#define SHARED_STRUCTURES_H

#ifndef SHADER_CODE
#include <glm/glm.hpp>
using namespace glm;
#endif // SHADER_CODE

// Light data

const uint LIGHT_TYPE_POINT = 0;
const uint LIGHT_TYPE_SPOT = 1;
const uint LIGHT_TYPE_DIRECTIONAL = 2;
const uint LIGHT_TYPE_LINEAR = 3;
const uint LIGHT_TYPE_POLYGON = 4;
const uint LIGHT_TYPE_COUNT = 5;

struct LightData
{
    uint LightType;
    vec3 vPosition;
    vec3 vDirection;
    float fRange;
    vec3 vColor;
    float fIntensity;
    vec4 vLightData;
    mat4 mLightViewProjection;
};

// Material

const uint TEX_ALBEDO = 0;
const uint TEX_NORMAL = 1;
const uint TEX_METALNESS = 2;
const uint TEX_ROUGHNESS = 3;
const uint TEX_AO = 4;
const uint TEX_EMISSIVE = 5;
const uint TEX_COUNT = 6;

struct PBRFactors
{
    vec4 vBaseColorFactors;
    float fRoughness;
    float fMetalness;
    uint UVIndices[TEX_COUNT];
    vec3 vEmissiveFactor;
    float padding0;
};
#endif // SHARED_STRUCTURES_H
