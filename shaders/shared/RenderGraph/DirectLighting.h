#pragma once
#include "Lights.h"

// Direct punctual lighting, ported from shaders/directLighting.h + shaders/brdf.h.
//
// The GLSL originals also carry the IBL and RSM terms; those are deliberately absent here. IBL and
// shadows are out of scope for the GBUFFER/LIGHTING port (#20, #49), so this node's output is direct
// lighting only - a documented visual delta, not an approximation of the old frame.
//
// The BRDF math is copied rather than shared because the originals are GLSL with `#extension`
// directives, which Slang does not compile.

struct DirectMaterial
{
    float3 albedo;
    float ao;
    float metalness;
    float roughness;
    float3 emissive;
};

static const float PI = 3.14159265359;

float DistributionGGX(float3 N, float3 H, float roughness)
{
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float NdotH = max(dot(N, H), 0.0);
    const float NdotH2 = NdotH * NdotH;
    const float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    const float r = roughness + 1.0;
    const float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    const float NdotV = max(dot(N, V), 0.0);
    const float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float3 EvalBRDF(float3 Wi, float3 Wo, float3 N, DirectMaterial material)
{
    const float3 F0 = lerp(float3(0.04), material.albedo, material.metalness);
    const float3 H = normalize(Wi + Wo);

    const float NDF = DistributionGGX(N, H, material.roughness);
    const float G = GeometrySmith(N, Wo, Wi, material.roughness);
    const float3 F = FresnelSchlick(max(dot(H, Wo), 0.0), F0);
    const float3 kS = F;
    float3 kD = float3(1.0) - kS;
    kD *= 1.0 - material.metalness;

    const float3 numerator = NDF * G * F;
    const float denominator = 4.0 * max(dot(N, Wo), 0.0) * max(dot(N, Wi), 0.0);
    const float3 specular = numerator / max(denominator, 0.001);

    const float NdotL = max(dot(N, Wi), 0.0);
    return (kD * material.albedo / PI + specular) * NdotL;
}

float SquareFalloffAttenuation(float distanceToLight, float lightAffectedRadius)
{
    const float invRadius = 1.0 / lightAffectedRadius;
    const float distanceSquare = distanceToLight * distanceToLight;
    const float factor = distanceSquare * invRadius * invRadius;
    const float smoothFactor = max(1.0 - factor * factor, 0.0);
    return (smoothFactor * smoothFactor) / max(distanceSquare, 1e-4);
}

float SpotAngleAttenuation(float3 posToLight, float3 lightDir, float cosInnerAngle, float cosOuterAngle)
{
    const float spotScale = 1.0 / max(cosInnerAngle - cosOuterAngle, 1e-4);
    const float spotOffset = -cosOuterAngle * spotScale;
    const float cd = dot(normalize(-lightDir), posToLight);
    const float attenuation = clamp(cd * spotScale + spotOffset, 0.0, 1.0);
    return attenuation * attenuation;
}

// @param light     Light with position and direction already in view space.
// @param posInView Fragment world position transformed to view space.
// @param normalInView Fragment normal in view space.
float3 ComputeDirectLighting(Light light, DirectMaterial material, float3 posInView, float3 normalInView)
{
    if (light.type == LIGHT_TYPE_DIRECTIONAL)
    {
        const float NdotL = clamp(dot(normalInView, -light.direction), 0.0, 1.0);
        const float3 irradiance = light.color * light.intensity * NdotL;
        return irradiance * EvalBRDF(normalize(-light.direction), normalize(-posInView), normalInView, material);
    }

    if (light.type == LIGHT_TYPE_POINT || light.type == LIGHT_TYPE_SPOT)
    {
        const float3 posToLight = normalize(light.position - posInView);
        const float distance = length(light.position - posInView);
        float attenuation = SquareFalloffAttenuation(distance, light.range);
        if (light.type == LIGHT_TYPE_SPOT)
        {
            attenuation *= SpotAngleAttenuation(posToLight, light.direction, cos(light.data.y), cos(light.data.z));
        }
        const float NdotL = clamp(dot(normalInView, posToLight), 0.0, 1.0);
        const float3 irradiance = light.color * attenuation * light.intensity * NdotL;
        return irradiance * EvalBRDF(posToLight, normalize(-posInView), normalInView, material);
    }

    // No supported light type: black, matching the GLSL original.
    return float3(0.0);
}
