#ifndef METERIAL_H
#define MATERIAL_H

#extension GL_EXT_scalar_block_layout : require
#include "shared/SharedStructures.h"
#define MATERIAL_UBO(SET)                                               \
    layout(set = SET, binding = 0) uniform sampler2D texPBR[TEX_COUNT]; \
    layout(std430, set = SET, binding = 1) uniform PBRFactorUBO { PBRMaterial factors; };
#endif
