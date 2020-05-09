#version 450
#extension GL_ARB_separate_shader_objects : enable

const uint GBUFFER_POS = 0;
const uint GBUFFER_ALBEDO = 1;
const uint GBUFFER_NORMAL = 2;
const uint GBUFFER_UV = 3;
const uint GBUFFER_COUNT = 4;

layout(location = 0) in vec2 texCoords;
layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D inGBuffers[GBUFFER_COUNT];

//vec4 drawAxis(vec2 center, float width)
//{
//    const vec3 X_COLOR = vec3(1.0, 0.0, 0.0);
//    const vec3 Y_COLOR = vec3(0.0, 1.0, 0.0);
//    const float LENGTH = 0.01;
//    vec2 pos = gl_FragCoord.xy;
//    if (pos.x < center.x + width && pos.x > center.x && pos.y < center.y + LENGTH && pos.y > center.y)
//        return vec4(Y_COLOR, 1.0);
//    if (pos.y < center.y + width && pos.y > center.y && pos.x < center.x + LENGTH && pos.x > center.x)
//        return vec4(X_COLOR, 1.0);
//    return texture(texSampler, texCoords);
//}

vec4 getFragColor()
{
    vec2 coord = texCoords.xy - vec2(0.5, 0.5);
    if (coord.x < 0.0)
    {
        coord.x = texCoords.x * 2.0;
        if (coord.y < 0.0)
        {
            coord.y = texCoords.y * 2.0;
            return texture(inGBuffers[GBUFFER_POS], coord);
        }
        else
        {
            coord.y *= 2.0;
            return texture(inGBuffers[GBUFFER_NORMAL], coord);
        }
    }
    else
    {
        coord.x *= 2.0;
        if (coord.y < 0.0)
        {
            coord.y = texCoords.y * 2.0;
            return texture(inGBuffers[GBUFFER_ALBEDO], coord);
        }
        else
        {
            coord.y *= 2.0;
            return texture(inGBuffers[GBUFFER_UV], coord);
        }
    }
}

void main() {
    outColor = getFragColor();
    outColor.a = 1.0;
}
