#pragma once
#include <splat-types.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

#include "RenderResourceManager.h"
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

        // Reconstruct colors from sh coefficients

        m_Colors.resize(m_NumSplats);
        if (spzData.shDegree != 0)
        {
            size_t shIndex = 0;
            size_t shStride = 0;
            switch (m_ShDegrees)
            {
                case 0:
                    shStride = 0;
                    break;
                case 1:
                    shStride = 9;
                    break;
                case 2:
                    shStride = 24;
                    break;
                case 3:
                    shStride = 45;
                    break;
            }
            for (size_t i = 0; i < m_NumSplats; i++)
            {
                glm::vec3 color{
                    0.5f + 0.282095f * spzData.sh[shIndex + 0],
                    0.5f + 0.282095f * spzData.sh[shIndex + 1],
                    0.5f + 0.282095f * spzData.sh[shIndex + 2],
                };
                shIndex += shStride;
                m_Colors[i] = color;
            }
        }
        else
        {
            std::fill(m_Colors.begin(), m_Colors.end(), glm::vec3(1.0f));
        }

        m_GpuPositionBuffer = GetRenderResourceManager()->GetStorageBuffer(m_GpuScaleBufferName + m_sName, m_Positions);
        m_GpuScaleBuffer = GetRenderResourceManager()->GetStorageBuffer(m_GpuScaleBufferName + m_sName, m_Scales);
        m_GpuRotationBuffer = GetRenderResourceManager()->GetStorageBuffer(m_GpuRotationBufferName + m_sName, m_Rotations);
        m_GpuAlphaBuffer = GetRenderResourceManager()->GetStorageBuffer(m_GpuAlphaBufferName + m_sName, m_Alphas);
        m_GpuColorBuffer = GetRenderResourceManager()->GetStorageBuffer(m_GpuColorBufferName + m_sName, m_Colors);
    }

    // Get num of splats
    int GetNumSplats() const { return m_NumSplats; }
    // Buffer getters
    const StorageBuffer<glm::vec3>* GetGpuPositionBuffer() const { return m_GpuPositionBuffer; }
    const StorageBuffer<glm::vec3>* GetGpuScaleBuffer() const { return m_GpuScaleBuffer; }
    const StorageBuffer<glm::quat>* GetGpuRotationBuffer() const { return m_GpuRotationBuffer; }
    const StorageBuffer<float>* GetGpuAlphaBuffer() const { return m_GpuAlphaBuffer; }
    const StorageBuffer<glm::vec3>* GetGpuColorBuffer() const { return m_GpuColorBuffer; }

public:
    int m_ShDegrees = 0;
    int m_NumSplats = 0;
    std::vector<glm::vec3> m_Positions;
    std::vector<glm::vec3> m_Scales;
    std::vector<glm::quat> m_Rotations;
    std::vector<float> m_Alphas;
    std::vector<glm::vec3> m_Colors;

    static constexpr char m_GpuPositionBufferName[] = "GpuPositionBuffer_";
    static constexpr char m_GpuScaleBufferName[] = "GpuScaleBuffer_";
    static constexpr char m_GpuRotationBufferName[] = "GpuRotationBuffer_";
    static constexpr char m_GpuAlphaBufferName[] = "GpuAlphaBuffer_";
    static constexpr char m_GpuColorBufferName[] = "GpuColorBuffer_";

    StorageBuffer<glm::vec3>* m_GpuPositionBuffer = nullptr;
    StorageBuffer<glm::vec3>* m_GpuScaleBuffer = nullptr;
    StorageBuffer<glm::quat>* m_GpuRotationBuffer = nullptr;
    StorageBuffer<float>* m_GpuAlphaBuffer = nullptr;
    StorageBuffer<glm::vec3>* m_GpuColorBuffer = nullptr;
};
}  // namespace Muyo
