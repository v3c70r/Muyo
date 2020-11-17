#version 450
#extension GL_ARB_separate_shader_objects : enable

//===============
// PBR functions
const float PI = 3.14159265359;
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
    float k = (r*r) / 8.0;

    float num   = NdotV;
    float denom = NdotV * (1.0 - k) + k;
	
    return num / denom;
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2  = GeometrySchlickGGX(NdotV, roughness);
    float ggx1  = GeometrySchlickGGX(NdotL, roughness);
	
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

layout (set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;

    mat4 objectToView;
    mat4 viewToObject;
    mat4 normalObjectToView;
} ubo;

layout(set = 1, binding = 0) uniform sampler2D inGBuffers[GBUFFER_COUNT];
layout(set = 2, binding = 0) uniform samplerCube irradianceMap;

void main() {
    // GBuffer info
    const vec3 vWorldPos = texture(inGBuffers[GBUFFER_POS_AO], texCoords).xyz;
    const vec3 vViewPos = (ubo.view * vec4(vWorldPos, 1.0)).xyz;
    const float fAO = texture(inGBuffers[GBUFFER_POS_AO], texCoords).w;
    const vec3 vAlbedo = texture(inGBuffers[GBUFFER_ALBEDO_TRANSMITTANCE], texCoords).xyz;
    const vec3 vWorldNormal = texture(inGBuffers[GBUFFER_NORMAL_ROUGHNESS], texCoords).xyz;
    const vec3 vFaceNormal = normalize((ubo.objectToView * vec4(vWorldNormal, 0.0)).xyz);
    const float fRoughness = texture(inGBuffers[GBUFFER_NORMAL_ROUGHNESS], texCoords).w;
    const float fMetallic = texture(inGBuffers[GBUFFER_METALNESS_TRANSLUCENCY], texCoords).r;
    //TODO: Read transmittence and translucency if necessary

    vec3 vF0 = mix(vec3(0.04), vAlbedo, fMetallic);
    vec3 vLo = vec3(0.0);
    vec3 V = normalize(-vViewPos);

    // For each light source
    for(int i = 0; i < USED_LIGHT_COUNT; ++i)
    {
        vec3 lightPosView = (vec4(lightPositions[i], 1.0)).xyz;
        vec3 L = normalize(lightPosView - vViewPos.xyz);
        vec3 H = normalize(L + V);
        float distance = length(lightPosView - vViewPos.xyz);
        float attenuation = 1.0 / (distance * distance);
        vec3 radiance = lightColors[i] * attenuation;

        // cook-torrance brdf
        float NDF = DistributionGGX(vFaceNormal, H, fRoughness);
        float G = GeometrySmith(vFaceNormal, V, L, fRoughness);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), vF0);
        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - fMetallic;

        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(vFaceNormal, V), 0.0) *
                            max(dot(vFaceNormal, L), 0.0);
        vec3 specular = numerator / max(denominator, 0.001);

        // add to outgoing radiance Lo
        float NdotL = max(dot(vFaceNormal, L), 0.0);
        vLo += (kD * vAlbedo / PI + specular) * radiance * NdotL;
    }
    // Calculate ambiance
    vec3 kS = fresnelSchlickRoughness(max(dot(vFaceNormal, V), 0.0), vF0, fRoughness);
    vec3 kD = 1.0 - kS;
    vec3 irradiance = texture(irradianceMap, vWorldNormal).xyz * 0.03;
    vec3 vDiffuse = irradiance * vAlbedo;
    vec3 vAmbient = (kD * vDiffuse) * fAO;

    vec3 vColor = vLo;

    // Gamma correction
    vColor = vColor / (vColor + vec3(1.0));
    vColor = pow(vColor, vec3(1.0 / 2.2));

    outColor = vec4(vColor, 1.0);
    //outColor = vec4(vWorldNormal, 1.0);
    //outColor = texture(inGBuffers[GBUFFER_POS_AO], texCoords);
    outColor.a = 1.0;
}
