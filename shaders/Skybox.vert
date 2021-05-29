#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

#include "Camera.h"
CAMERA_UBO(0)

layout (location = 0) in vec3 vInPos;
layout (location = 0) out vec3 vTexCoord;

void main()
{
    vTexCoord = vInPos;
    vec4 vPos = uboCamera.proj * mat4(mat3(uboCamera.view)) * vec4(vInPos, 1.0);
    gl_Position = vPos.xyww;
}  
