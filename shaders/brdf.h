#ifndef BRDF_H
#define BRDF_H
#extension GL_GOOGLE_include_directive : enable
#include "random.h"
#include "lights.h"

struct Material
{
    vec3 vAlbedo;
    float fAO;
    float fMetalness;
    float fRoughness;
    vec3 vEmissive;
};


//===============
// PBR functions
const float PI = 3.14159265359;

vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float fRoughness)
{
    float a = fRoughness * fRoughness;

    float phi = 2.0 * PI * Xi.x;
    // Inverse of CDF to map 0,1 to the theta with higher possiblity
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta*cosTheta);
	
    // from spherical coordinates to cartesian coordinates
    vec3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;
	
    // from tangent-space vector to world-space sample vector
    vec3 up        = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent   = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
	
    vec3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}
float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a      = roughness*roughness;
    float a2     = a*a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;
	
    float num   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
	
    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return num / denom;
}

float GeometrySchlickGGXIBL(float NdotV, float roughness)
{
    float a = roughness;
    float k = (a * a) / 2.0;

    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2  = GeometrySchlickGGX(NdotV, roughness);
    float ggx1  = GeometrySchlickGGX(NdotL, roughness);
	
    return ggx1 * ggx2;
}
float GeometrySmithIBL(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2  = GeometrySchlickGGXIBL(NdotV, roughness);
    float ggx1  = GeometrySchlickGGXIBL(NdotL, roughness);
	
    return ggx1 * ggx2;
}
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}   

// Evaluate Cook Torrance BRDF
vec3 EvalBRDF(
    vec3 vWi,
    vec3 vWo,
    vec3 vN,
    Material material)
{
    const vec3 vF0 = mix(vec3(0.04), material.vAlbedo, material.fMetalness);
    const vec3 vH = normalize(vWi + vWo);

    // cook-torrance brdf
    const float fNDF = DistributionGGX(vN, vH, material.fRoughness);
    const float fG = GeometrySmith(vN, vWo, vWi, material.fRoughness);
    const vec3 vF = fresnelSchlick(max(dot(vH, vWo), 0.0), vF0);
    const vec3 vkS = vF;
    vec3 vkD = vec3(1.0) - vkS;
    vkD *= 1.0 - material.fMetalness;

    vec3 vNumerator = fNDF * fG * vF;
    float fDenominator = 4.0 * max(dot(vN, vWo), 0.0) * max(dot(vN, vWi), 0.0);
    vec3 vSpecular = vNumerator / max(fDenominator, 0.001);

    // add to outgoing radiance Lo
    float fNdotL = max(dot(vN, vWi), 0.0);
    return (vkD * material.vAlbedo / PI + vSpecular) * fNdotL;
}

vec2 IntegrateBRDF(float NdotV, float roughness)
{
    vec3 V;
    V.x = sqrt(1.0 - NdotV*NdotV);
    V.y = 0.0;
    V.z = NdotV;

    float A = 0.0;
    float B = 0.0;

    vec3 N = vec3(0.0, 0.0, 1.0);

    const uint SAMPLE_COUNT = 1024u;
    for(uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        vec2 Xi = Hammersley(i, SAMPLE_COUNT);
        vec3 H  = ImportanceSampleGGX(Xi, N, roughness);
        vec3 L  = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);

        if(NdotL > 0.0)
        {
            float G = GeometrySmithIBL(N, V, L, roughness);
            float G_Vis = (G * VdotH) / (NdotH * NdotV);
            float Fc = pow(1.0 - VdotH, 5.0);

            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    A /= float(SAMPLE_COUNT);
    B /= float(SAMPLE_COUNT);
    return vec2(A, B);
}
#endif
