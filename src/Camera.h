#pragma once
// a camera class to handle camera transformations
#include "glm/ext/quaternion_geometric.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/gtx/transform.hpp>
#include <iostream>

#include "UniformBuffer.h"
#include "glm/ext/quaternion_common.hpp"
#include "glm/matrix.hpp"
namespace Muyo
{

// Make sure this structure is consistant with the one in Camera.h in shader
struct PerViewData
{
    glm::mat4 mView = glm::mat4(1.0);
    glm::mat4 mProj = glm::mat4(1.0);
    glm::mat4 mProjInv = glm::mat4(1.0);
    glm::mat4 mViewInv = glm::mat4(1.0);
    glm::vec2 vScreenExtent = glm::vec2(0.0);
    glm::vec2 vPadding = glm::vec2(0.0);
    // Left top ray and bottom right ray directions
    glm::vec3 vLT = glm::vec3(0.0);
    float m_fNear = 0.0f;
    glm::vec3 vRB = glm::vec3(0.0);
    float m_fFar = 1.0f;

    uint32_t uFrameId = 0;
    float fAperture = 3.0f;
    float fFocalDistance = 10.0f;
    float fLeftSplitScreenRatio = 0.5f;

    // World-space frustum planes (left, right, bottom, top, near, far), packed as
    // (normal.xyz, distance). Used by the GPU frustum culling pass. Plane normals point
    // inward: a point is inside when dot(normal, p) + distance >= 0.
    glm::vec4 vFrustumPlanes[6] = {glm::vec4(0.0f)};
};

// Extract the 6 world-space frustum planes from a view-projection matrix
// (Gribb-Hartmann). Matches glm::perspective's default OpenGL depth range [-1, 1].
// Plane normals point inward: a point is inside when dot(normal, p) + distance >= 0.
inline void ExtractFrustumPlanes(const glm::mat4 &mViewProj, glm::vec4 outPlanes[6])
{
    const glm::vec4 row0 = glm::row(mViewProj, 0);
    const glm::vec4 row1 = glm::row(mViewProj, 1);
    const glm::vec4 row2 = glm::row(mViewProj, 2);
    const glm::vec4 row3 = glm::row(mViewProj, 3);

    outPlanes[0] = row3 + row0;  // left:   clip.x >= -clip.w
    outPlanes[1] = row3 - row0;  // right:  clip.x <=  clip.w
    outPlanes[2] = row3 + row1;  // bottom: clip.y >= -clip.w
    outPlanes[3] = row3 - row1;  // top:    clip.y <=  clip.w
    outPlanes[4] = row3 + row2;  // near:   clip.z >= -clip.w (OpenGL depth range)
    outPlanes[5] = row3 - row2;  // far:    clip.z <=  clip.w

    for (int i = 0; i < 6; ++i)
    {
        const float len = glm::length(glm::vec3(outPlanes[i]));
        if (len > 0.0f) outPlanes[i] /= len;
    }
}

class Camera
{
public:
    virtual ~Camera() = default;
    Camera(glm::mat4 mProj, glm::mat4 mView, float fNear, float fFar, float width, float height)
    {
        m_perViewData.mProj = mProj;
        m_perViewData.mView = mView;
        m_perViewData.mProjInv = glm::inverse(mProj);
        m_perViewData.mViewInv = glm::inverse(mView);
        m_perViewData.vScreenExtent = {width, height};

        // Left top
        glm::vec4 vScreenRay = m_perViewData.mProjInv * glm::vec4(-1.0, -1.0, 0.0, 1.0);
        m_perViewData.vLT = vScreenRay / vScreenRay[3];
        m_perViewData.vLT = glm::normalize(m_perViewData.vLT);

        // Bottom rgith
        vScreenRay = m_perViewData.mProjInv * glm::vec4(1.0, 1.0, 0.0, 1.0);
        m_perViewData.vRB = vScreenRay / vScreenRay[3];
        m_perViewData.vRB = glm::normalize(m_perViewData.vRB);

        m_perViewData.m_fNear = fNear;
        m_perViewData.m_fFar = fFar;
    }
    glm::mat4 GetProjMat() const { return m_perViewData.mProj; }
    virtual void SetProjMat(const glm::mat4 mProj)
    {
        m_perViewData.mProj = mProj;
        m_perViewData.mProjInv = glm::inverse(mProj);

        glm::vec4 vScreenRay = m_perViewData.mProjInv * glm::vec4(-1.0, -1.0, 0.0, 1.0);
        m_perViewData.vLT = vScreenRay / vScreenRay[3];
        m_perViewData.vLT = glm::normalize(m_perViewData.vLT);

        vScreenRay = m_perViewData.mProjInv * glm::vec4(1.0, 1.0, 0.0, 1.0);
        m_perViewData.vRB = vScreenRay / vScreenRay[3];
        m_perViewData.vRB = glm::normalize(m_perViewData.vRB);
    }
    virtual glm::mat4 GetViewMat() const { return m_perViewData.mView; }
    virtual void Resize(const glm::vec2 &newSize)
    {
        m_perViewData.vScreenExtent = newSize;
        glm::perspective(glm::radians(45.0f), newSize.x / newSize.y, 0.1f, 10.0f);
    }
    virtual void Update() = 0;

