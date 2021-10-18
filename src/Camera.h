#pragma once
// a camera class to handle camera transformations
#include "glm/ext/quaternion_geometric.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/gtx/transform.hpp>
#include <iostream>

#include "UniformBuffer.h"
#include "glm/ext/quaternion_common.hpp"
#include "glm/matrix.hpp"

// Make sure this structure is consistant with the one in Camera.h in shader
struct PerViewData
{
    glm::mat4 mView = glm::mat4(1.0);
    glm::mat4 mProj = glm::mat4(1.0);
    glm::mat4 mProjInv = glm::mat4(1.0);
    glm::mat4 mViewInv = glm::mat4(1.0);
    glm::vec2 vScreenExtent = glm::vec2(0.0);
    glm::vec2 padding = glm::vec2(0.0);
    // Left top ray and bottom right ray directions
    glm::vec3 vLT = glm::vec3(0.0);
    float m_fNear = 0.0f;
    glm::vec3 vRB = glm::vec3(0.0);
    float m_fFar = 1.0f;
};

class Camera
{
public:
    Camera(glm::mat4 mProj, glm::mat4 mView, float fNear, float fFar)
    {
        m_perViewData.mProj = mProj;
        m_perViewData.mView = mView;
        m_perViewData.mProjInv = glm::inverse(mProj);
        m_perViewData.mViewInv = glm::inverse(mView);
        m_perViewData.vScreenExtent = {800.0f, 600.0f};

        // Left top
        glm::vec4 vScreenRay = m_perViewData.mProjInv * glm::vec4(-1.0, -1.0, 0.0, 1.0);
        m_perViewData.vLT = vScreenRay / vScreenRay[3];
        m_perViewData.vLT = glm::normalize(m_perViewData.vLT);

        // Bottom rgith
        vScreenRay = vScreenRay = m_perViewData.mProjInv * glm::vec4(1.0, 1.0, 0.0, 1.0);
        m_perViewData.vRB = vScreenRay / vScreenRay[3];
        m_perViewData.vRB = glm::normalize(m_perViewData.vRB);

        m_perViewData.m_fNear = fNear;
        m_perViewData.m_fFar = fFar;
    }
    glm::mat4 GetProjMat() const { return m_perViewData.mProj; }
    virtual void SetProjMat( const glm::mat4 mProj)
    {
        m_perViewData.mProj = mProj;
        m_perViewData.mProjInv = glm::inverse(mProj);

        glm::vec4 vScreenRay = m_perViewData.mProjInv * glm::vec4(-1.0, -1.0, 0.0, 1.0);
        m_perViewData.vLT = vScreenRay / vScreenRay[3];
        m_perViewData.vLT = glm::normalize(m_perViewData.vLT);

        vScreenRay = vScreenRay = m_perViewData.mProjInv * glm::vec4(1.0, 1.0, 0.0, 1.0);
        m_perViewData.vRB = vScreenRay / vScreenRay[3];
        m_perViewData.vRB = glm::normalize(m_perViewData.vRB);
    }
    virtual glm::mat4 GetViewMat() const { return m_perViewData.mView; }
    virtual void Resize(const glm::vec2 &newSize)
    {
        m_perViewData.vScreenExtent = newSize;
        glm::perspective(glm::radians(45.0f), newSize.x / newSize.y, 0.1f,
                         10.0f);
    }
    virtual void Update() = 0;
    virtual void UpdatePerViewDataUBO(UniformBuffer<PerViewData> *ubo)
    {
        ubo->setData(m_perViewData);
    };

protected:
    PerViewData m_perViewData;
};

class Arcball : public Camera
{
public:
    Arcball(glm::mat4 proj, glm::mat4 view, float fNear, float fFar)
        : Camera(proj, view, fNear, fFar),
          mLastPos(glm::vec2(0.0)),
          mCurPos(glm::vec2(0.0)),
          mArcballRotate(glm::mat4(1.0)),
          mIsDragging(false),
          mIsFirstPos(false),
          mZoom(0.0)

    {
        Update();
    }
    void StartDragging()
    {
        mIsDragging = true;
        mIsFirstPos = true;
    }
    void StopDragging()
    {
        mIsDragging = false;
        mIsFirstPos = true;
    }

    void AddZoom(float fZoom)
    {
        mZoom += fZoom;
    }

