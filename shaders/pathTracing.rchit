#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : enable

#include "brdf.h"
#include "Camera.h"
#include "lights.h"
#include "pathTracingPayload.h"
#include "random.h"


// Materials
const uint TEX_ALBEDO = 0;
const uint TEX_NORMAL = 1;
const uint TEX_METALNESS = 2;
const uint TEX_ROUGHNESS = 3;
const uint TEX_AO = 4;
const uint TEX_EMISSIVE = 5;
const uint TEX_COUNT = 6;

struct PrimitiveDesc
{
    uint64_t vertexAddress;
    uint64_t indexAddress;
    uint64_t pbrFactorsAddress;
    uint pbrTextureIndices[TEX_COUNT];
};

struct Vertex
{
    vec3 pos;
    vec3 normal;
    vec4 textureCoord;
};

CAMERA_UBO(0)
layout(set = 0, binding = 1) uniform accelerationStructureEXT topLevelAS;
layout(set = 0, binding = 4, scalar) buffer PrimDesc_ {PrimitiveDesc i[]; } primDescs;
layout(set = 0, binding = 5) uniform sampler2D AllTextures[];
layout(set = 2, binding = 6) uniform samplerCube environmentMap;
// Light data
LIGHTS_UBO(1)

layout(buffer_reference, scalar) buffer Vertices {Vertex v[]; }; // Positions of an object
layout(buffer_reference, scalar) buffer Indices {ivec3 i[]; }; // Triangle indices
layout(buffer_reference, scalar) buffer PBRFactors {
    vec4 vBaseColorFactors;
    float fMetalness;
    float fRoughness;
    uint UVIndices0;
    uint UVIndices1;
    uint UVIndices2;
    uint UVIndices3;
    uint UVIndices4;
    uint UVIndices5;
    vec3 vEmissiveFactor;
    float padding0;
};




layout(location = 0) rayPayloadInEXT RayPayload ray;
hitAttributeEXT vec2 vHitAttribs;


// integrate environment diffuse lighting
vec3 IntegrateEnvironmentDiffuse(vec3 vNormal)
{
    const float PI = 3.14159265359;
    vec3 vIrradiance = vec3(0.0f);
    vec3 vUp = vec3(0.0, 1.0, 0.0);
    vec3 vRight = cross(vUp, vNormal);
    vUp = cross(vRight, vNormal);

    float fSampleDelta = 0.5f;
    float fNumSamples = 0.0f;

    for (float fPhi = 0.0; fPhi < 2.0 * PI; fPhi += fSampleDelta)
    {
        for (float fTheta = 0.0; fTheta < PI; fTheta += fSampleDelta)
        {
            vec3 vSampleDirTangent = vec3(sin(fTheta) * cos(fPhi), sin(fTheta) * sin(fPhi), cos(fTheta));
            vec3 vSampleDir = vSampleDirTangent * vRight + vSampleDirTangent.y * vUp + vSampleDirTangent.z * vNormal;
            vSampleDir = normalize(vSampleDir);
            vIrradiance += texture(environmentMap, vSampleDir).rgb * cos(fTheta) * sin(fTheta);
            fNumSamples += 1.0f;
        }
    }
    return PI * vIrradiance / fNumSamples;
}



// Helper functions
vec3 InterpolateBarycentric(vec3 p0, vec3 p1, vec3 p2, vec3 vBarycentrics)
{
    return p0 * vBarycentrics.x + p1 * vBarycentrics.y + p2 * vBarycentrics.z;
}

// Helper functions
vec2 InterpolateBarycentric(vec2 p0, vec2 p1, vec2 p2, vec3 vBarycentrics)
{
    return p0 * vBarycentrics.x + p1 * vBarycentrics.y + p2 * vBarycentrics.z;
}

Vertex InterpolateVertex(Vertex v0, Vertex v1, Vertex v2, vec3 vBarycentrics)
{
    
    Vertex v;
    v.pos = InterpolateBarycentric(v0.pos, v1.pos, v2.pos, vBarycentrics);
    v.normal = InterpolateBarycentric(v0.normal, v1.normal, v2.normal, vBarycentrics);
    v.textureCoord.xy = InterpolateBarycentric(v0.textureCoord.xy, v1.textureCoord.xy, v2.textureCoord.xy, vBarycentrics);
    v.textureCoord.zw = InterpolateBarycentric(v0.textureCoord.zw, v1.textureCoord.zw, v2.textureCoord.zw, vBarycentrics);
    return v;
}

