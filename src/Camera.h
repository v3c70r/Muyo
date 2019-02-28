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
    virtual glm::mat4 getViewMat() const { return mView; }
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
    Arcball(glm::mat4 proj, glm::mat4 view)
        : Camera(proj, view), 
        mLastPos(glm::vec2(0.0)),
        mCurPos(glm::vec2(0.0)),
        mArcballRotate(glm::mat4(1.0)),
        mIsDragging(false), 
        mIsFirstPos(false),
        mZoom(1.0)
        
    {
        mZoom = 2.0;
        update();
    }
    void startDragging()
    {
        mIsDragging = true;
        mIsFirstPos = true;
    }
    void stopDragging()
    {
        mIsDragging = false;
        mIsFirstPos = true;
    }

    bool isDragging() const
    {
        return mIsDragging;
    }
    void updateDrag(glm::vec2 screenCoord)
    {
        if (isDragging() && mIsFirstPos)
        {
            // handle first dragging point
            mLastPos = mCurPos = screenCoord;
            mIsFirstPos = false;
        }
        if (isDragging())
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
                glm::mat4 camera2World = glm::inverse(mView);

                glm::vec4 axisInWorld = glm::normalize(camera2World * axisInCameraCoord);
                glm::mat4 world2obj = glm::inverse(mArcballRotate);
                glm::vec4 axisInObj = world2obj * axisInWorld;


                mArcballRotate = glm::rotate(mArcballRotate, angle, glm::vec3(axisInObj));

                mLastPos = mCurPos;
            }
        }
    }

    glm::mat4 getViewMat() const override
    {
        return mView * mArcballRotate;
    }

    void update() override
    {
    }

    glm::vec3 mGetArcballVector(glm::vec2 position)
    {
        // screen -> NDC
        glm::vec3 P = glm::vec3(1.0 * position.x / mScreenExtent.x * 2 - 1.0,
                                1.0 * position.y / mScreenExtent.y * 2 - 1.0, 0);
        P.y = P.y;
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
    glm::mat4 mArcballRotate;

    bool mIsDragging;
    bool mIsFirstPos;
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
