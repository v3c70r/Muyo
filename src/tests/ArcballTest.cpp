// The arcball camera's input contract, tested without a device.
//
// The orbit/pan behaviour is a pure transform: a drag in screen pixels becomes a view-matrix change, and
// the property that makes the drag feel right is that the point the user grabbed stays under the cursor.
// That is checkable by projecting through `GetProjMat()` / `GetViewMat()` alone, so these cases run on any
// machine and are not another "it rendered something" test (#267's lesson: a contract that only holds on
// one topology gets a device-independent test).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Camera.h"

namespace
{
constexpr float kWidth = 800.0f;
constexpr float kHeight = 600.0f;

Muyo::Arcball MakeCamera()
{
    return Muyo::Arcball(glm::perspective(glm::radians(80.0f), kWidth / kHeight, 0.1F, 100.0F),
                         glm::lookAt(glm::vec3(0.0F, 0.0F, -2.0F), glm::vec3(0.0F), glm::vec3(0.0F, 1.0F, 0.0F)), 0.1F,
                         100.0F, kWidth, kHeight);
}

/// Where a world point lands on screen, in pixels, for the camera's current view.
///
/// y grows downwards, like the presented image and like GLFW's cursor positions, so a delta fed in here
/// is the same delta the window reports.
glm::vec2 ProjectToScreen(const Muyo::Arcball& camera, const glm::vec3& point)
{
    const glm::vec4 clip = camera.GetProjMat() * camera.GetViewMat() * glm::vec4(point, 1.0F);
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    return glm::vec2((ndc.x * 0.5F + 0.5F) * kWidth, (1.0F - (ndc.y * 0.5F + 0.5F)) * kHeight);
}

/// Whether two matrices agree to within float noise, entry by entry.
bool Near(const glm::mat4& a, const glm::mat4& b)
{
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            if (glm::abs(a[column][row] - b[column][row]) > 1e-5F)
            {
                return false;
            }
        }
    }
    return true;
}

/// Press at `from`, drag to `to`; the arcball ignores the delta until it has seen the press position.
void Drag(Muyo::Arcball& camera, Muyo::Arcball::DragMode mode, glm::vec2 from, glm::vec2 to)
{
    camera.StartDragging(mode);
    camera.UpdateDrag(from);
    camera.UpdateDrag(to);
    camera.StopDragging();
}
}  // namespace

TEST_CASE("Arcball: pan maps a drag pixel-for-pixel onto the view plane", "[Arcball]")
{
    Muyo::Arcball camera = MakeCamera();
    const glm::vec3 pivot(0.0F);
    const glm::vec2 before = ProjectToScreen(camera, pivot);

    const glm::vec2 start(kWidth * 0.5F, kHeight * 0.5F);
    const glm::vec2 delta(50.0F, 30.0F);
    Drag(camera, Muyo::Arcball::DragMode::PAN, start, start + delta);

    const glm::vec2 after = ProjectToScreen(camera, pivot);
    // One dragged pixel moves the pivot by one pixel on screen: this is the assertion that the
    // world-units-per-pixel scale is right, on both axes.
    CHECK(after.x - before.x == Catch::Approx(delta.x).margin(0.5F));
    // Vertically the sign is inverted on purpose. `Pan` treats the incoming y as up, which is the
    // convention `mGetArcballVector` already uses for these same cursor coordinates (`P.y = P.y;` is the
    // no-op left where the tutorial negates). A downward drag therefore moves the view up, matching how
    // the orbit responds to the same drag - the alternative reads as reversed to anyone used to it.
    CHECK(after.y - before.y == Catch::Approx(-delta.y).margin(0.5F));
}

TEST_CASE("Arcball: pan follows the cursor at a different zoom", "[Arcball]")
{
    Muyo::Arcball camera = MakeCamera();
    camera.AddZoom(-1.0F);  // closer to the pivot, so one pixel is fewer world units

    const glm::vec3 pivot(0.0F);
    const glm::vec2 before = ProjectToScreen(camera, pivot);
    const glm::vec2 start(kWidth * 0.5F, kHeight * 0.5F);
    Drag(camera, Muyo::Arcball::DragMode::PAN, start, start + glm::vec2(-40.0F, 20.0F));

    const glm::vec2 after = ProjectToScreen(camera, pivot);
    CHECK(after.x - before.x == Catch::Approx(-40.0F).margin(0.5F));
    CHECK(after.y - before.y == Catch::Approx(-20.0F).margin(0.5F));
}

TEST_CASE("Arcball: pan does not rotate, orbit does not translate", "[Arcball]")
{
    const Muyo::Arcball::DragMode mode = GENERATE(Muyo::Arcball::DragMode::PAN, Muyo::Arcball::DragMode::ORBIT);
    Muyo::Arcball camera = MakeCamera();
    const glm::mat4 before = camera.GetViewMat();

    const glm::vec2 start(kWidth * 0.4F, kHeight * 0.6F);
    Drag(camera, mode, start, start + glm::vec2(60.0F, -25.0F));
    const glm::mat4 after = camera.GetViewMat();

    // The view matrix's rotation lives in the upper-left 3x3; its translation lives in the last column.
    glm::mat4 rotationBefore(1.0F);
    glm::mat4 rotationAfter(1.0F);
    for (int column = 0; column < 3; ++column)
    {
        rotationBefore[column] = glm::vec4(glm::vec3(before[column]), 0.0F);
        rotationAfter[column] = glm::vec4(glm::vec3(after[column]), 0.0F);
    }
    const bool rotationChanged = !Near(rotationBefore, rotationAfter);
    const bool translationChanged = glm::length(before[3] - after[3]) > 1e-5F;

    if (mode == Muyo::Arcball::DragMode::PAN)
    {
        // A pan is a pure camera-space translation: the rotation must be untouched, or the view would spin
        // while the user is only sliding the scene.
        CHECK_FALSE(rotationChanged);
        CHECK(translationChanged);
        // The z translation belongs to the zoom, so a pan may not touch it.
        CHECK(after[3].z == Catch::Approx(before[3].z).margin(1e-5F));
    }
    else
    {
        // Orbit rotates about the pivot, so the pivot stays put on screen while the camera orbits it.
        const glm::vec2 pivotScreen = ProjectToScreen(camera, glm::vec3(0.0F));
        CHECK(pivotScreen.x == Catch::Approx(kWidth * 0.5F).margin(0.5F));
        CHECK(pivotScreen.y == Catch::Approx(kHeight * 0.5F).margin(0.5F));
        CHECK(rotationChanged);
    }
}

TEST_CASE("Arcball: an idle or released drag leaves the view alone", "[Arcball]")
{
    Muyo::Arcball camera = MakeCamera();
    const glm::mat4 before = camera.GetViewMat();

    // No press: motion must not move the camera.
    camera.UpdateDrag(glm::vec2(10.0F, 10.0F));
    camera.UpdateDrag(glm::vec2(200.0F, 220.0F));
    CHECK(Near(camera.GetViewMat(), before));

    // Press, drag, release, then drag again: the released drag must not pan or orbit.
    Drag(camera, Muyo::Arcball::DragMode::PAN, glm::vec2(100.0F, 100.0F), glm::vec2(150.0F, 140.0F));
    const glm::mat4 afterPan = camera.GetViewMat();
    camera.UpdateDrag(glm::vec2(400.0F, 400.0F));
    CHECK(Near(camera.GetViewMat(), afterPan));
    CHECK(camera.GetDragMode() == Muyo::Arcball::DragMode::NONE);
}
