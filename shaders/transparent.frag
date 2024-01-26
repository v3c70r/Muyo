#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#include "directLighting.h"
#include "Camera.h"
#include "material.h"

//===============

// GBuffer texture indices
layout(location = 0) in vec2 inTexCoords0;
layout(location = 1) in vec2 inTexCoords1;
layout(location = 2) in vec4 inWorldPos;
layout(location = 3) in vec4 inWorldNormal;

layout(location = 0) out vec4 outColor;

CAMERA_UBO(0)
// IBL parameters
layout(set = 2, binding = 0) uniform samplerCube irradianceMap;
layout(set = 2, binding = 1) uniform samplerCube prefilteredMap;
layout(set = 2, binding = 2) uniform sampler2D specularBrdfLut;

LIGHTS_UBO(3)



void main()
{
    vec2 inTexCoords[2];
    inTexCoords[0] = inTexCoords0;
    inTexCoords[1] = inTexCoords1;

    vec3 vTextureNormal = texture(texPBR[TEX_NORMAL], inTexCoords[factors.UVIndices[TEX_NORMAL]]).xyz;
    vec3 vWorldNormal = normalize(inWorldNormal.xyz + vTextureNormal);

    const vec4 vAlbedo = texture(texPBR[TEX_ALBEDO], inTexCoords[factors.UVIndices[TEX_ALBEDO]]) * factors.vBaseColorFactors;
    const float fMetalness = texture(texPBR[TEX_METALNESS], inTexCoords[factors.UVIndices[TEX_METALNESS]]).r * factors.fMetalness;
    const float fRoughness = texture(texPBR[TEX_ROUGHNESS], inTexCoords[factors.UVIndices[TEX_ROUGHNESS]]).g * factors.fRoughness;

    Material material;
    // populate material struct with material properties
    material.vAlbedo = vAlbedo.xyz;
    material.fAO = 1.0;
    material.fMetalness = fMetalness;
    material.fRoughness = fRoughness;
    material.vEmissive = vec3(0.0);


    vec3 vLo = vec3(0.0);
    const vec3 vViewPos = (uboCamera.view * inWorldPos).xyz;
    const vec3 vNormal = (uboCamera.view * vec4(vWorldNormal, 0.0)).xyz;
    for(int i = 0; i < USED_LIGHT_COUNT; ++i)
    {
        // TODO: Bind light uniforms and finish transparent lighting

    }

    outColor = vec4(vAlbedo);
}

