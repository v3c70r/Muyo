#ifndef METERIAL_H
#define MATERIAL_H
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
#endif
