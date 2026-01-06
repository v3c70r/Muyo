#pragma once

#if defined(SLANG) && defined(SHADER_CODE)


// ====================
// Shader (Slang)
// ====================

#define GPU_FLOAT        float
#define GPU_UINT         uint
#define GPU_INT          int

#define GPU_FLOAT2       float2
#define GPU_FLOAT3       float3
#define GPU_FLOAT4       float4

#define GPU_MAT3         matrix<float,3,3>
#define GPU_MAT4         matrix<float,4,4>

#define GPU_DEVICE_ADDR  uint64_t

#elif !defined(SHADER_CODE)

#include <cstdint>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#define GPU_FLOAT        float
#define GPU_UINT         uint32_t
#define GPU_INT          int32_t

#define GPU_FLOAT2       glm::vec2
#define GPU_FLOAT3       glm::vec3
#define GPU_FLOAT4       glm::vec4

#define GPU_MAT3         glm::mat3
#define GPU_MAT4         glm::mat4

#define GPU_DEVICE_ADDR  VkDeviceAddress

#endif
