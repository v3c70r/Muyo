#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 texCoords;
layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D texSampler;

vec4 drawAxis(vec2 center, float width)
{
    const vec3 X_COLOR = vec3(1.0, 0.0, 0.0);
    const vec3 Y_COLOR = vec3(0.0, 1.0, 0.0);
    const float LENGTH = 0.01;
    vec2 pos = gl_FragCoord.xy;
    if (pos.x < center.x + width && pos.x > center.x && pos.y < center.y + LENGTH && pos.y > center.y)
        return vec4(Y_COLOR, 1.0);
    if (pos.y < center.y + width && pos.y > center.y && pos.x < center.x + LENGTH && pos.x > center.x)
        return vec4(X_COLOR, 1.0);
    return texture(texSampler, texCoords);
}

void main() {
    outColor = texture(texSampler, texCoords);
    outColor.a = 1.0;
    outColor = drawAxis(vec2(0.5), 0.001);
}
