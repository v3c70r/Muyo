#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable

#include "Camera.h"

layout(location = 0) in vec2 texCoords;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D inTexture;
layout(rgba32f, set = 1, binding = 0) uniform image2D inRtTarget;

CAMERA_UBO(2)

void main() {
    if (texCoords.x < uboCamera.fLeftSplitScreenRatio)
    {
        outColor = texture(inTexture, texCoords);
    }
    else
    {
        outColor = imageLoad(inRtTarget, ivec2(imageSize(inRtTarget) * texCoords));
    }
    outColor.a = 1.0;
}
