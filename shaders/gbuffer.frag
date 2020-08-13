#version 450
#extension GL_ARB_separate_shader_objects : enable



layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inViewPos;

layout(location = 0) out vec4 outPositionAO;
layout(location = 1) out vec4 outAlbedoTransmittance;
layout(location = 2) out vec4 outNormalRoughness;
layout(location = 3) out vec4 outMatelnessTranslucency;

const uint TEX_ALBEDO = 0;
const uint TEX_NORMAL = 1;
const uint TEX_METALNESS = 2;
const uint TEX_ROUGHNESS = 3;
const uint TEX_AO = 4;
const uint TEX_COUNT = 5;
layout (set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;
layout(set = 1, binding = 0) uniform sampler2D texPBR[TEX_COUNT];

void main() {

    // Caclulate normals
    // TODO: move normal map to tangent space
    vec3 dFdxPos = dFdx( inViewPos.xyz );
    vec3 dFdyPos = dFdy( inViewPos.xyz );
    vec3 vFaceNormal = normalize( cross(dFdxPos,dFdyPos ));
    vec3 vTextureNormal = texture(texPBR[TEX_NORMAL], inTexCoord).xyz;
    vFaceNormal = normalize(vFaceNormal + vTextureNormal);

    // Populate GBuffer
    outPositionAO = inViewPos;
    outPositionAO.w = texture(texPBR[TEX_AO], inTexCoord).r;
    outAlbedoTransmittance.xyz = texture(texPBR[TEX_ALBEDO], inTexCoord).xyz;
    outAlbedoTransmittance.w = 1.0; // Transmittance

    const float fRoughness =
        texture(texPBR[TEX_ROUGHNESS], inTexCoord).r;
    outNormalRoughness =
        vec4(vFaceNormal, fRoughness);

    // Probably need a specular map (Reflection map);
    outMatelnessTranslucency =
        vec4(texture(texPBR[TEX_METALNESS], inTexCoord).r, 1.0, 0.0, 0.0);
}
