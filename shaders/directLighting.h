#ifndef DIRECT_LIGHTING_H
#define DIRECT_LIGHTING_H
#include "brdf.h"

// https://google.github.io/filament/Filament.md.html#lighting/directlighting
// Direct lighting related functions

float SquareFalloffAttenuation(float fDistanceToLight, float fLightAffectedRadius)
{
    const float fLightInvRadius = 1.0 / fLightAffectedRadius;
    const float fDistanceSquare = fDistanceToLight * fDistanceToLight;
    const float fFactor = fDistanceSquare * fLightInvRadius * fLightInvRadius;
    const float fSmoothFactor = max(1.0 - fFactor * fFactor, 0.0);
    return (fSmoothFactor * fSmoothFactor) / max(fDistanceSquare, 1e-4);
}

float SpotAngleAttenuation(vec3 vPosToLight, vec3 vLightDir, float fCosInnerAngle, float fCosOuterAngle) 
{
    // the scale and offset computations can be done CPU-side
    const float fSpotScale = 1.0 / max(fCosInnerAngle - fCosOuterAngle, 1e-4);
    const float fSpotOffset = -fCosOuterAngle * fSpotScale;

    const float cd = dot(normalize(-vLightDir), vPosToLight);
    const float fAttenuation = clamp(cd * fSpotScale + fSpotOffset, 0.0, 1.0);
    return fAttenuation * fAttenuation;
}

vec3 ComputeDirectLighting(
    LightData light,  // don't forget to covert position and direction to view space
    Material material,
    vec3 vPosInView,
    vec3 vNormalInView)
{
    // Punctual light: Radiance = BSDF * Irradiance

    vec3 vIrradiance = vec3(0.0f);
    float NdotL = 1.0;
    float fDistance = 0.0f;
    float fAttenuation = 0.0f;
    vec3 vPosToLight = vec3(0.0f);
    switch(light.LightType)
    {
        case LIGHT_TYPE_DIRECTIONAL:
            // project irradiance onto shading surface
            NdotL = clamp(dot(vNormalInView, -light.vDirection.xyz), 0.0, 1.0);
            vIrradiance = light.vColor * light.fIntensity * NdotL;
            return vIrradiance * EvalBRDF(normalize(-light.vDirection.xyz), normalize(-vPosInView), vNormalInView, material);

        case LIGHT_TYPE_POINT:
        case LIGHT_TYPE_SPOT:
            vPosToLight = normalize(light.vPosition - vPosInView);
            fDistance = length(light.vPosition - vPosInView);

            fAttenuation = SquareFalloffAttenuation(fDistance, light.fRange);
            if (light.LightType == LIGHT_TYPE_SPOT)
            {
                fAttenuation *= SpotAngleAttenuation(vPosToLight, light.vDirection, cos(light.vLightData.y), cos(light.vLightData.z));
            }

            // Project irradiance onto shading surface
            NdotL = clamp(dot(vNormalInView, vPosToLight), 0.0, 1.0);
            vIrradiance = light.vColor * fAttenuation * light.fIntensity * NdotL;
            return vIrradiance * EvalBRDF(vPosToLight, normalize(-vPosInView), vNormalInView, material);
    }

    // return black if there's no supported light
    return vec3(0.0);
}


#endif