    void SetAperture(float fAperture) { m_perViewData.fAperture = fAperture; }

    void SetFocalDistance(float fDistance) { m_perViewData.fFocalDistance = fDistance; }

    void SetFrameId(uint32_t uFrameId) { m_perViewData.uFrameId = uFrameId; }
    void SetLeftSplitScreenRatio(float ratio) { m_perViewData.fLeftSplitScreenRatio = ratio; }
    float GetLeftSplitScreenRatio() const { return m_perViewData.fLeftSplitScreenRatio; }

    bool IsTransforationUpdated() const { return m_bIsTransformationUpdated; }

protected:
    PerViewData m_perViewData;
    mutable bool m_bIsTransformationUpdated = false;
};

/// A camera placement that frames a world-space box.
struct FramedView
{
    glm::vec3 eye = glm::vec3(0.0f);
    glm::vec3 center = glm::vec3(0.0f);
    float fNear = 0.1f;
    float fFar = 100.0f;
};

/// Place a camera so that `[vMin, vMax]` fills the frame with a margin.
///
/// The viewing direction is fixed - from -Z, 20 degrees above the horizon, looking down - rather than
/// derived from the box: a scene has no "natural" direction, and a ground-plane scene seen from a
/// horizontal camera is edge-on, which is how a chessboard ends up looking like a line.
///
/// Framing by the box's bounding *sphere* keeps the fit independent of orientation, and the distance is
/// set by the *narrower* of the two half-angles: a tall frame is limited horizontally, so fitting only
/// the vertical field of view would push a wide box's sides out of a portrait window.
///
/// @param vMin,vMax Opposite corners of the box in world space.
/// @param fFovYRadians Vertical field of view, radians.
/// @param fAspect Viewport width / height; the horizontal field is derived from it.
/// @return The eye point, look-at centre, and the near/far planes that bracket the box.
inline FramedView FrameBounds(const glm::vec3 &vMin, const glm::vec3 &vMax, float fFovYRadians, float fAspect)
{
    FramedView view;
    view.center = (vMin + vMax) * 0.5f;
    // A box with one flat side (a plane's wall, a single triangle) still needs a non-zero sphere.
    const float radius = glm::max(glm::length(vMax - vMin) * 0.5f, 1e-4f);
    const float halfFovY = glm::max(fFovYRadians * 0.5f, 1e-3f);
    const float halfFovX = glm::atan(glm::tan(halfFovY) * glm::max(fAspect, 1e-3f));
    const float halfFov = glm::min(halfFovY, halfFovX);
    // Distance at which a sphere of `radius` exactly fills that field of view, plus a margin so the
    // silhouette is not flush with the image edge.
    const float distance = radius / glm::sin(halfFov) * 1.15f;
    const float elevation = glm::radians(20.0f);
    view.eye = view.center + glm::vec3(0.0f, glm::sin(elevation), -glm::cos(elevation)) * distance;
    // Near is a fraction of the framing distance and far comfortably clears the box: the hardcoded
    // 0.1/100 pair put a 0.7-unit chess set at the wrong end of the depth range and clipped anything
    // larger than a car.
    view.fNear = glm::max(distance * 0.01f, 1e-4f);
    view.fFar = distance + radius * 8.0f;
    return view;
}

class Arcball : public Camera
{
public:
    /// What a mouse drag does. The app maps a button to a mode; the camera keeps one drag at a time.
    enum class DragMode
    {
        NONE,   ///< No button is down.
        ORBIT,  ///< Rotate about the pivot (left button).
        PAN,    ///< Translate in the view plane, the scene following the cursor (middle button).
    };

