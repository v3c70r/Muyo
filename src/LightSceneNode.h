#include "Scene.h"

enum LightType
{
    LIGHT_TYPE_SPOT = 0,
    LIGHT_TYPE_COUNT
};

class LightSceneNode : public SceneNode
{
public:
    LightSceneNode(const uint32_t &nLightType, const glm::vec3 &vColor,
                   const float &fIntensity)
        : m_nLightType(nLightType), m_vColor(vColor), m_fIntensity(fIntensity)
    {
    }
    void SetWorldMatrix(const glm::mat4 mWorldMatrix)
    {
        m_mWorldTransformation = mWorldMatrix;
    }
    glm::vec3 GetWorldPosition() const
    {
        glm::vec4 vPos = glm::vec4(1.0, 1.0, 1.0, 1.0) * m_mWorldTransformation;
        vPos = vPos / vPos.w;
        return glm::vec3(vPos);
    }
    uint32_t GetLightType() const
    {
        return m_nLightType;
    }
    glm::vec3 GetColor() const
    {
        return m_vColor;
    }
    float GetIntensity() const
    {
        return m_fIntensity;
    }

private:
    uint32_t m_nLightType = LIGHT_TYPE_SPOT;
    glm::mat4 m_mWorldTransformation = glm::mat4(1.0);
    glm::vec3 m_vColor = glm::vec3(0.0);
    float m_fIntensity = 0.0f;
};
