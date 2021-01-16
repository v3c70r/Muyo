#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;

    mat4 objectToView;
    mat4 viewToObject;
    mat4 normalObjectToView;
} ubo;

layout (location = 0) in vec3 vInPos;
layout (location = 0) out vec3 vTexCoord;

void main()
{
    vTexCoord = vInPos;
    vec4 vPos = ubo.proj * mat4(mat3(ubo.view)) * vec4(vInPos, 1.0);
    gl_Position = vPos.xyww;
}  
