#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <splat-types.h>

#include "Scene.h"
#include "glm/fwd.hpp"
namespace Muyo
{

class GaussianSplatsSceneNode : public SceneNode
{
public:
    void SetData(const spz::GaussianCloud& spzData)
    {
        m_ShDegrees = spzData.shDegree;
        m_NumSplats = spzData.numPoints;

        m_Positions.resize(m_NumSplats);
        memcpy(m_Positions.data(), spzData.positions.data(), sizeof(float) * spzData.positions.size());

        m_Scales.resize(m_NumSplats);
        memcpy(m_Scales.data(), spzData.scales.data(), sizeof(float) * spzData.scales.size());

        m_Rotations.resize(m_NumSplats);
        memcpy(m_Rotations.data(), spzData.rotations.data(), sizeof(float) * spzData.rotations.size());

        m_Alphas = spzData.alphas;
        m_Colors = spzData.colors;
    }
public:
    int m_ShDegrees = 0;;
    int m_NumSplats = 0;
    std::vector<glm::vec3> m_Positions;
    std::vector<glm::vec3> m_Scales;
    std::vector<glm::quat> m_Rotations;
    std::vector<float> m_Alphas;
    std::vector<float> m_Colors;
};
}  // namespace Muyo