RayPayload ScatterDiffuse(const vec3 vDirection, const vec3 vNormal, const Material material, const float t, inout uint nSeed)
{
    vec3 vSampleDir = RandomInHemiSphere(nSeed, vNormal);

    // TODO: Check why here is not divided by PI
    const vec3 vColor = material.vAlbedo * dot(vDirection * -1.0, vNormal) * 2.0;
        
    return RayPayload(vec4(vColor, t), vec4(vSampleDir, 1.0f), nSeed);
}

RayPayload ScatterSpecular(const vec3 vDirection, const vec3 vNormal, const Material material, const float t, inout uint nSeed)
{
    vec3 vSampleDir = RandomInHemiSphere(nSeed, vNormal);

    const vec3 V = vDirection * -1.0;
    const vec3 vF0 = mix(vec3(0.04), material.vAlbedo, material.fMetalness);
    const vec3 L = vSampleDir;
    const vec3 H = normalize(L + V);

    // cook-torrance brdf
    const float NDF = DistributionGGX(vNormal, H, material.fRoughness);
    const float G = GeometrySmith(vNormal, V, L, material.fRoughness);
    const vec3 F = fresnelSchlick(max(dot(H, V), 0.0), vF0);
    const vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - material.fMetalness;

    const vec3 vColor = PI * 0.5 * NDF * G * F / dot(vDirection * -1.0, vNormal);


    return RayPayload(vec4(vColor, t), vec4(vSampleDir, 1.0), nSeed);
}

RayPayload ScatterBRDF(const vec3 vDirection, const vec3 vNormal, const Material material, const float t, inout uint nSeed)
{
    if (material.fMetalness == 0.0)
    {
        return ScatterDiffuse(vDirection, vNormal, material, t, nSeed);
    }
    else
    {
        return ScatterSpecular(vDirection, vNormal, material, t, nSeed);
    }
}

RayPayload ScatterRay(const vec3 vDirection, const vec3 vNormal, const Material material, const float t, inout uint nSeed)
{
    const vec3 vReflected = reflect(vDirection, vNormal);
    const bool bIsScattered = dot(vReflected, vNormal) > 0;
    const vec3 vF0 = mix(vec3(0.04), material.vAlbedo, material.fMetalness);
    vec3 F = fresnelSchlickRoughness(max(dot(vNormal, -vDirection), 0.0), vF0, material.fRoughness);
    vec3 fKs = F;
    vec3 fKd = vec3(1.0f) - fKs;
    if (bIsScattered)
    {
        return RandomFloat(nSeed) < fKs.x ? ScatterSpecular(vDirection, vNormal, material, t, nSeed)
                                                   : ScatterDiffuse(vDirection, vNormal, material, t, nSeed);
    }
    else
    {
        return RayPayload(vec4(0.0f), vec4(0.0f, 0.0f, 0.0f, 0.0f), 0);
    }
}

RayPayload ScatterRay(const vec3 vDirection, const vec3 vNormal, const vec3 vAlbedo, const float fRoughness, const float t, inout uint nSeed)
{
    const vec3 vReflected = reflect(vDirection, vNormal);
    const bool bIsScattered = dot(vReflected, vNormal) > 0;

    const vec4 vColorAndDistance = vec4(vAlbedo * dot(vReflected, vNormal), t);
    const vec4 vScatter = vec4(vReflected + fRoughness * RandomInUnitSphere(nSeed), bIsScattered ? 1 : 0);

    return RayPayload(vColorAndDistance, vScatter, nSeed);
}



RayPayload IntegrateSkylightRay(const vec3 vNormal)
{
    const vec3 vColor = IntegrateEnvironmentDiffuse(vNormal);
    return RayPayload(vec4(vColor, 1.0), vec4(0.0, 0.0, 0.0, 0.0), 0);
}

// Diffuse Light
RayPayload ScatterDiffuseLight(const vec3 vEmissive, const float t, inout uint seed)
{
	const vec4 colorAndDistance = vec4(vEmissive, t);
	const vec4 scatter = vec4(1, 0, 0, 0);

	return RayPayload(colorAndDistance, scatter, seed);
}

