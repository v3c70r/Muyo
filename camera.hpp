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
        mPos = glm::vec3(2.0);
        mRot = glm::angleAxis(0.0f, glm::vec3(1.0, 0.0, 0.0));
        mZoom = 2.0;
        update();
    }
    void startDrag(glm::vec2 screenCoord)
    {
        mPrePos = screenCoord;
    }
    void updateDrag(glm::vec2 screenCoord)
    {
        glm::vec2 from(2.0f * mPrePos / mScreenExtent - glm::vec2(1.0));
        glm::vec2 to(2.0f * screenCoord / mScreenExtent - glm::vec2(1.0));
        to.y *= -1.0;
        from.y *= -1.0;
        rotateArcball(from, to);
        update();
        mPrePos = screenCoord;
    }

    void update() override
    {
        glm::mat4 p = glm::translate(glm::mat4(1.0), -mPos);
        glm::mat4 r = glm::toMat4(mRot);
        glm::mat4 d = glm::translate(glm::mat4(1.0), glm::vec3(0.0, 0.0, mZoom));
        mView = (d * (r * p));
    }

    void rotateArcball(glm::vec2 from, glm::vec2 to)
    {
        glm::vec3 a, b;
        float az = from.x * from.x + from.y * from.y;
        float bz = to.x * to.x + to.y * to.y;

        std::cout<<"=="<<std::endl;
        std::cout<<"a: "<<glm::to_string(from)<<std::endl;
        std::cout<<"b: "<<glm::to_string(to)<<std::endl;

        // keep the controls stable by rejecting very small movements.
        if (fabsf(az - bz) < 1e-5f) return;

        if (az < 1.0f) {
            a = glm::vec3(from.x, from.y, sqrt(1.0f - az));
        }
        else {
            a = glm::vec3(from.x, from.y, 0.0f);
            a = glm::normalize(a);
        }

        if (bz < 1.0f) {
            b = glm::vec3(to.x, to.y, sqrt(1.0f - bz));
        }
        else {
            b = glm::vec3(to.x, to.y, 0.0f);
            b = glm::normalize(b);
        }

        float angle = acosf(glm::min(1.0f, glm::dot(a, b)));

        glm::vec3 axis = glm::cross(a, b);
        axis = glm::normalize(axis);

        mDirty = true;

        glm::quat delta = glm::angleAxis(angle, axis);
        mRot *= delta;
    }

protected:
    glm::vec2 mPrePos;
    glm::quat mRot;
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
