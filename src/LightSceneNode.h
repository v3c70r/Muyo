#include "Scene.h"
#include "SharedStructures.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/geometric.hpp"

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
    glm::vec3 GetWorldDirection() const
    {
        glm::vec3 vDir = (m_mWorldTransformation * glm::vec4(0.0, 0.0, -1.0, 0.0));
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

    void SetRange(const float &fRange)
    {
        m_fRange = fRange;
    }

    float GetRange() const
    {
        return m_fRange;
    }

    void SetShadowMapIndex(const uint32_t &nShadowMapIndex)
    {
        m_nShadowMapIndex = nShadowMapIndex;
    }

    int GetShadowMapIndex() const
    {
        return m_nShadowMapIndex;
    }

    virtual glm::mat4 GetLightViewProjectionMatrix() const = 0;

    virtual LightData ConstructLightData() const = 0;

private:
    uint32_t m_nLightType;
    glm::mat4 m_mWorldTransformation = glm::mat4(1.0);
    glm::vec3 m_vColor = glm::vec3(0.0);
    float m_fPower = 0.0f;  // Energy of the light in watts
    float m_fRange = 0.0f;
    int m_nShadowMapIndex = -1;
};

class PointLightNode : public LightSceneNode
{
public:
    PointLightNode(const glm::vec3 &vColor, const float &fPower, float fRadius = 0.0f)
        : LightSceneNode(LIGHT_TYPE_POINT, vColor, fPower), m_fRadius(fRadius)
    {
    }

    glm::mat4 GetLightViewProjectionMatrix() const override
    {
        return glm::mat4(1.0);
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
            glm::vec4(m_fRadius, 0.0f, 0.0f, float(GetShadowMapIndex())),
            GetLightViewProjectionMatrix()};
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
        SetRange(0.0f);
    }

    glm::mat4 GetLightViewProjectionMatrix() const override
    {
        // Get shadow matrix of spot light
        const glm::vec3 vWorldPosition = GetWorldPosition();
        return glm::perspective(2.0f * m_fOuterConeAngle, 1.0f, 0.1f, 10.0f) *
               glm::lookAt(vWorldPosition, vWorldPosition + GetWorldDirection(), glm::vec3(0.0, 1.0, 0.0));
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
            glm::vec4(m_fInnerConeAngle, m_fOuterConeAngle, 0.0f, float(GetShadowMapIndex())),
            GetLightViewProjectionMatrix()};
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

    glm::mat4 GetLightViewProjectionMatrix() const override
    {
        // Get shadow matrix of directional light
        return glm::ortho(-100.0f, 100.0f, -100.0f, 100.0f) *
               glm::lookAt(GetWorldPosition(), GetWorldDirection(), glm::vec3(0.0, 1.0, 0.0));
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
            glm::vec4(0.0f, 0.0f, 0.0f, float(GetShadowMapIndex())),
            GetLightViewProjectionMatrix()};
    }
};

