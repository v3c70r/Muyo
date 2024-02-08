#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : require
#include "directLighting.h"
#include "Camera.h"
#include "material.h"

//===============

// GBuffer texture indices
layout(location = 0) in vec2 inTexCoords0;
layout(location = 1) in vec2 inTexCoords1;
layout(location = 2) in vec4 inWorldPos;
layout(location = 3) in vec4 inWorldNormal;
layout(location = 4) flat in uvec2 inObjSubmeshIndex;

layout(location = 0) out vec4 outColor;

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

    vec4 vAlbedo = vec4(texture(AllTextures[material.textureIds[TEX_ALBEDO]], inTexCoords[material.UVIndices[TEX_ALBEDO]]).xyz, 1.0) * material.vBaseColorFactors;

    outColor = vec4(vAlbedo);
}

