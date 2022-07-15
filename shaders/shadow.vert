#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

#include "Camera.h"
CAMERA_UBO(0)

layout (set = 1, binding = 0) uniform WorldMatrix {
    mat4 mWorldMatrix;
} worldMatrix;

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec4 inTexCoord;

out gl_PerVertex {
    vec4 gl_Position;
};

void main() {
    gl_Position = uboCamera.proj * uboCamera.view * worldMatrix.mWorldMatrix * vec4(inPos, 1.0);
}