void main()
{
    PrimitiveDesc primDesc = primDescs.i[gl_InstanceCustomIndexEXT + gl_GeometryIndexEXT];
    Indices indices = Indices(primDesc.indexAddress);
    Vertices vertices = Vertices(primDesc.vertexAddress);
    PBRFactors pbrFactors = PBRFactors(primDesc.pbrFactorsAddress);

    uint texPBRIndieces[TEX_COUNT];
    for (int i = 0 ; i < TEX_COUNT; i++)
    {
        texPBRIndieces[i] = primDesc.pbrTextureIndices[i];
    }

    ivec3 triangleIndex = indices.i[gl_PrimitiveID];
    
    // Vertex of the triangle
    Vertex v0 = vertices.v[triangleIndex.x];
    Vertex v1 = vertices.v[triangleIndex.y];
    Vertex v2 = vertices.v[triangleIndex.z];

    const vec3 barycentrics = vec3(1.0f - vHitAttribs.x - vHitAttribs.y, vHitAttribs.x, vHitAttribs.y);
    Vertex v = InterpolateVertex(v0, v1, v2, barycentrics);

    vec2 vTexCoords[2];
    vTexCoords[0] = v.textureCoord.xy;
    vTexCoords[1] = v.textureCoord.zw;

    uint UVIndices[TEX_COUNT];
    UVIndices[0] = pbrFactors.UVIndices0;
    UVIndices[1] = pbrFactors.UVIndices1;
    UVIndices[2] = pbrFactors.UVIndices2;
    UVIndices[3] = pbrFactors.UVIndices3;
    UVIndices[4] = pbrFactors.UVIndices4;


    // Computing the coordinates of the hit position
    const vec3 vPos      = v0.pos * barycentrics.x + v1.pos * barycentrics.y + v2.pos * barycentrics.z;
    vec3 vWorldPos = (gl_ObjectToWorldEXT * vec4(vPos, 1.0));  // Transforming the position to world space

    const vec3 vTextureNormal = texture(AllTextures[nonuniformEXT(texPBRIndieces[TEX_NORMAL])], vTexCoords[UVIndices[TEX_NORMAL]]).xyz;
    vec3 vWorldNormal = normalize((gl_ObjectToWorldEXT * vec4(v.normal, 0.0)).xyz + vTextureNormal);

    const vec3 vAlbedo = texture(AllTextures[nonuniformEXT(texPBRIndieces[TEX_ALBEDO])], vTexCoords[UVIndices[TEX_ALBEDO]]).xyz * pbrFactors.vBaseColorFactors.xyz;;
    const float fAO = texture(AllTextures[nonuniformEXT(texPBRIndieces[TEX_AO])], vTexCoords[UVIndices[TEX_AO]]).r;
    const float fRoughness = texture(AllTextures[nonuniformEXT(texPBRIndieces[TEX_ROUGHNESS])], vTexCoords[UVIndices[TEX_ROUGHNESS]]).r * pbrFactors.fRoughness;
    const float fMetalness = texture(AllTextures[nonuniformEXT(texPBRIndieces[TEX_METALNESS])], vTexCoords[UVIndices[TEX_METALNESS]]).r * pbrFactors.fMetalness;
    const vec3 vEmissive = texture(AllTextures[nonuniformEXT(texPBRIndieces[TEX_ALBEDO])], vTexCoords[UVIndices[TEX_ALBEDO]]).xyz * pbrFactors.vEmissiveFactor;

    Material material;
    // Fill material structure
    material.vAlbedo = vAlbedo;
    material.fAO = fAO;
    material.fMetalness = fMetalness;
    material.fRoughness = 0.1;
    material.vEmissive = vEmissive;

    

    const vec3 vViewPos = (uboCamera.view * vec4(vWorldPos, 1.0)).xyz;
    const vec3 vFaceNormal = normalize((uboCamera.view * vec4(vWorldNormal, 0.0)).xyz);

    if (abs(length(vEmissive)) < 0.01)
    {
        //  ray = ScatterRay(gl_WorldRayDirectionEXT, vWorldNormal, vAlbedo, fRoughness, gl_HitTEXT, ray.uRandomSeed);
        //ray = ScatterRay(gl_WorldRayDirectionEXT, vWorldNormal, material, gl_HitTEXT, ray.uRandomSeed);
        ray = ScatterBRDF(gl_WorldRayDirectionEXT, vWorldNormal, material, gl_HitTEXT, ray.uRandomSeed);
    }
    else
    {
        ray = ScatterDiffuseLight(vEmissive, gl_HitTEXT, ray.uRandomSeed);
    }


}
