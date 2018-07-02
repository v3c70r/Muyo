// a camera class to handle camera transformations
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>

class Camera {
public:
    Camera()
        : mProj(glm::mat4(1.0)),
          mView(glm::mat4(1.0))
    {
    }
    glm::mat4 getProjMat() const { return mProj; }
    glm::mat4 getViewMat() const { return mView; }
    virtual void update() = 0;

protected:
    glm::mat4 mProj;
    glm::mat4 mView;
    glm::mat4 mViewInv;
};

class Arcball : public Camera {
public:
    void startDragging(glm::vec2 screenCoord)
    {
    }
    void endDragging(glm::vec2 screenCoord)
    {
    }
    void update() override { glm::mat4 cameraToWorld = glm::inverse(mView); }

protected:
    //! Convert a screen coordinate to a rotation on the arcball
    static glm::quat rotationFromScreen(glm::vec2 screenCoord)
    {
        // Map screen coord to a rotation angle on the sphere surface
        
    }
    glm::quat mPreRot;
    float zoom;
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