    Arcball(glm::mat4 proj, glm::mat4 view, float fNear, float fFar, float width, float height)
        : Camera(proj, view, fNear, fFar, width, height)
        , mLastPos(glm::vec2(0.0))
        , mCurPos(glm::vec2(0.0))
        , mArcballRotate(glm::mat4(1.0))
        , m_dragMode(DragMode::NONE)
        , mIsFirstPos(false)
        , mZoom(0.0)

    {
        // Qualified: this calls Arcball's own override, which is what the virtual call resolved to during
        // construction anyway; naming it makes that explicit (clang-analyzer VirtualCall).
        Arcball::Update();
    }
    /// @param mode What the drag does; the default keeps the original left-button orbit behaviour.
    void StartDragging(DragMode mode = DragMode::ORBIT)
    {
        m_dragMode = mode;
        mIsFirstPos = true;
    }
    void StopDragging()
    {
        m_dragMode = DragMode::NONE;
        mIsFirstPos = true;
    }

    DragMode GetDragMode() const { return m_dragMode; }

    void AddZoom(float fZoom) { mZoom += fZoom; }

    bool IsDragging() const
    {
        // Hack: Set transformation updated when dragging
        if (m_dragMode != DragMode::NONE)
        {
            m_bIsTransformationUpdated = true;
        }
        else
        {
            m_bIsTransformationUpdated = false;
        }
        return m_dragMode != DragMode::NONE;
    }
    void UpdateDrag(glm::vec2 screenCoord)
    {
        if (IsDragging() && mIsFirstPos)
        {
            // handle first dragging point
            mLastPos = mCurPos = screenCoord;
            mIsFirstPos = false;
        }
        if (!IsDragging())
        {
            return;
        }
        mCurPos = screenCoord;
        if (mCurPos == mLastPos)
        {
            return;
        }
        if (m_dragMode == DragMode::ORBIT)
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
        }
        else
        {
            Pan(mCurPos - mLastPos);
        }
        mLastPos = mCurPos;
    }

    /// Pan by a drag measured in screen pixels.
    ///
    /// Horizontal drags move the content with the cursor. Vertical drags move it the other way, because
    /// the y delta is taken in the arcball's y-up convention (see `mGetArcballVector`) rather than
    /// GLFW's y-down one: a downward drag moves the view up, so the camera follows the cursor. That is
    /// the direction the orbit responds to the same drag, which is why it is the one shipped.
    ///
    /// The offset is accumulated in camera space and applied by `GetViewMat`, so it composes with the
    /// orbit rotation and with `AddZoom` instead of replacing them.
    void Pan(glm::vec2 screenDelta)
    {
        const float unitsPerPixel = GetWorldUnitsPerPixel();
        // Both axes are taken as screen-space deltas, matching the y convention `mGetArcballVector` already
        // uses for these same cursor coordinates: it maps y straight to NDC instead of negating it, so a
        // positive y delta is up for the camera. Panning the same way keeps the two drags consistent; using
        // GLFW's y-down reading instead would invert the pan against every orbit the user has done.
        m_panOffset += screenDelta * unitsPerPixel;
    }

