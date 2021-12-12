#extension GL_EXT_scalar_block_layout : require
const int LIGHT_COUNT = 4;
const int USED_LIGHT_COUNT = 1;
const vec3 lightPositions[LIGHT_COUNT] = vec3[](
    vec3(0.0, 0.0, 0.0),
    vec3(0.0, 0.0, 0.0),
    vec3(0.0, 0.0, 0.0),
    vec3(0.0, 0.0, 0.0)
);
const vec3 lightColors[LIGHT_COUNT] = vec3[](
    vec3(0.0, 0.0, 0.0),
    vec3(0.0, 0.0, 0.0),
    vec3(0.0, 0.0, 0.0),
    vec3(0.0, 0.0, 0.0)
);

struct LightData
{
    uint LightType;
    vec3 vPosition;
    vec3 vColor;
    float fIntensity;
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
