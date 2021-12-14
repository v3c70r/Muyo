#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 texCoords;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D inTexture;

void main() {
    outColor = texture(inTexture, texCoords);
    outColor.a = 1.0;
}