    /// @return World units covered by one screen pixel in the pivot plane.
    ///
    /// This is what makes a pan follow the cursor: the pivot (the world origin, which is what the arcball
    /// orbits) sits at a known depth, and the projection says how much world space one pixel spans there.
    float GetWorldUnitsPerPixel() const
    {
        const float extentY = m_perViewData.vScreenExtent.y;
        if (extentY <= 0.0f || m_perViewData.mProj[1][1] == 0.0f)
        {
            return 0.0f;
        }
        // Pivot in camera space under the current zoom. Pan is not part of this: it does not change depth.
        const glm::mat4 viewWithZoom = glm::translate(m_perViewData.mView, glm::vec3(0.0f, 0.0f, mZoom));
        const glm::vec4 pivot = viewWithZoom * mArcballRotate * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        const float depth = glm::max(-pivot.z, 1e-4f);
        // P[1][1] == 1 / tan(fovY / 2), so the visible half-height at `depth` is depth * P[1][1]^-1.
        const float tanHalfFovY = 1.0f / m_perViewData.mProj[1][1];
        return 2.0f * depth * tanHalfFovY / extentY;
    }

    glm::mat4 GetViewMat() const override
    {
        // The pan is leftmost so it acts in camera space: it keeps following the cursor after an orbit,
        // which a world-space offset would not. The zoom stays where it was (world space, unchanged).
        const glm::mat4 viewWithZoom = glm::translate(m_perViewData.mView, glm::vec3(0.0f, 0.0f, mZoom));
        return glm::translate(glm::mat4(1.0f), glm::vec3(m_panOffset, 0.0f)) * viewWithZoom * mArcballRotate;
    }

    void Update() override {}

protected:
    glm::vec2 mLastPos;
    glm::vec2 mCurPos;
    glm::mat4 mArcballRotate;

    DragMode m_dragMode;
    bool mIsFirstPos;
    float mZoom;
    /// Accumulated pan, in camera space; `GetViewMat` applies it with the zoom.
    glm::vec2 m_panOffset = glm::vec2(0.0f);

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

// class FPScamera : public Camera {
// public:
//     FPScamera() : mStrafe(0.0), mForward(0.0), mEyeVector(glm::vec3(0.0)) {}
//     void pitch(float angle) { mEuler[1] += angle; }
//     void yaw(float angle) { mEuler[2] += angle; }
//     void forward(float dist) { mForward = dist; }
//     void strafe(float dist) { mStrafe = dist; }
//     void Update() override
//     {
//         glm::mat4 matRoll = glm::mat4(1.0f);   // identity matrix;
//         glm::mat4 matPitch = glm::mat4(1.0f);  // identity matrix
//         glm::mat4 matYaw = glm::mat4(1.0f);    // identity matrix
//         // roll, pitch and yaw are used to store our angles in our class
//         matRoll = glm::rotate(matRoll, mEuler[0], glm::vec3(0.0f, 0.0f, 1.0f));
//         matPitch = glm::rotate(matPitch, mEuler[1], glm::vec3(1.0f, 0.0f, 0.0f));
//         matYaw = glm::rotate(matYaw, mEuler[2], glm::vec3(0.0f, 1.0f, 0.0f));
//         // order matters
//         glm::mat4 rotate = matRoll * matPitch * matYaw;
//         glm::vec3 forward(m_perViewData.mView[0][2], m_perViewData.mView[1][2],
//                           m_perViewData.mView[2][2]);
//         glm::vec3 strafe(m_perViewData.mView[0][0], m_perViewData.mView[1][0],
//                          m_perViewData.mView[2][0]);
//         mEyeVector += (-mForward * forward + mStrafe * strafe);
//         mForward = 0;
//         mStrafe = 0;
//         glm::mat4 translate = glm::mat4(1.0f);
//         translate = glm::translate(translate, -mEyeVector);
//         m_perViewData.mView = rotate * translate;
//     }
// protected:
//     float mStrafe;
//     float mForward;
//     glm::vec3 mEyeVector;
// };
}  // namespace Muyo
