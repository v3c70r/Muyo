#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#include "brdf.h"
#include "Camera.h"

// Light informations
const int LIGHT_COUNT = 4;
const int USED_LIGHT_COUNT = 1;
const vec3 lightPositions[LIGHT_COUNT] = vec3[](
    vec3(1.0, 1.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0),
    vec3(1.0, 1.0, 0.0)
);
const vec3 lightColors[LIGHT_COUNT] = vec3[](
    vec3(1.0, 1.0, 1.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0),
    vec3(1.0, 1.0, 0.0)
);
//===============

// GBuffer texture indices
const uint GBUFFER_POS_AO = 0;
const uint GBUFFER_ALBEDO_TRANSMITTANCE = 1;
const uint GBUFFER_NORMAL_ROUGHNESS = 2;
const uint GBUFFER_METALNESS_TRANSLUCENCY = 3;
const uint GBUFFER_COUNT = 4;

layout(location = 0) in vec2 texCoords;
layout(location = 0) out vec4 outColor;

CAMERA_UBO(0)

layout(input_attachment_index = 0, set = 1, binding = 0) uniform subpassInput inGBuffer_POS_AO;
layout(input_attachment_index = 1, set = 1, binding = 1) uniform subpassInput inGBuffer_ALBEDO_TRANSMITTANCE;
layout(input_attachment_index = 2, set = 1, binding = 2) uniform subpassInput inGBuffer_NORMAL_ROUGHNESS;
layout(input_attachment_index = 3, set = 1, binding = 3) uniform subpassInput inGBuffer_MATELNESS_TRANSLUCENCY;

// IBL parameters
layout(set = 2, binding = 0) uniform samplerCube irradianceMap;
layout(set = 2, binding = 1) uniform samplerCube prefilteredMap;
layout(set = 2, binding = 2) uniform sampler2D specularBrdfLut;

void main() {
    // GBuffer info
    const vec3 vWorldPos = subpassLoad(inGBuffer_POS_AO).xyz;
    const vec3 vViewPos = (uboCamera.view * vec4(vWorldPos, 1.0)).xyz;
    const float fAO = subpassLoad(inGBuffer_POS_AO).w;
    const vec3 vAlbedo = subpassLoad(inGBuffer_ALBEDO_TRANSMITTANCE).xyz;
    const vec3 vWorldNormal = subpassLoad(inGBuffer_NORMAL_ROUGHNESS).xyz;
    const vec3 vFaceNormal = normalize((uboCamera.view * vec4(vWorldNormal, 0.0)).xyz);
    const float fRoughness = subpassLoad(inGBuffer_NORMAL_ROUGHNESS).w;
    const float fMetallic = subpassLoad(inGBuffer_MATELNESS_TRANSLUCENCY).r;
    //TODO: Read transmittence and translucency if necessary

    vec3 vF0 = mix(vec3(0.04), vAlbedo, fMetallic);
    vec3 vLo = vec3(0.0);
    vec3 V = normalize(-vViewPos);

    // For each light source
    //
    for(int i = 0; i < USED_LIGHT_COUNT; ++i)
    {
        vLo += ComputeDirectLighting(
                lightPositions[i],
                lightColors[i],
                vViewPos.xyz,
                vFaceNormal,
                vAlbedo,
                fMetallic, 
                fRoughness);
    }

    // Add IBL
    const float MAX_REFLECTION_LOD = 4.0;
    vec3 R = reflect(-V, vFaceNormal);
    vec3 vPrefilteredColor = textureLod(prefilteredMap, R, fRoughness * MAX_REFLECTION_LOD).rgb;
    vec3 F = fresnelSchlickRoughness(max(dot(vFaceNormal, V), 0.0), vF0, fRoughness);
    vec2 vEnvBRDF  = texture(specularBrdfLut, vec2(max(dot(vFaceNormal, V), 0.0), fRoughness)).rg;
    vec3 specular = vPrefilteredColor * (F * vEnvBRDF.x + vEnvBRDF.y);
    vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - fMetallic;
    vec3 irradiance = texture(irradianceMap, vWorldNormal).xyz;
    vec3 vDiffuse = vAlbedo * irradiance * 0.1;
    vec3 diffuse = irradiance * vAlbedo;

    vec3 vAmbient = (kD * vDiffuse + specular) * fAO;

    vec3 vColor = vLo + vAmbient;

    // Gamma correction
    vColor = vColor / (vColor + vec3(1.0));
    vColor = pow(vColor, vec3(1.0 / 2.2));

    outColor = vec4(vColor, 1.0);
    //outColor = vec4(vWorldNormal, 1.0);
    outColor.a = 1.0;
}
