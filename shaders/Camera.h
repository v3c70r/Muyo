
// Macro to quickly define a camera UBO at binding 0, with name uboCamera and a given SET
#define CAMERA_UBO(SET)                                        \
    layout(std140, set = SET, binding = 0) uniform UniformBufferObject \
    {                                                          \
        mat4 view;                                             \
        mat4 proj;                                             \
        mat4 projInv;                                          \
        mat4 viewInv;                                          \
        vec2 screenExtent;                                     \
        vec2 padding;                                          \
        vec3 vLT;                                              \
        float m_fNear;                                         \
        vec3 vRB;                                              \
        float m_fFar;                                          \
    }                                                          \
    uboCamera;
