#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#include "brdf.h"
#include "material.h"
#include "Camera.h"

// Light informations
const int LIGHT_COUNT = 4;
const int USED_LIGHT_COUNT = 4;
const vec3 lightPositions[LIGHT_COUNT] = vec3[]( vec3(1.0, 1.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0),
    vec3(1.0, 1.0, 0.0)
);
const vec3 lightColors[LIGHT_COUNT] = vec3[](
    vec3(1.0, 1.0, 1.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0),
    vec3(1.0, 1.0, 0.0)
);
//===============

// GBuffer texture indices
layout(location = 0) in vec2 inTexCoords0;
layout(location = 1) in vec2 inTexCoords1;
layout(location = 2) in vec4 inWorldPos;
layout(location = 3) in vec4 inWorldNormal;

layout(location = 0) out vec4 outColor;

CAMERA_UBO(0)

void main()
{
    vec2 inTexCoords[2];
    inTexCoords[0] = inTexCoords0;
    inTexCoords[1] = inTexCoords1;

    uint UVIndices[5];
    UVIndices[0] = factors.UVIndices0;
    UVIndices[1] = factors.UVIndices1;
    UVIndices[2] = factors.UVIndices2;
    UVIndices[3] = factors.UVIndices3;
    UVIndices[4] = factors.UVIndices4;
    vec3 vTextureNormal = texture(texPBR[TEX_NORMAL], inTexCoords[UVIndices[TEX_NORMAL]]).xyz;
    vec3 vWorldNormal = normalize(inWorldNormal.xyz + vTextureNormal);

    const vec4 baseColor = texture(texPBR[TEX_ALBEDO], inTexCoords[UVIndices[TEX_ALBEDO]]) * factors.vBaseColorFactors;
    const float fMetallic = texture(texPBR[TEX_METALNESS], inTexCoords[UVIndices[TEX_METALNESS]]).r * factors.fMetalness;
    const float fRoughness = texture(texPBR[TEX_ROUGHNESS], inTexCoords[UVIndices[TEX_ROUGHNESS]]).g * factors.fRoughness;

    vec3 vLo = vec3(0.0);
    const vec3 vViewPos = (uboCamera.view * inWorldPos).xyz;
    const vec3 vNormal = (uboCamera.view * vec4(vWorldNormal, 0.0)).xyz;
    for(int i = 0; i < USED_LIGHT_COUNT; ++i)
    {
        vLo += ComputeDirectLighting(
                lightPositions[i],
                lightColors[i],
                vViewPos,
                vNormal,
                baseColor.xyz,
                fMetallic,
                fRoughness);
    }

    outColor = vec4(vLo, baseColor.a);
}

