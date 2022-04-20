#include "Scene.h"

enum LightType
{
    LIGHT_TYPE_POINT = 0,
    LIGHT_TYPE_LINEAR = 1,
    LIGHT_TYPE_POLYGON = 2,
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
		glm::vec4 vPos = m_mWorldTransformation * glm::vec4(0.0, 0.0, 0.0, 1.0);
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

    virtual LightData ConstructLightData() const = 0;
private:
    uint32_t m_nLightType;
    glm::mat4 m_mWorldTransformation = glm::mat4(1.0);
    glm::vec3 m_vColor = glm::vec3(0.0);
    float m_fIntensity = 0.0f;
};

class PointLightNode : public LightSceneNode
{
public:
    PointLightNode (const glm::vec3 &vColor, const float &fIntensity, float fRadius = 0.0f)
        : LightSceneNode(LIGHT_TYPE_POINT, vColor, fIntensity), m_fRadius(fRadius)
    {
    }

    LightData ConstructLightData() const override
    {
        return {
            LIGHT_TYPE_POINT,
            GetWorldPosition(),
            GetColor(),
            GetIntensity(),
            glm::vec4(m_fRadius, 0.0f, 0.0f, 0.0f)};
    }

private:
    float m_fRadius = 0.0f;
};
