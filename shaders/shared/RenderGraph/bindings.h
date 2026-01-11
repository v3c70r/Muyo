#pragma once
#include "Camera.h"
#include "../Types.h"

#define SET_PER_VIEW 0    // Global camera info
#define SET_PER_OBJECT 1    // Object information buffer
#define SET_MATERIAL 2  // Material buffer
#define BINDING_PBR_MATERIAL 0
#define BINDING_TEXTURS 1

#if defined(__SLANG__)

#define CAMERA() [[vk::binding(0, SET_PER_VIEW)]] ConstantBuffer<PerViewData> perView;

#define OBJECT_DATA() [[vk::binding(0, SET_PER_OBJECT)]] StructuredBuffer<PerObjData> perObjData;

#define MATERIAL()                                                    \
    [[vk::binding(BINDING_PBR_MATERIAL, SET_MATERIAL)]] StructuredBuffer<PBRMaterial> materials;\
    [[vk::binding(BINDING_TEXTURS, SET_MATERIAL)]] Sampler2D<Texture2D>[] textures;
#endif
