#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 inTexCoords0;
layout(location = 1) in vec2 inTexCoords1;
layout(location = 2) in vec4 inWorldPos;
layout(location = 3) in vec4 inWorldNormal;

layout(location = 0) out vec4 outPositionAO;
layout(location = 1) out vec4 outAlbedoTransmittance;
layout(location = 2) out vec4 outNormalRoughness;
layout(location = 3) out vec4 outMatelnessTranslucency;

layout (set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;

    mat4 objectToView;
    mat4 viewToObject;
    mat4 normalObjectToView;
} ubo;

// Materials
const uint TEX_ALBEDO = 0;
const uint TEX_NORMAL = 1;
const uint TEX_METALNESS = 2;
const uint TEX_ROUGHNESS = 3;
const uint TEX_AO = 4;
const uint TEX_COUNT = 5;

layout(set = 1, binding = 0) uniform sampler2D texPBR[TEX_COUNT];
layout(set = 1, binding = 1) uniform PBRFactors {
    vec4 vBaseColorFactors;
    float fRoughness;
    float fMetalness;
    uint UVIndices0;
    uint UVIndices1;
    uint UVIndices2;
    uint UVIndices3;
    uint UVIndices4;
    float padding0;
} factors;

void main() {

    vec2 inTexCoords[2];
    inTexCoords[0] = inTexCoords0;
    inTexCoords[1] = inTexCoords1;

    uint UVIndices[5];
    UVIndices[0] = factors.UVIndices0;
    UVIndices[1] = factors.UVIndices1;
    UVIndices[2] = factors.UVIndices2;
    UVIndices[3] = factors.UVIndices3;
    UVIndices[4] = factors.UVIndices4;
    vec3 vTextureNormal = texture(texPBR[TEX_NORMAL], inTexCoords[UVIndices[TEX_NORMAL]]).xyz;
    vec3 vWorldNormal = normalize(inWorldNormal.xyz + vTextureNormal);

    // Populate GBuffer
    outPositionAO = inWorldPos;
    outPositionAO.w = texture(texPBR[TEX_AO], inTexCoords[UVIndices[TEX_AO]]).r;
    outAlbedoTransmittance.xyz = texture(texPBR[TEX_ALBEDO], inTexCoords[UVIndices[TEX_ALBEDO]]).xyz * factors.vBaseColorFactors.xyz;
    outAlbedoTransmittance.w = 1.0; // Transmittance

    const float fRoughness = texture(texPBR[TEX_ROUGHNESS], inTexCoords[UVIndices[TEX_ROUGHNESS]]).g * factors.fRoughness;
    outNormalRoughness = vec4(vWorldNormal, fRoughness);

    // Probably need a specular map (Reflection map);
    outMatelnessTranslucency = vec4(texture(texPBR[TEX_METALNESS], inTexCoords[UVIndices[TEX_METALNESS]]).r * factors.fMetalness, 1.0, 0.0, 0.0);
}
