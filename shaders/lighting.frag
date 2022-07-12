#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#include "directLighting.h"
#include "Camera.h"
#include "lights.h"


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

LIGHTS_UBO(3)

void main() {
    // GBuffer info
    const vec3 vWorldPos = subpassLoad(inGBuffer_POS_AO).xyz;
    const vec3 vViewPos = (uboCamera.view * vec4(vWorldPos, 1.0)).xyz;
    const float fAO = subpassLoad(inGBuffer_POS_AO).w;
    const vec3 vAlbedo = subpassLoad(inGBuffer_ALBEDO_TRANSMITTANCE).xyz;
    const vec3 vWorldNormal = subpassLoad(inGBuffer_NORMAL_ROUGHNESS).xyz;
    const vec3 vFaceNormal = normalize((uboCamera.view * vec4(vWorldNormal, 0.0)).xyz);
    const float fRoughness = subpassLoad(inGBuffer_NORMAL_ROUGHNESS).w;
    const float fMetalness = subpassLoad(inGBuffer_MATELNESS_TRANSLUCENCY).r;
    //TODO: Read transmittence and translucency if necessary

    Material material;
    // populate material struct with material properties
    material.vAlbedo = vAlbedo;
    material.fAO = fAO;
    material.fMetalness = fMetalness;
    material.fRoughness = fRoughness;
    material.vEmissive = vec3(0.0);


    vec3 vLo = vec3(0.0);

    // For each light source
    //
    for (int i = 0; i < numLights.nNumLights; ++i)
    {
        LightData light = lightDatas.i[i];
        // Convert light position to view space
        const vec4 lightPosition = uboCamera.view * vec4(light.vPosition, 1.0);
        light.vPosition = lightPosition.xyz / lightPosition.w;
        light.vDirection = (uboCamera.view * vec4(light.vDirection, 0.0)).xyz;

        vLo += ComputeDirectLighting(light, material, vViewPos, vFaceNormal);

        /*
        const vec3 vLightViewPos = (uboCamera.view * vec4(light.vPosition, 1.0)).xyz;

        vLo += ComputeDirectLighting(
            vLightViewPos,
            light.vColor * light.fIntensity,
            vViewPos.xyz,
            vFaceNormal,
            vAlbedo,
            fMetalness,
            fRoughness);
        */
    }

    // Add IBL
    
    const vec3 vF0 = mix(vec3(0.04), vAlbedo, fMetalness);
    vec3 V = normalize(-vViewPos);
    const float MAX_REFLECTION_LOD = 7.0;
    vec3 R = -normalize(uboCamera.viewInv * vec4(reflect(V, vFaceNormal), 0.0)).xyz;
    vec3 vPrefilteredColor = textureLod(prefilteredMap, R, fRoughness * MAX_REFLECTION_LOD).rgb;
    vec3 F = fresnelSchlickRoughness(max(dot(vFaceNormal, V), 0.0), vF0, fRoughness);
    vec2 vEnvBRDF  = texture(specularBrdfLut, vec2(max(dot(vFaceNormal, V), 0.0), fRoughness)).rg;
    vec3 specular = vPrefilteredColor * (F * vEnvBRDF.x + vEnvBRDF.y);
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - fMetalness;
    vec3 irradiance = texture(irradianceMap, vWorldNormal).xyz;
    vec3 vDiffuse = vAlbedo * irradiance;
    vec3 diffuse = irradiance * vAlbedo;

    vec3 vAmbient = (kD * vDiffuse + specular);// * fAO;

    vec3 vColor = vLo + vAmbient;
    //vec3 vColor = vLo;

    // Gamma correction
    //vColor = vColor / (vColor + vec3(1.0));
    //vColor = pow(vColor, vec3(1.0 / 2.2));

    outColor = vec4(vColor, 1.0);
    outColor.a = 1.0;
}
