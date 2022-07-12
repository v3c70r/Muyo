#ifndef LIGHTS_H
#define LIGHTS_H

#extension GL_EXT_scalar_block_layout : require

const uint LIGHT_TYPE_POINT = 0;
const uint LIGHT_TYPE_SPOT = 1;
const uint LIGHT_TYPE_DIRECTIONAL = 2;
const uint LIGHT_TYPE_LINEAR = 3;
const uint LIGHT_TYPE_POLYGON = 4;
const uint LIGHT_TYPE_COUNT = 5;

struct LightData
{
    uint LightType;
    vec3 vPosition;
    vec3 vDirection;
    float fRange;
    vec3 vColor;
    float fIntensity;
    vec4 vLightData;
};

// Macro to bind light buffer
#define LIGHTS_UBO(SET)                                                         \
    layout(std140, set = SET, binding = 0) uniform NumLightsUBO                 \
    {                                                                           \
        uint nNumLights;                                                        \
    }                                                                           \
    numLights;                                                                  \
    layout(scalar, set = SET, binding = 1) buffer LightData_ { LightData i[]; } \
    lightDatas;

#endif
