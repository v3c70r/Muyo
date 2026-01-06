#pragma once
#include "Camera.h"

#define SET_CAMERA 0    // Global camera info
#define SET_OBJECT 1    // Object information buffer
#define SET_MATERIAL 2  // Material buffer

#if defined(SLANG)

#define CAMERA() [[vk::binding(SET_CAMERA)]] ConstantBuffer<PerViewData> perView;

#define OBJECT_DATA() [[vk::binding(SET_OBJECT)]] StructuredBuffer<PerObjData> perObjData;

#define MATERIAL()                                                    \
    [[vk::binding(0, SET_MATERIAL)]] Sampler2D<Texture2D>[] textures; \
    [[vk::binding(1, SET_MATERIAL)]] StructuredBuffer<PBRMaterial> materials;
#endif
