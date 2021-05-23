#pragma once
#include "Scene.h"
#include <glm/glm.hpp>

class StorageBuffer;
class ParticleEmitter : public SceneNode
{
protected:
    StorageBuffer* pPositionBuffer;
    StorageBuffer* pVelocityBuffer;
    uint32_t m_nNumParticles = 0;
};
