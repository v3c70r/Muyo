#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 vTexCoords;
layout(location = 0) out vec4 vOutColor;

layout(set = 0, binding = 1) uniform samplerCube environmentMap;

void main()
{
    vOutColor = texture(environmentMap, vTexCoords) * 1.0;
    gl_FragDepth = 1.0f;
}