    bool IsDragging() const
    {
        return mIsDragging;
    }
    void UpdateDrag(glm::vec2 screenCoord)
    {
        if (IsDragging() && mIsFirstPos)
        {
            // handle first dragging point
            mLastPos = mCurPos = screenCoord;
            mIsFirstPos = false;
        }
        if (IsDragging())
        {
            mCurPos = screenCoord;
            if (mCurPos != mLastPos)
            {
                // https://en.wikibooks.org/wiki/OpenGL_Programming/Modern_OpenGL_Tutorial_Arcball
                glm::vec3 va = mGetArcballVector(mLastPos);
                glm::vec3 vb = mGetArcballVector(mCurPos);
                float angle = acos(glm::min(1.0f, glm::dot(va, vb)));
                glm::vec4 axisInCameraCoord(glm::cross(va, vb), 0.0);
                axisInCameraCoord = glm::normalize(axisInCameraCoord);
                glm::mat4 camera2World = glm::inverse(m_perViewData.mView);

                glm::vec4 axisInWorld = glm::normalize(camera2World * axisInCameraCoord);
                glm::mat4 world2obj = glm::inverse(mArcballRotate);
                glm::vec4 axisInObj = world2obj * axisInWorld;

                mArcballRotate = glm::rotate(mArcballRotate, angle, glm::vec3(axisInObj));

                mLastPos = mCurPos;
            }
        }
    }

    glm::mat4 GetViewMat() const override
    {
        glm::mat4 mViewMatrix =
            glm::translate(m_perViewData.mView, glm::vec3(0.0, 0.0, mZoom));
        return mViewMatrix * mArcballRotate;
    }

    void Update() override
    {
    }

    virtual void UpdatePerViewDataUBO(UniformBuffer<PerViewData> *ubo) override
    {
        PerViewData perView = m_perViewData;
        perView.mView = GetViewMat();
        perView.mViewInv = glm::inverse(perView.mView);
        ubo->setData(perView);
    }

protected:
    glm::vec2 mLastPos;
    glm::vec2 mCurPos;
    glm::mat4 mArcballRotate;

    bool mIsDragging;
    bool mIsFirstPos;
    float mZoom;

private:
    glm::vec3 mGetArcballVector(glm::vec2 position)
    {
        // screen -> NDC
        glm::vec3 P = glm::vec3(1.0 * position.x / m_perViewData.vScreenExtent.x * 2 - 1.0,
                                1.0 * position.y / m_perViewData.vScreenExtent.y * 2 - 1.0, 0);
        P.y = P.y;
        float OP_squared = P.x * P.x + P.y * P.y;
        if (OP_squared <= 1 * 1)
            P.z = sqrt(1 * 1 - OP_squared);  // Pythagoras
        else
            P = glm::normalize(P);  // nearest point
        return P;
    }

};

//class FPScamera : public Camera {
//public:
//    FPScamera() : mStrafe(0.0), mForward(0.0), mEyeVector(glm::vec3(0.0)) {}
//    void pitch(float angle) { mEuler[1] += angle; }
//    void yaw(float angle) { mEuler[2] += angle; }
//    void forward(float dist) { mForward = dist; }
//    void strafe(float dist) { mStrafe = dist; }
//    void Update() override
//    {
//        glm::mat4 matRoll = glm::mat4(1.0f);   // identity matrix;
//        glm::mat4 matPitch = glm::mat4(1.0f);  // identity matrix
//        glm::mat4 matYaw = glm::mat4(1.0f);    // identity matrix
//        // roll, pitch and yaw are used to store our angles in our class
//        matRoll = glm::rotate(matRoll, mEuler[0], glm::vec3(0.0f, 0.0f, 1.0f));
//        matPitch = glm::rotate(matPitch, mEuler[1], glm::vec3(1.0f, 0.0f, 0.0f));
//        matYaw = glm::rotate(matYaw, mEuler[2], glm::vec3(0.0f, 1.0f, 0.0f));
//        // order matters
//        glm::mat4 rotate = matRoll * matPitch * matYaw;
//        glm::vec3 forward(m_perViewData.mView[0][2], m_perViewData.mView[1][2],
//                          m_perViewData.mView[2][2]);
//        glm::vec3 strafe(m_perViewData.mView[0][0], m_perViewData.mView[1][0],
//                         m_perViewData.mView[2][0]);
//        mEyeVector += (-mForward * forward + mStrafe * strafe);
//        mForward = 0;
//        mStrafe = 0;
//        glm::mat4 translate = glm::mat4(1.0f);
//        translate = glm::translate(translate, -mEyeVector);
//        m_perViewData.mView = rotate * translate;
//    }
//protected:
//    float mStrafe;
//    float mForward;
//    glm::vec3 mEyeVector;
//};
