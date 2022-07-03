#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#include "brdf.h"
#include "random.h"

layout(location = 0) in vec3 localPos;
layout(location = 0) out vec4 outIrradiance;

layout(set = 1, binding = 0) uniform samplerCube environmentMap;

layout (push_constant) uniform RoughnessPushConstant
{
    float fRoughness;
} roughness;

void main()
{
    vec3 N = normalize(localPos);
    vec3 R = N;
    vec3 V = R;

    const uint SAMPLE_COUNT = 1024u;
    float totalWeight = 0.0;
    vec3 prefilteredColor = vec3(0.0);
    for (uint i = 0 ; i<SAMPLE_COUNT; i++)
    {
        vec2 Xi = Hammersley(i, SAMPLE_COUNT);
        vec3 H = ImportanceSampleGGX(Xi, N, roughness.fRoughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0)
        {
            prefilteredColor += texture(environmentMap, L).rgb * NdotL;
            totalWeight += NdotL;
        }
    }
    prefilteredColor = prefilteredColor / totalWeight;
    outIrradiance = vec4(prefilteredColor, 1.0);
}
