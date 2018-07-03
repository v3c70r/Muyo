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

protected:
    glm::mat4 mProj;
    glm::mat4 mView;
    glm::mat4 mViewInv;
    glm::vec2 mScreenExtent;
};

class Arcball : public Camera {
public:
    Arcball(glm::mat4 proj, glm::mat4 view): Camera(proj, view) {}
    void startDrag(glm::vec2 screenCoord)
    {
        mPreRot = rotationFromScreen(screenCoord);
        mPrePosOnSphere = pointOnSphere(screenCoord);
        //std::cout<<"start rotation at: "<<glm::to_string(mPreRot)<<std::endl;
    }
    void updateDrag(glm::vec2 screenCoord)
    {
        glm::quat currentRot = rotationFromScreen(screenCoord);
        glm::quat diff = currentRot * glm::inverse(mPreRot);
        glm::mat4 cameraInWorld = glm::inverse(mView);
        glm::mat4 rotMat = glm::toMat4(diff);
        //mView = glm::inverse(rotMat * cameraInWorld);
        mPreRot = currentRot;

        glm::vec3 curPosOnSphere = pointOnSphere(screenCoord);
        glm::vec3 rotationAxis = glm::cross(mPrePosOnSphere, curPosOnSphere);
        float angle = glm::acos(glm::dot(mPrePosOnSphere, curPosOnSphere));
        //glm::mat4 cameraInWorld = glm::inverse(mView);
        glm::rotate(cameraInWorld, angle, rotationAxis);
        mView = glm::inverse(rotMat * cameraInWorld);


        mPrePosOnSphere = curPosOnSphere;


        //std::cout<<"new view is: "<<glm::to_string(mView)<<std::endl;
    }

protected:
    //! Convert a screen coordinate to a rotation on the arcball
    glm::quat rotationFromScreen(glm::vec2 screenCoord)
    {
        glm::vec3 pointOnSphere(screenCoord / mScreenExtent - glm::vec2(0.5f), 0.0f);
        pointOnSphere.z = glm::sqrt( 1.0f - pointOnSphere.x * pointOnSphere.x - pointOnSphere.y * pointOnSphere.y);
        const glm::vec3 Z_AXIS = glm::vec3(0.0, 0.0, 1.0);
        glm::vec3 rotationAxis = glm::cross( pointOnSphere, Z_AXIS);
        float angle = glm::acos(glm::dot(pointOnSphere, Z_AXIS));
        return glm::angleAxis(angle, glm::normalize(rotationAxis));
    }
    glm::vec3 pointOnSphere(glm::vec2 screenCoord)
    {
        glm::vec3 pointOnSphere(screenCoord / mScreenExtent - glm::vec2(0.5f), 0.0f);
        pointOnSphere.z = -glm::sqrt( 1.0f - pointOnSphere.x * pointOnSphere.x - pointOnSphere.y * pointOnSphere.y);
        return pointOnSphere;
    }
    glm::quat mPreRot;
    glm::vec3 mPrePosOnSphere;
    float zoom;
    bool isDraging;
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
