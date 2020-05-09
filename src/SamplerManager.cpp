#include "SamplerManager.h"

static SamplerManager s_samplerManager;

SamplerManager& GetSamplerManager()
{
    return s_samplerManager;
}

