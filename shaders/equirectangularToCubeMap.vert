#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inTexCoord;

layout (location = 0) out vec3 localPos;

out gl_PerVertex {
    vec4 gl_Position;
};

void main() {
    gl_Position = ubo.proj * ubo.view * vec4(inPos, 1.0);
    localPos = inPos;
    
}
