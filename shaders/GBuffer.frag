#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require
#include "material.h"
#include "Camera.h"
#include "material.h"

layout(location = 0) in vec2 inTexCoords0;
layout(location = 1) in vec2 inTexCoords1;
layout(location = 2) in vec4 inWorldPos;
layout(location = 3) in vec4 inWorldNormal;
layout(location = 4) flat in uvec2 inObjSubmeshIndex;

layout(location = 0) out vec4 outPositionAO;
layout(location = 1) out vec4 outAlbedoTransmittance;
layout(location = 2) out vec4 outNormalRoughness;
layout(location = 3) out vec4 outMatelnessTranslucency;

CAMERA_UBO(0)
layout(scalar, set = 1, binding = 0) buffer PerObjData_ { PerObjData i[]; }
perObjData;
MATERIAL_SSBO(2)

void main()
{
    // Suports up to 2 sets of UVs
    vec2 inTexCoords[2];
    inTexCoords[0] = inTexCoords0;
    inTexCoords[1] = inTexCoords1;

    uint objIndex = inObjSubmeshIndex.x;
    uint submeshIndex = inObjSubmeshIndex.y;
    uint materialIndex = perObjData.i[objIndex].vSubmeshDatas[submeshIndex].nMaterialIndex;

    PBRMaterial material = AllMaterials.i[materialIndex];

    const vec3 vTextureNormal = texture(AllTextures[material.textureIds[TEX_NORMAL]], inTexCoords[material.UVIndices[TEX_NORMAL]]).xyz;

    const vec3 vWorldNormal = normalize(inWorldNormal.xyz + vTextureNormal);

    // Populate GBuffer
    outPositionAO = inWorldPos / inWorldPos.w;
    outPositionAO.w = texture(AllTextures[material.textureIds[TEX_AO]], inTexCoords[material.UVIndices[TEX_AO]]).r;
    outAlbedoTransmittance.xyz = texture(AllTextures[material.textureIds[TEX_ALBEDO]], inTexCoords[material.UVIndices[TEX_ALBEDO]]).xyz * material.vBaseColorFactors.xyz;
    outAlbedoTransmittance.w = 1.0;  // Transmittance

    const float fRoughness = texture(AllTextures[material.textureIds[TEX_ROUGHNESS]], inTexCoords[material.UVIndices[TEX_ROUGHNESS]]).g * material.fRoughness;

    outNormalRoughness = vec4(vWorldNormal, fRoughness);

    // Probably need a specular map (Reflection map);
    outMatelnessTranslucency = vec4(
        texture(AllTextures[material.textureIds[TEX_METALNESS]], inTexCoords[material.UVIndices[TEX_METALNESS]]).r * material.fMetalness,
        1.0, 0.0, 0.0);
}

