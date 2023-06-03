#include "SamplerManager.h"

namespace Muyo
{

static SamplerManager s_samplerManager;

SamplerManager* GetSamplerManager()
{
    return &s_samplerManager;
}

}  // namespace Muyo
