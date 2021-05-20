#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 texCoords;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D inTexture;
layout(rgba32f, set = 1, binding = 0) uniform image2D inRtTarget;

void main() {
    const vec2 vViewport = vec2(1024.0f, 768.0f);
    if (texCoords.x > 0.5f)
    {
        outColor = texture(inTexture, texCoords);
    }
    else
    {
        outColor = imageLoad(inRtTarget, ivec2(vViewport * texCoords));
    }
    outColor.a = 1.0;
}
