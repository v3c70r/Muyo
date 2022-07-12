#include "Scene.h"
#include "glm/geometric.hpp"

enum LightType
{
    LIGHT_TYPE_POINT = 0,
    LIGHT_TYPE_SPOT,
    LIGHT_TYPE_DIRECTIONAL,
    LIGHT_TYPE_LINEAR,
    LIGHT_TYPE_POLYGON,
    LIGHT_TYPE_COUNT
};

struct LightData
{
    uint32_t LightType = 0;
    glm::vec3 vPosition = glm::vec3(0.0f);
    glm::vec3 vDirection = glm::vec3(1.0f); // can be vec3 if needed, use vec4 for alignment
    float fRange = 0.0f;
    glm::vec3 vColor = glm::vec3(0.0f);
    float fIntensity = 0.0f;
    glm::vec4 vMetaData = glm::vec4(0.0f);
};



class LightSceneNode : public SceneNode
{
public:
    LightSceneNode(const uint32_t &nLightType, const glm::vec3 &vColor,
                   const float &fPower)
        : m_nLightType(nLightType), m_vColor(vColor), m_fPower(fPower)
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
    glm::vec4 GetWorldDirection() const
    {
        glm::vec4 vDir = m_mWorldTransformation * glm::vec4(0.0, 0.0, -1.0, 0.0);
        return glm::normalize(vDir);
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
        return m_fPower;
    }

    float GetRange() const
    {
        return m_fRange;
    }

    virtual LightData ConstructLightData() const = 0;

private:
    uint32_t m_nLightType;
    glm::mat4 m_mWorldTransformation = glm::mat4(1.0);
    glm::vec3 m_vColor = glm::vec3(0.0);
    float m_fPower = 0.0f;  // Energy of the light in watts
    float m_fRange = 0.0f;
};

class PointLightNode : public LightSceneNode
{
public:
    PointLightNode(const glm::vec3 &vColor, const float &fPower, float fRadius = 0.0f)
        : LightSceneNode(LIGHT_TYPE_POINT, vColor, fPower), m_fRadius(fRadius)
    {
    }

    LightData ConstructLightData() const override
    {
        return {
            LIGHT_TYPE_POINT,
            GetWorldPosition(),
            GetWorldDirection(),
            GetRange(),
            GetColor(),
            GetIntensity(),
            glm::vec4(m_fRadius, 0.0f, 0.0f, 0.0f)};
    }

private:
    float m_fRadius = 0.0f;
};

class SpotLightNode : public LightSceneNode
{
public:
    SpotLightNode(const glm::vec3 &vColor, const float &fPower, float fInnerConeAngle = 0.0f, float fOuterConeAngle = 0.0f)
        : LightSceneNode(LIGHT_TYPE_SPOT, vColor, fPower), m_fInnerConeAngle(fInnerConeAngle), m_fOuterConeAngle(fOuterConeAngle)
    {
    }

    LightData ConstructLightData() const override
    {
        return {
            LIGHT_TYPE_SPOT,
            GetWorldPosition(),
            GetWorldDirection(),
            GetRange(),
            GetColor(),
            GetIntensity(),
            glm::vec4(m_fInnerConeAngle, m_fOuterConeAngle, 0.0f, 0.0f)};
    }

private:
    float m_fInnerConeAngle = 0.0f;
    float m_fOuterConeAngle = 0.0f;
};

class DirectionalLightNode : public LightSceneNode
{
public:
    DirectionalLightNode(const glm::vec3 &vColor, const float &fPower)
        : LightSceneNode(LIGHT_TYPE_DIRECTIONAL, vColor, fPower)
    {
    }
    LightData ConstructLightData() const override
    {
        return {
            LIGHT_TYPE_DIRECTIONAL,
            GetWorldPosition(),
            GetWorldDirection(),
            GetRange(),
            GetColor(),
            GetIntensity(),
            glm::vec4(0.0f, 0.0f, 0.0f, 0.0f)};
    }
};
        
