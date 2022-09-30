#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

#include "Camera.h"
#include "material.h"

layout(location = 0) in vec2 inTexCoords0;
layout(location = 1) in vec2 inTexCoords1;
layout(location = 2) in vec4 inWorldPos;
layout(location = 3) in vec4 inWorldNormal;

layout(location = 0) out vec4 outPositionAO;
layout(location = 1) out vec4 outAlbedoTransmittance;
layout(location = 2) out vec4 outNormalRoughness;
layout(location = 3) out vec4 outMatelnessTranslucency;

CAMERA_UBO(0)
MATERIAL_UBO(1)

void main() {

    // Suports up to 2 sets of UVs
    vec2 inTexCoords[2];
    inTexCoords[0] = inTexCoords0;
    inTexCoords[1] = inTexCoords1;

    vec3 vTextureNormal = texture(texPBR[TEX_NORMAL], inTexCoords[factors.UVIndices[TEX_NORMAL]]).xyz;
    vec3 vWorldNormal = normalize(inWorldNormal.xyz + vTextureNormal);

    // Populate GBuffer
    outPositionAO = inWorldPos / inWorldPos.w;
    outPositionAO.w = texture(texPBR[TEX_AO], inTexCoords[factors.UVIndices[TEX_AO]]).r;
    outAlbedoTransmittance.xyz = texture(texPBR[TEX_ALBEDO], inTexCoords[factors.UVIndices[TEX_ALBEDO]]).xyz * factors.vBaseColorFactors.xyz;
    outAlbedoTransmittance.w = 1.0; // Transmittance

    const float fRoughness = texture(texPBR[TEX_ROUGHNESS], inTexCoords[factors.UVIndices[TEX_ROUGHNESS]]).g * factors.fRoughness;
    outNormalRoughness = vec4(vWorldNormal, fRoughness);

    // Probably need a specular map (Reflection map);
    outMatelnessTranslucency = vec4(texture(texPBR[TEX_METALNESS], inTexCoords[factors.UVIndices[TEX_METALNESS]]).r * factors.fMetalness, 1.0, 0.0, 0.0);
}
