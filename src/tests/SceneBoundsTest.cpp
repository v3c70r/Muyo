// Scene bounds and the camera placement derived from them, tested without a device.
//
// The app frames its camera from the loaded scene instead of a hardcoded eye position, so two things
// have to hold: the bounds are the *world*-space bounds of the geometry (a rotation is the case an
// axis-aligned box cannot express by transforming min/max alone), and the placement that comes out of
// them actually puts the box inside the frame. Both are pure arithmetic, so they are checkable here
// rather than by looking at a screenshot of whatever scene happened to be loaded.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <optional>

#include "Camera.h"
#include "Scene.h"

namespace
{
/// A geometry node with the given local bounds, so nothing here needs the importer or an asset.
std::unique_ptr<Muyo::GeometrySceneNode> MakeGeometry(const glm::vec3 &vMin, const glm::vec3 &vMax)
{
    auto pNode = std::make_unique<Muyo::GeometrySceneNode>();
    pNode->SetAABB(Muyo::AABB{vMin, vMax});
    return pNode;
}

constexpr float kFovY = 1.3962634F;  // 80 degrees, what helloVulkan uses

/// Project a world point through the framed view; returns NDC.
glm::vec3 Project(const Muyo::FramedView &view, const glm::vec3 &point, float aspect)
{
    const glm::mat4 proj = glm::perspective(kFovY, aspect, view.fNear, view.fFar);
    const glm::mat4 viewMat = glm::lookAt(view.eye, view.center, glm::vec3(0.0F, 1.0F, 0.0F));
    const glm::vec4 clip = proj * viewMat * glm::vec4(point, 1.0F);
    return glm::vec3(clip) / clip.w;
}
}  // namespace

TEST_CASE("Scene: world bounds follow the node transforms", "[SceneBounds]")
{
    Muyo::Scene scene("bounds");
    // Two geometries: one at the origin, one translated and scaled by its own transform. The union must
    // cover both, in world space.
    Muyo::SceneNode *pFirst = scene.GetRoot();
    auto pNear = MakeGeometry(glm::vec3(-0.5F), glm::vec3(0.5F));
    pFirst->AppendChild(pNear.release());

    auto pFar = MakeGeometry(glm::vec3(-0.5F), glm::vec3(0.5F));
    Muyo::SceneNode *pFarNode = pFar.get();
    // Root -> child: translate by 10 and scale by 2, so the world box is 9..11 on every axis.
    pFarNode->SetMatrix(glm::translate(glm::mat4(1.0F), glm::vec3(10.0F)) *
                        glm::scale(glm::mat4(1.0F), glm::vec3(2.0F)));
    pFirst->AppendChild(pFar.release());

    const std::optional<Muyo::AABB> bounds = scene.GetWorldAABB();
    REQUIRE(bounds.has_value());
    CHECK(bounds->vMin.x == Catch::Approx(-0.5F).margin(1e-5F));
    CHECK(bounds->vMin.y == Catch::Approx(-0.5F).margin(1e-5F));
    CHECK(bounds->vMin.z == Catch::Approx(-0.5F).margin(1e-5F));
    CHECK(bounds->vMax.x == Catch::Approx(11.0F).margin(1e-5F));
    CHECK(bounds->vMax.y == Catch::Approx(11.0F).margin(1e-5F));
    CHECK(bounds->vMax.z == Catch::Approx(11.0F).margin(1e-5F));
}

TEST_CASE("Scene: world bounds transform every corner, not just min and max", "[SceneBounds]")
{
    Muyo::Scene scene("rotated");
    auto pGeometry = MakeGeometry(glm::vec3(-1.0F, -2.0F, -3.0F), glm::vec3(1.0F, 2.0F, 3.0F));
    Muyo::SceneNode *pNode = pGeometry.get();
    // A quarter turn about Y swaps the x and z half-extents. Transforming only min and max would leave
    // the box at the unrotated extents - the bug this test exists to catch.
    pNode->SetMatrix(glm::rotate(glm::mat4(1.0F), glm::radians(90.0F), glm::vec3(0.0F, 1.0F, 0.0F)));
    scene.GetRoot()->AppendChild(pGeometry.release());

    const std::optional<Muyo::AABB> bounds = scene.GetWorldAABB();
    REQUIRE(bounds.has_value());
    CHECK(bounds->vMin.x == Catch::Approx(-3.0F).margin(1e-5F));
    CHECK(bounds->vMax.x == Catch::Approx(3.0F).margin(1e-5F));
    CHECK(bounds->vMin.z == Catch::Approx(-1.0F).margin(1e-5F));
    CHECK(bounds->vMax.z == Catch::Approx(1.0F).margin(1e-5F));
    // The untransformed axis is untouched.
    CHECK(bounds->vMin.y == Catch::Approx(-2.0F).margin(1e-5F));
    CHECK(bounds->vMax.y == Catch::Approx(2.0F).margin(1e-5F));
}

