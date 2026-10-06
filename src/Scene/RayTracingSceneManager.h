#pragma once
#include <vulkan/vulkan.h>

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "Material.h"
namespace Muyo
{

struct SubmeshDescription
{
    VkDeviceAddress m_vertexBufferAddress;
    VkDeviceAddress m_indextBufferAddress;

    VkDeviceAddress m_pbrFactorAddress;
    std::array<uint32_t, Material::TEX_COUNT> m_aPbrTextureIndices;
};

class SceneNode;
struct Mesh;
class RayTracingSceneManager
{
public:
    // Build the acceleration structures for a whole scene of geometry nodes (used by the app).
    void BuildScene(const std::vector<const SceneNode*>& vpGeometries);

    // A mesh range from the shared MeshResourceManager buffers together with a world transform.
    struct MeshInstance
    {
        const Mesh* pMesh = nullptr;
        glm::mat4 transform = glm::mat4(1.0f);
    };

    // Build a BLAS per instance (from the shared MeshResourceManager vertex/index buffers) and a
    // TLAS referencing them. The TLAS is stored in the RenderResourceManager under "TLAS".
    // Returns the created TLAS.
    AccelerationStructure* BuildSceneFromMeshes(const std::vector<MeshInstance>& instances);

private:
    std::vector<SubmeshDescription> m_vSubmeshDescs;
    std::unordered_map<const SceneNode*, uint32_t> m_mSubmeshBeginIndices;

    AccelerationStructure* BuildBLAS(const std::vector<VkAccelerationStructureGeometryKHR>& vGeometries,
                                     const std::vector<uint32_t>& vPrimCounts,
                                     VkBuildAccelerationStructureFlagsKHR flags, const std::string& sName);
    AccelerationStructure* BuildBLASfromNode(const SceneNode& geometry, VkBuildAccelerationStructureFlagsKHR flags);
    AccelerationStructure* BuildTLAS(const std::vector<VkAccelerationStructureInstanceKHR>& vInstances);
    uint32_t AlignUp(uint32_t nSize, uint32_t nAlignment);
};
}  // namespace Muyo
