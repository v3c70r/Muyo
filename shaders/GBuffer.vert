#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_scalar_block_layout : require

#include "Camera.h"
#include "shared/SharedStructures.h"
CAMERA_UBO(0)
layout(scalar, set = 1, binding = 0) readonly buffer PerObjData_ { PerObjData i[]; }perObjData;

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec4 inTexCoord;

layout (location = 0) out vec2 outTexCoords0;
layout (location = 1) out vec2 outTexCoords1;
layout (location = 2) out vec4 outWorldPos;
layout (location = 3) out vec4 outWorldNormal;
layout (location = 4) out uvec2 outObjSubmeshIndex;

out gl_PerVertex {
    vec4 gl_Position;
};

void main() {
    outTexCoords0 = inTexCoord.xy;
    outTexCoords1 = inTexCoord.zw;

    outObjSubmeshIndex = uvec2(GetObjectIndex(gl_InstanceIndex), GetSubmeshIndex(gl_InstanceIndex));
    mat4 mWorldMatrix = perObjData.i[outObjSubmeshIndex.x].mWorldMatrix;

    outWorldPos = mWorldMatrix * vec4(inPos, 1.0);
    outWorldNormal = normalize(mWorldMatrix * vec4(inNormal, 0.0));

    gl_Position = uboCamera.proj * uboCamera.view * outWorldPos;
}


