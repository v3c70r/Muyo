#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_scalar_block_layout : require
#extension GL_GOOGLE_include_directive : enable

#include "directLighting.h"   // SpotAngleAttenuation
#include "material.h"

layout (location = 0) out vec4 fOutNormal;
layout (location = 1) out vec4 fOutPosition;
layout (location = 2) out vec4 fOutFlux;

layout(location = 0) in vec4 inWorldPos;
layout(location = 1) in vec4 inWorldNormal;

// Light info for flux
layout(scalar, set = 0, binding = 0) buffer LightData_ { LightData i[]; }
lightData;

MATERIAL_UBO(2)

layout (push_constant) uniform PushConstant {
    uint nLightIndex;
    uint nShadowMapSize;
} pushConstant;




void main()
{
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

    
    fOutFlux = vec4(light.vColor * light.fIntensity * fAngleAttenuation * fCosFalloff * fAreaPerPixel, 0.0f);
}
