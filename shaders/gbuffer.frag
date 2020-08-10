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

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inViewPos;

layout(location = 0) out vec4 outPosition;
layout(location = 1) out vec4 outAlbedo;
layout(location = 2) out vec4 outNormal;
layout(location = 3) out vec4 outUV;

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

    const vec3 vView = normalize(-inViewPos.xyz);

    const vec3 vAlbedo = texture(texPBR[TEX_ALBEDO], inTexCoord).xyz;
    const float fMetallic = texture(texPBR[TEX_METALNESS], inTexCoord).r;
    const float fRoughness = texture(texPBR[TEX_ROUGHNESS], inTexCoord).r;
    const float fAO = texture(texPBR[TEX_AO], inTexCoord).r;

    vec3 vF0 = mix(vec3(0.04), vAlbedo, fMetallic);

    vec3 vLo = vec3(0.0);

    // For each light
    for(int i = 0; i < USED_LIGHT_COUNT; ++i) 
    {
        // calculate per-light radiance

        //vec3 lightPosView = (ubo.view * ubo.model * vec4(lightPositions[i], 1.0)).xyz;
        vec3 lightPosView = (vec4(lightPositions[i], 1.0)).xyz;
        vec3 L = normalize(lightPosView - inViewPos.xyz);
        vec3 H = normalize(vView + L);
        float distance    = length(lightPosView - inViewPos.xyz);
        float attenuation = 1.0 / (distance * distance);
        vec3 radiance     = lightColors[i] * attenuation;        
        
        // cook-torrance brdf
        float NDF = DistributionGGX(vFaceNormal, H, fRoughness);        
        float G   = GeometrySmith(vFaceNormal, vView, L, fRoughness);      
        vec3 F    = fresnelSchlick(max(dot(H, vView), 0.0), vF0);       
        
        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - fMetallic;	  
        
        vec3 numerator    = NDF * G * F;
        float denominator = 4.0 * max(dot(vFaceNormal, vView), 0.0) * max(dot(vFaceNormal, L), 0.0);
        vec3 specular     = numerator / max(denominator, 0.001);  
            
        // add to outgoing radiance Lo
        float NdotL = max(dot(vFaceNormal, L), 0.0);                
        vLo += (kD * vAlbedo / PI + specular) * radiance * NdotL; 
    }   

    vec3 vAmbient = vec3(0.03) * vAlbedo * fAO;
    vec3 vColor = vAmbient + vLo;

    // Gamma correction
    vColor = vColor / (vColor + vec3(1.0));
    vColor = pow(vColor, vec3(1.0 / 2.2));


    outPosition = vec4(vColor, 1.0);
    outAlbedo = texture(texPBR[TEX_ALBEDO], inTexCoord);
    outNormal = vec4(vFaceNormal, 1.0);
    outUV = texture(texPBR[TEX_ROUGHNESS], inTexCoord);
    // Calculate screen space normal,,, for now.
    //outNormal = vec4(facenormal, 0.0);
    //outUV = vec4(inTexCoord, 0.0, 0.0);
}