TEST_CASE("Scene: a scene with no geometry has no bounds", "[SceneBounds]")
{
    Muyo::Scene scene("empty");
    scene.GetRoot()->AppendChild(new Muyo::SceneNode("just a transform"));
    // Not a zero-sized box at the origin: there is nothing to frame, and a caller that treats
    // nullopt as "unit box" has to say so.
    CHECK_FALSE(scene.GetWorldAABB().has_value());
}

TEST_CASE("SceneBounds: framing puts the whole box inside the frame", "[SceneBounds]")
{
    // A deliberately awkward set: tall and thin, wide and flat, and the two real scenes' shapes
    // (a ~4-unit car, a 0.7-unit chess set). The contract is the same for all of them.
    const std::array<std::pair<glm::vec3, glm::vec3>, 4> boxes = {{
        {glm::vec3(-0.35F, 0.0F, -0.35F), glm::vec3(0.35F, 0.17F, 0.35F)},  // chess set
        {glm::vec3(-2.0F, 0.0F, -1.0F), glm::vec3(2.0F, 1.4F, 1.0F)},       // car
        {glm::vec3(-0.1F, -5.0F, -0.1F), glm::vec3(0.1F, 5.0F, 0.1F)},      // tall
        {glm::vec3(-20.0F, 0.0F, -20.0F), glm::vec3(20.0F, 0.5F, 20.0F)},   // flat and wide
    }};
    for (const auto &[vMin, vMax] : boxes)
    {
        const Muyo::FramedView framed = Muyo::FrameBounds(vMin, vMax, kFovY, 16.0F / 9.0F);
        // Three aspects: square, the app's 16:9, and a portrait one where the *horizontal* field is the
        // tighter constraint. That last case is why the framing takes the aspect at all.
        for (const float aspect : {1.0F, 16.0F / 9.0F, 0.5F})
        {
            const Muyo::FramedView view = Muyo::FrameBounds(vMin, vMax, kFovY, aspect);
            for (int corner = 0; corner < 8; ++corner)
            {
                const glm::vec3 vCorner(corner & 1 ? vMax.x : vMin.x, corner & 2 ? vMax.y : vMin.y,
                                        corner & 4 ? vMax.z : vMin.z);
                const glm::vec3 ndc = Project(view, vCorner, aspect);
                INFO("corner " << corner << " of box " << vMin.y << ".." << vMax.y << " at aspect " << aspect
                               << " -> ndc " << ndc.x << ", " << ndc.y);
                CHECK(std::abs(ndc.x) <= 1.0F);
                CHECK(std::abs(ndc.y) <= 1.0F);
                CHECK(ndc.z > 0.0F);  // between the near and far planes, not clipped
                CHECK(ndc.z < 1.0F);
            }
        }
        // The camera looks down at the scene rather than along the ground, and sits outside the box.
        CHECK(framed.eye.y > framed.center.y);
        CHECK(framed.fNear > 0.0F);
        CHECK(framed.fFar > framed.fNear);
        CHECK(glm::length(framed.eye - framed.center) > glm::length(vMax - vMin) * 0.5F);
    }
}

TEST_CASE("SceneBounds: a degenerate box still yields a usable projection", "[SceneBounds]")
{
    // A single point (an empty mesh's bounds, a plane seen edge-on) must not divide by a zero radius.
    const glm::vec3 point(3.0F, 0.0F, -1.0F);
    const Muyo::FramedView view = Muyo::FrameBounds(point, point, kFovY, 16.0F / 9.0F);
    CHECK(std::isfinite(view.eye.x));
    CHECK(std::isfinite(view.eye.y));
    CHECK(std::isfinite(view.eye.z));
    CHECK(view.fNear > 0.0F);
    CHECK(view.fFar > view.fNear);
    CHECK(view.center == point);
}
