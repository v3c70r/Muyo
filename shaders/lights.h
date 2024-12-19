#ifndef LIGHTS_H
#define LIGHTS_H

#extension GL_EXT_scalar_block_layout : require

#include "shared/SharedStructures.h"

// Macro to bind light buffer
#define LIGHTS_UBO(SET)                                                         \
    layout(std140, set = SET, binding = 0) uniform NumLightsUBO                 \
    {                                                                           \
        uint nNumLights;                                                        \
    }                                                                           \
    numLights;                                                                  \
    layout(scalar, set = SET, binding = 1) readonly buffer LightData_ { LightData i[]; } \
    lightDatas;

#endif
