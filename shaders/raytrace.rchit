#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : enable

#include "brdf.h"
#include "Camera.h"
CAMERA_UBO(0)

// Materials
const uint TEX_ALBEDO = 0;
const uint TEX_NORMAL = 1;
const uint TEX_METALNESS = 2;
const uint TEX_ROUGHNESS = 3;
const uint TEX_AO = 4;
const uint TEX_COUNT = 5;

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

layout(buffer_reference, scalar) buffer Vertices {Vertex v[]; }; // Positions of an object
layout(buffer_reference, scalar) buffer Indices {ivec3 i[]; }; // Triangle indices
layout(buffer_reference, scalar) buffer PBRFactors {
    vec4 vBaseColorFactors;
    float fRoughness;
    float fMetalness;
    uint UVIndices0;
    uint UVIndices1;
    uint UVIndices2;
    uint UVIndices3;
    uint UVIndices4;
    float padding0;
};

layout(set = 1, binding = 2, scalar) buffer PrimDesc_ {PrimitiveDesc i[]; } primDescs;
layout(set = 1, binding = 3) uniform sampler2D AllTextures[];

layout(location = 0) rayPayloadInEXT vec3 vResColor;
hitAttributeEXT vec2 vHitAttribs;

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


    const vec3 vViewPos = (uboCamera.view * vec4(vWorldPos, 1.0)).xyz;

    const vec3 vFaceNormal = normalize((uboCamera.view * vec4(vWorldNormal, 0.0)).xyz);
    // For each light source
    //
    vec3 vLo = vec3(0.0, 0.0, 0.0);
    for(int i = 0; i < USED_LIGHT_COUNT; ++i)
    {
        vLo += ComputeDirectLighting(
                lightPositions[i],
                lightColors[i],
                vViewPos,
                vFaceNormal,
                vAlbedo,
                fMetalness, 
                fRoughness);
    }


    vResColor = vLo;

    vResColor = vResColor / (vResColor + vec3(1.0));
    vResColor = pow(vResColor, vec3(1.0 / 2.2));

    //vResColor = vec3(1.0, 0.0, 0.0);
}
