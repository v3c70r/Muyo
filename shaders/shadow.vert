#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_scalar_block_layout : require

#include "shared/SharedStructures.h"

layout(scalar, set = 0, binding = 0) buffer LightData_ { LightData i[]; }
lightData;

layout (set = 1, binding = 0) uniform WorldMatrix {
    mat4 mWorldMatrix;
} worldMatrix;

layout (push_constant) uniform PushConstant {
    uint nLightIndex;
    uint nShadowMapSize;
} pushConstant;

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec4 inTexCoord;


layout (location = 0) out vec2 outTexCoords0;
layout (location = 1) out vec2 outTexCoords1;
layout (location = 2) out vec4 outWorldPos;
layout (location = 3) out vec4 outWorldNormal;

out gl_PerVertex {
    vec4 gl_Position;
};

void main() 
{
    outTexCoords0 = inTexCoord.xy;
    outTexCoords1 = inTexCoord.zw;
    outWorldPos = worldMatrix.mWorldMatrix * vec4(inPos, 1.0);
    outWorldNormal = normalize(worldMatrix.mWorldMatrix * vec4(inNormal, 0.0));
    const mat4 mLightViewProj = lightData.i[pushConstant.nLightIndex].mLightViewProjection;
    gl_Position = mLightViewProj * worldMatrix.mWorldMatrix * vec4(inPos, 1.0);
}




