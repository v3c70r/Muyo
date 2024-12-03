#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_scalar_block_layout : require
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : require

#include "directLighting.h"   // SpotAngleAttenuation
#include "material.h"

layout (location = 0) out vec4 fOutNormal;
layout (location = 1) out vec4 fOutPosition;
layout (location = 2) out vec4 fOutFlux;

layout(location = 0) in vec2 inTexCoords0;
layout(location = 1) in vec2 inTexCoords1;
layout(location = 2) in vec4 inWorldPos;
layout(location = 3) in vec4 inWorldNormal;
layout(location = 4) flat in uvec2 inObjSubmeshIndex;

// Light info for flux
layout(scalar, set = 0, binding = 0) buffer readonly LightData_ { LightData i[]; }
lightData;

MATERIAL_SSBO(1)

layout(scalar, set = 2, binding = 0) buffer readonly PerObjData_ { PerObjData i[]; }
perObjData;

layout (push_constant) uniform PushConstant {
    uint nLightIndex;
    uint nShadowMapSize;
} pushConstant;

void main()
{

    // Suports up to 2 sets of UVs
    vec2 inTexCoords[2];
    inTexCoords[0] = inTexCoords0;
    inTexCoords[1] = inTexCoords1;

    fOutNormal = inWorldNormal;
    fOutPosition = inWorldPos;

    // Compute flux for the pixel on near plane
    // Flux = Irradiance * Area per pixel
    LightData light = lightData.i[pushConstant.nLightIndex];

    const float fRadius = light.vLightData.x;
    float fAreaPerPixel = fRadius * tan(light.vLightData.z) / float(pushConstant.nShadowMapSize);
    fAreaPerPixel = fAreaPerPixel * fAreaPerPixel;

    const vec3 vPosToLight =  normalize(inWorldPos.xyz - light.vLightData.xyz);
    const vec3 vLightDirection = normalize(light.vDirection);
    const float fAngleAttenuation = SpotAngleAttenuation(vPosToLight, vLightDirection, cos(light.vLightData.y), cos(light.vLightData.z));
    const float fCosFalloff = max(0.0f, dot(vLightDirection, vPosToLight));

    uint objIndex = inObjSubmeshIndex.x;
    uint submeshIndex = inObjSubmeshIndex.y;


    uint materialIndex = perObjData.i[objIndex].vSubmeshDatas[submeshIndex].nMaterialIndex;
    PBRMaterial material = AllMaterials.i[materialIndex];
    
    vec3 vAlbedo = texture(AllTextures[material.textureIds[TEX_ALBEDO]], inTexCoords[material.UVIndices[TEX_ALBEDO]]).xyz * material.vBaseColorFactors.xyz;

    fOutFlux = vec4(light.vColor * light.fIntensity * vAlbedo, 0.0f);
}
