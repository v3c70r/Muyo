// This file contains structures that shared between shaders and c++ code
// SHADER_CODE defines the shader code, which is passed in while compiling the shader in CMakeLists.txt
// -------------------------------------------------------------------------------------------------------------
#ifndef SHARED_STRUCTURES_H
#define SHARED_STRUCTURES_H

#ifndef SHADER_CODE
#include <glm/glm.hpp>
using namespace glm;
#endif // SHADER_CODE

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
    mat4 mLightViewProjection;
};


#endif // SHARED_STRUCTURES_H
