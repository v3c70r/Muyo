#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) out float fDepth;
void main()
{
    fDepth = gl_FragCoord.z;
}
