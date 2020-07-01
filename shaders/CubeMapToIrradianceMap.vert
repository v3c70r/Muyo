#version 450 core
#extension GL_ARB_separate_shader_objects : enable

layout(set = 0, binding = 0) uniform UniformBufferObject
{
    mat4 model;
    mat4 view;
    mat4 proj;
}
ubo;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inTexCoord;

layout(location = 0) out vec3 localPos;

out gl_PerVertex { vec4 gl_Position; };

void main()
{
    localPos = inPos;

    mat4 rotView =
        mat4(mat3(ubo.view));  // remove translation from the view matrix
    vec4 clipPos = ubo.proj * rotView * vec4(localPos, 1.0);

    gl_Position = clipPos.xyww;
}
