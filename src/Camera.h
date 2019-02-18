// a camera class to handle camera transformations
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <iostream>
#include <glm/gtx/string_cast.hpp>

class Camera {
public:
    Camera(glm::mat4 proj, glm::mat4 view)
        : mProj(proj),
          mView(view),
          mScreenExtent(800.0f, 600.0f)
    {
    }
    glm::mat4 getProjMat() const { return mProj; }
    glm::mat4 getViewMat() const { return mView; }
    virtual void resize(const glm::vec2 &newSize)
    {
        mScreenExtent = newSize;
        glm::perspective(glm::radians(45.0f), newSize.x / newSize.y, 0.1f,
                         10.0f);
    }
    virtual void update() = 0;

protected:
    glm::vec3 mPos;
    glm::mat4 mProj;
    glm::mat4 mView;
    glm::vec2 mScreenExtent;
};

class Arcball : public Camera {
public:
    Arcball(glm::mat4 proj, glm::mat4 view) : Camera(proj, view), mDirty(false)
    {
        mZoom = 2.0;
        update();
    }
    void startDragging(glm::vec2 screenCoord)
    {
        mIsDragging = true;
        mLastPos = mCurPos = screenCoord;
    }
    void stopDragging()
    {
        mIsDragging = false;
    }

    bool isDragging() const
    {
        return mIsDragging;
    }
    void updateDrag(glm::vec2 screenCoord)
    {
        if (isDragging())
        {
            mCurPos = screenCoord;
            if (mCurPos != mLastPos)
            {
                glm::vec3 va = mGetArcballVector(mLastPos);
                glm::vec3 vb = mGetArcballVector(mCurPos);
                float angle = acos(glm::min(1.0f, glm::dot(va, vb)));
                glm::vec3 axisInCameraCoord = glm::cross(va, vb);
                glm::mat3 camera2obj = glm::inverse(mView);
            }
        }
    }

    void update() override
    {
        //glm::mat4 p = glm::translate(glm::mat4(1.0), -mPos);
        //glm::mat4 r = glm::toMat4(mRot);
        //glm::mat4 d = glm::translate(glm::mat4(1.0), glm::vec3(0.0, 0.0, mZoom));
        //mView = (d * (r * p));
    }

    glm::vec3 mGetArcballVector(glm::vec2 position)
    {
        glm::vec3 P = glm::vec3(1.0 * position.x / mScreenExtent.x * 2 - 1.0,
                                1.0 * position.y / mScreenExtent.y * 2 - 1.0, 0);
        P.y = -P.y;
        float OP_squared = P.x * P.x + P.y * P.y;
        if (OP_squared <= 1 * 1)
            P.z = sqrt(1 * 1 - OP_squared);  // Pythagoras
        else
            P = glm::normalize(P);  // nearest point
        return P;
    }

protected:

    glm::vec2 mLastPos;
    glm::vec2 mCurPos;

    glm::vec2 mScreenExtent;
    bool mIsDragging;
    bool mDirty;
    float mZoom;
};

//class FPScamera : public Camera {
//public:
//    FPScamera() : mStrafe(0.0), mForward(0.0), mEyeVector(glm::vec3(0.0)) {}
//    void pitch(float angle) { mEuler[1] += angle; }
//    void yaw(float angle) { mEuler[2] += angle; }
//    void forward(float dist) { mForward = dist; }
//    void strafe(float dist) { mStrafe = dist; }
//    void update() override
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
//        glm::vec3 forward(mView[0][2], mView[1][2],
//                          mView[2][2]);
//        glm::vec3 strafe(mView[0][0], mView[1][0],
//                         mView[2][0]);
//        mEyeVector += (-mForward * forward + mStrafe * strafe);
//        mForward = 0;
//        mStrafe = 0;
//        glm::mat4 translate = glm::mat4(1.0f);
//        translate = glm::translate(translate, -mEyeVector);
//        mView = rotate * translate;
//    }
//protected:
//    float mStrafe;
//    float mForward;
//    glm::vec3 mEyeVector;
//};
