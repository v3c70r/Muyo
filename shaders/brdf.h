#ifndef BRDF_H
#define BRDF_H
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

vec3 ComputeDirectLighting(
        vec3 vLightPos,     // Light pos in view space
        vec3 vLightColor,
        vec3 vViewPos,
        vec3 vNormal,
        vec3 vAlbedo,
        float fMetallic,
        float fRoughness
        )
{
    const vec3 V = normalize(-vViewPos);
    const vec3 vF0 = mix(vec3(0.04), vAlbedo, fMetallic);
    const vec3 L = normalize(vLightPos - vViewPos);
    const vec3 H = normalize(L + V);
    const float distance = length(vLightPos - vViewPos);
    const float attenuation = 1.0 / (distance * distance);
    const vec3 radiance = vLightColor * attenuation;

    // cook-torrance brdf
    const float NDF = DistributionGGX(vNormal, H, fRoughness);
    const float G = GeometrySmith(vNormal, V, L, fRoughness);
    const vec3 F = fresnelSchlick(max(dot(H, V), 0.0), vF0);
    const vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - fMetallic;

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(vNormal, V), 0.0) *
                        max(dot(vNormal, L), 0.0);
    vec3 specular = numerator / max(denominator, 0.001);

    // add to outgoing radiance Lo
    float NdotL = max(dot(vNormal, L), 0.0);
    return (kD * vAlbedo / PI + specular) * radiance * NdotL;
}
#endif
