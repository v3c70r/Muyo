#include "SceneImporter.h"

#include <tiny_gltf.h>

#include <cassert>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <sstream>  // std::stringstream
#include <stdexcept>

#include "Geometry.h"
#include "LightSceneNode.h"
#include "Material.h"
#include "MeshResourceManager.h"
#include "RenderResourceManager.h"
#include "SceneImporter.h"

namespace Muyo
{
namespace
{
/// glTF data is input: a malformed asset must fail in Release too, naming the file and what was wrong.
/// The asserts these replace were compiled out under NDEBUG, leaving the parse to read past a buffer
/// or to build the wrong vertex layout silently.
[[noreturn]] void ThrowGltf(const std::filesystem::path &sceneFile, const std::string &message)
{
    throw std::runtime_error("GLTFImporter: '" + sceneFile.string() + "': " + message);
}

void RequireGltf(bool condition, const std::filesystem::path &sceneFile, const std::string &message)
{
    if (!condition)
    {
        ThrowGltf(sceneFile, message);
    }
}

/// Validate the layout this importer requires for one vertex attribute: the component type, the stride
/// it hard-codes, and that the buffer covers the accessor's range. All four are asset data.
void RequireGltfAccessor(const tinygltf::Accessor &accessor, const tinygltf::BufferView &bufferView,
                         const tinygltf::Buffer &buffer, const char *attribute, const char *typeName, int expectedType,
                         size_t byteStride, const std::filesystem::path &sceneFile)
{
    RequireGltf(accessor.type == expectedType, sceneFile, std::string(attribute) + " accessor type is not " + typeName);
    RequireGltf(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT, sceneFile,
                std::string(attribute) + " accessor component type is not FLOAT");
    RequireGltf(bufferView.byteStride == byteStride || bufferView.byteStride == 0, sceneFile,
                std::string(attribute) + " bufferView stride is not " + std::to_string(byteStride));
    RequireGltf(buffer.data.size() >= bufferView.byteOffset + accessor.byteOffset + byteStride * accessor.count,
                sceneFile, std::string(attribute) + " buffer is shorter than the accessor range");
}
}  // namespace

std::vector<Scene> GLTFImporter::ImportScene(const std::string &sSceneFile)
{
    std::vector<Scene> res;
    m_sceneFile = std::filesystem::path(sSceneFile);
    if (std::filesystem::exists(sSceneFile))
    {
        tinygltf::TinyGLTF loader;
        tinygltf::Model model;
        std::string err, warn;
        bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, sSceneFile.c_str());
        // The loader's result is a file/library outcome, not an invariant: report the file and the
        // loader's own message rather than aborting on a bare assert (or proceeding empty in Release).
        if (!ret)
        {
            throw std::runtime_error("GLTFImporter: failed to load '" + sSceneFile + "'" +
                                     (err.empty() ? std::string() : ": " + err));
        }

        res.resize(model.scenes.size());
        for (size_t i = 0; i < model.scenes.size(); i++)
        {
            tinygltf::Scene &tinyScene = model.scenes[i];
            SceneNode *pSceneRoot = res[i].GetRoot();
            res[i].SetName(tinyScene.name);
            // For each root node in scene
            for (int nNodeIdx : tinyScene.nodes)
            {
                std::function<void(SceneNode **, const tinygltf::Node &)> ConstructTreeFromGLTF =
                    [&](SceneNode **ppSceneNode, const tinygltf::Node &gltfNode)
                {
                    // Copy current node. Owned until it is attached or the recursion succeeds: a
                    // malformed asset throws from ConstructGeometryNode, and with a raw `new` that
                    // exception unwound past the only pointer to the node (found by the sanitizer).
                    std::unique_ptr<SceneNode> pSceneNode;
                    static const std::string LIGHT_EXT_NAME = "KHR_lights_punctual";

                    if (gltfNode.mesh != -1)
                    {
                        const tinygltf::Mesh mesh = model.meshes[gltfNode.mesh];
                        pSceneNode = std::make_unique<GeometrySceneNode>();
                        CopyGLTFNode(*pSceneNode, gltfNode);
                        ConstructGeometryNode(static_cast<GeometrySceneNode &>(*pSceneNode), mesh, model);
                    }
                    else if (gltfNode.extensions.find(LIGHT_EXT_NAME) != gltfNode.extensions.end() &&
                             gltfNode.extensions.at(LIGHT_EXT_NAME).Has("light"))
                    {
                        // Check gltf Lights
                        // https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_lights_punctual/README.md
                        int nLightIdx = gltfNode.extensions.at(LIGHT_EXT_NAME).Get("light").Get<int>();
                        tinygltf::Light light = model.lights[nLightIdx];

                        glm::vec3 lightColor(light.color[0], light.color[1], light.color[2]);
                        if (light.type == "point")
                        {
                            // TODO(#20): derive the point-light radius from the glTF light's `range`.
                            pSceneNode = std::make_unique<PointLightNode>(lightColor, (float)light.intensity);
                        }
                        else if (light.type == "spot")
                        {
                            pSceneNode = std::make_unique<SpotLightNode>(lightColor, (float)light.intensity,
                                                                         (float)light.spot.innerConeAngle,
                                                                         (float)light.spot.outerConeAngle);
                        }
                        else if (light.type == "directional")
                        {
                            pSceneNode = std::make_unique<DirectionalLightNode>(lightColor, (float)light.intensity);
                        }
                        else
                        {
                            ThrowGltf(m_sceneFile, "unsupported light type '" + light.type + "'");
                        }

                        CopyGLTFNode(*pSceneNode, gltfNode);
                    }
                    else
                    {
                        pSceneNode = std::make_unique<SceneNode>();
                        CopyGLTFNode(*pSceneNode, gltfNode);
                    }

                    // recursively copy children
                    for (int nNodeIdx : gltfNode.children)
                    {
                        SceneNode *pChild = nullptr;
                        const tinygltf::Node &node = model.nodes[nNodeIdx];
                        ConstructTreeFromGLTF(&(pChild), node);
                        pSceneNode->AppendChild(pChild);
                    }
                    *ppSceneNode = pSceneNode.release();
                };

                SceneNode *pSceneNode = nullptr;
                tinygltf::Node gltfRootNode = model.nodes[nNodeIdx];

                ConstructTreeFromGLTF(&(pSceneNode), gltfRootNode);
                pSceneRoot->AppendChild(pSceneNode);
            }
        }
    }
    else
    {
        // A missing scene file is input, and it used to fall through this `if` into an empty scene
        // list - which surfaced far away as `SetData(nullptr)` inside the per-object upload, looking
        // like a render bug. Name the file and the working-directory requirement here instead.
        throw std::runtime_error("GLTFImporter: scene file '" + sSceneFile +
                                 "' does not exist. Tests load 'assets/...' relative to the working "
                                 "directory; run from the build directory with `assets/` linked.");
    }
    return res;
}

void GLTFImporter::CopyGLTFNode(SceneNode &sceneNode, const tinygltf::Node &gltfNode)
{
    sceneNode.SetName(gltfNode.name);

    // Use matrix if it has transformation matrix
    if (gltfNode.matrix.size() != 0)
    {
        glm::mat4 mMat(1.0);
        for (int i = 0; i < 4; i++)
        {
            for (int j = 0; j < 4; j++)
            {
                mMat[i][j] = static_cast<float>(gltfNode.matrix[i * 4 + j]);
            }
        }
        sceneNode.SetMatrix(mMat);
    }
    // Oterwise read translation, rotation and scaling to construct the matrix
    else
    {
        glm::vec3 vT(0.0f);
        if (gltfNode.translation.size() > 0)
        {
            vT.x = (float)gltfNode.translation[0];
            vT.y = (float)gltfNode.translation[1];
            vT.z = (float)gltfNode.translation[2];
        }

        glm::quat qR(1.0, 0.0, 0.0, 0.0);
        if (gltfNode.rotation.size() > 0)
        {
            qR.x = (float)gltfNode.rotation[0];
            qR.y = (float)gltfNode.rotation[1];
            qR.z = (float)gltfNode.rotation[2];
            qR.w = (float)gltfNode.rotation[3];
        }

        glm::vec3 vS(1.0f);
        if (gltfNode.scale.size() > 0)
        {
            vS.x = (float)gltfNode.scale[0];
            vS.y = (float)gltfNode.scale[1];
            vS.z = (float)gltfNode.scale[2];
        }

        glm::mat4 mMat(1.0);
        mMat = glm::translate(mMat, vT);
        glm::mat4 mR = glm::mat4_cast(qR);
        mMat = mMat * mR;
        mMat = glm::scale(mMat, vS);
        if (!Scene::IsMat4Valid(mMat))
        {
            ThrowGltf(m_sceneFile, "node '" + gltfNode.name + "' has a non-finite transform");
        }
        sceneNode.SetMatrix(mMat);
    }
}

void GLTFImporter::ConstructGeometryNode(GeometrySceneNode &geomNode, const tinygltf::Mesh &mesh,
                                         const tinygltf::Model &model)
{
    std::vector<std::unique_ptr<Submesh>> vSubmeshes;
    bool bIsMeshTransparent = false;
    bool bIsMeshEmissive = false;

    // Keep track of local bounding box
    glm::vec3 vAABBMin(std::numeric_limits<float>::max());
    glm::vec3 vAABBMax(std::numeric_limits<float>::min());

    for (const auto &primitive : mesh.primitives)
    {
        std::vector<glm::vec3> vPositions;
        std::vector<glm::vec2> vUV0s;
        std::vector<glm::vec2> vUV1s;
        std::vector<glm::vec3> vNormals;

        // vPositions
        {
            const std::string sAttribkey = "POSITION";
            const auto &accessor = model.accessors.at(primitive.attributes.at(sAttribkey));
            const auto &bufferView = model.bufferViews[accessor.bufferView];
            const auto &buffer = model.buffers[bufferView.buffer];

            // Checked before the min/max read: a non-VEC3 accessor has fewer than three components,
            // so indexing [0..2] would read past the vector.
            RequireGltfAccessor(accessor, bufferView, buffer, "POSITION", "VEC3", TINYGLTF_TYPE_VEC3, 12, m_sceneFile);
            RequireGltf(accessor.minValues.size() >= 3 && accessor.maxValues.size() >= 3, m_sceneFile,
                        "POSITION accessor has no three-component min/max");

            // Update min max value for this node
            auto minValue = accessor.minValues;
            auto maxValue = accessor.maxValues;

            vAABBMin.x = std::min(vAABBMin.x, (float)minValue[0]);
            vAABBMin.y = std::min(vAABBMin.y, (float)minValue[1]);
            vAABBMin.z = std::min(vAABBMin.z, (float)minValue[2]);

            vAABBMax.x = std::max(vAABBMax.x, (float)maxValue[0]);
            vAABBMax.y = std::max(vAABBMax.y, (float)maxValue[1]);
            vAABBMax.z = std::max(vAABBMax.z, (float)maxValue[2]);

            // Hard code the buffer stride
            size_t nByteStride = 12;

            vPositions.resize(accessor.count);
            memcpy(vPositions.data(), buffer.data.data() + bufferView.byteOffset + accessor.byteOffset,
                   accessor.count * nByteStride);
        }
        // vNormals
        {
            const std::string sAttribkey = "NORMAL";
            const auto &accessor = model.accessors.at(primitive.attributes.at(sAttribkey));
            const auto &bufferView = model.bufferViews[accessor.bufferView];
            const auto &buffer = model.buffers[bufferView.buffer];

            RequireGltfAccessor(accessor, bufferView, buffer, "NORMAL", "VEC3", TINYGLTF_TYPE_VEC3, 12, m_sceneFile);
            size_t nByteStride = 12;

            vNormals.resize(accessor.count);
            memcpy(vNormals.data(), buffer.data.data() + bufferView.byteOffset + accessor.byteOffset,
                   accessor.count * nByteStride);
        }
        // vUV0s
        {
            const std::string sAttribkey = "TEXCOORD_0";
            if (primitive.attributes.find(sAttribkey) != primitive.attributes.end())
            {
                const auto &accessor = model.accessors.at(primitive.attributes.at(sAttribkey));
                const auto &bufferView = model.bufferViews[accessor.bufferView];
                const auto &buffer = model.buffers[bufferView.buffer];

                RequireGltfAccessor(accessor, bufferView, buffer, "TEXCOORD_0", "VEC2", TINYGLTF_TYPE_VEC2, 8,
                                    m_sceneFile);
                size_t nByteStride = 8;
                vUV0s.resize(accessor.count);
                memcpy(vUV0s.data(), buffer.data.data() + bufferView.byteOffset + accessor.byteOffset,
                       accessor.count * nByteStride);
            }
            else
            {
                vUV0s.resize(vPositions.size());
                std::fill(vUV0s.begin(), vUV0s.end(), glm::vec2(0.0f, 0.0f));
            }
        }
        // vUV1s
        {
            const std::string sAttribkey = "TEXCOORD_1";
            if (primitive.attributes.find(sAttribkey) != primitive.attributes.end())
            {
                const auto &accessor = model.accessors.at(primitive.attributes.at(sAttribkey));
                const auto &bufferView = model.bufferViews[accessor.bufferView];
                const auto &buffer = model.buffers[bufferView.buffer];

                RequireGltfAccessor(accessor, bufferView, buffer, "TEXCOORD_1", "VEC2", TINYGLTF_TYPE_VEC2, 8,
                                    m_sceneFile);
                size_t nByteStride = 8;
                vUV1s.resize(accessor.count);
                memcpy(vUV1s.data(), buffer.data.data() + bufferView.byteOffset + accessor.byteOffset,
                       accessor.count * nByteStride);
            }
            else
            {
                // Use UV0 as UV1 if UV1 doesn't exist
                vUV1s = vUV0s;
            }
        }

        RequireGltf(vUV0s.size() == vPositions.size(), m_sceneFile, "TEXCOORD_0 count does not match POSITION count");
        RequireGltf(vNormals.size() == vPositions.size(), m_sceneFile, "NORMAL count does not match POSITION count");
        std::vector<Vertex> vVertices(vUV0s.size());
        for (size_t i = 0; i < vVertices.size(); i++)
        {
            Vertex &vertex = vVertices[i];
            {
                glm::vec4 pos(vPositions[i], 1.0);
                vertex.pos = pos;
                glm::vec4 normal(vNormals[i], 1.0);
                vertex.normal = normal;
                vertex.textureCoord = {vUV0s[i].x, vUV0s[i].y, vUV1s[i].x, vUV1s[i].y};
            }
        }
        // Indices
        std::vector<Index> vIndices;
        {
            const auto &accessor = model.accessors.at(primitive.indices);
            const auto &bufferView = model.bufferViews[accessor.bufferView];
            const auto &buffer = model.buffers[bufferView.buffer];
            // Convert indices to unsigned int
            vIndices.resize(accessor.count);
            if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
            {
                memcpy(vIndices.data(), buffer.data.data() + bufferView.byteOffset + accessor.byteOffset,
                       accessor.count * sizeof(Index));
            }
            else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
            {
                // Convert short index to index

                unsigned short *pData =
                    (unsigned short *)(buffer.data.data() + bufferView.byteOffset + accessor.byteOffset);
                for (size_t i = 0; i < accessor.count; i++)
                {
                    vIndices[i] = (uint32_t)pData[i];
                }
            }
            else
            {
                ThrowGltf(m_sceneFile, "unsupported index component type " + std::to_string(accessor.componentType));
            }
        }

        // Construct primitive name
        size_t nMeshIndex = GetMeshResourceManager()->AppendMesh(vVertices, vIndices);
        vSubmeshes.emplace_back(std::make_unique<Submesh>(nMeshIndex));


        //  =========Material
        //
        // Use default material if primitive has no material
        tinygltf::Material gltfMaterial;
        gltfMaterial.name = "default material";
        if (primitive.material > -1)
        {
            gltfMaterial = model.materials.at(primitive.material);
        }

        bool bIsMaterialInitialized = GetMaterialManager()->HasMaterial(gltfMaterial.name);
        Material &material = GetMaterialManager()->GetOrCreateMaterial(gltfMaterial.name);

        if (!bIsMaterialInitialized)
        {
            const std::filesystem::path sceneDir = m_sceneFile.parent_path();

            // Load material textures

            std::array<uint32_t, Material::TEX_COUNT> aUVIndices;
            std::fill(aUVIndices.begin(), aUVIndices.end(), 0);

            // Albedo
            std::string sAlbedoTexPath = "assets/Materials/white5x5.png";
            std::string sAlbedoTexName = "defaultAlbedo";
            if (gltfMaterial.pbrMetallicRoughness.baseColorTexture.index != -1)
            {
                aUVIndices[Material::TEX_ALBEDO] = gltfMaterial.pbrMetallicRoughness.baseColorTexture.texCoord;
                const tinygltf::Texture &albedoTexture =
                    model.textures[gltfMaterial.pbrMetallicRoughness.baseColorTexture.index];

                sAlbedoTexPath = (sceneDir / model.images[albedoTexture.source].uri).string();
                sAlbedoTexName = model.images[albedoTexture.source].uri;
            }

            // Metalness
            std::string sMetalnessTexPath = "assets/Materials/white5x5.png";
            std::string sMetalnessTexName = "defaultMetalness";
            if (gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index != -1)
            {
                aUVIndices[Material::TEX_METALNESS] =
                    gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.texCoord;
                const tinygltf::Texture &metalnessTextrue =
                    model.textures[gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index];
                sMetalnessTexPath = (sceneDir / model.images[metalnessTextrue.source].uri).string();

                sMetalnessTexName = model.images[metalnessTextrue.source].uri;
            }

            // Normal
            std::string sNormalTexPath = "assets/Materials/black5x5.png";
            std::string sNormalTexName = "defaultNormal";
            if (gltfMaterial.normalTexture.index != -1)
            {
                aUVIndices[Material::TEX_NORMAL] = gltfMaterial.normalTexture.texCoord;
                const tinygltf::Texture &normalTexture = model.textures[gltfMaterial.normalTexture.index];
                sNormalTexPath = (sceneDir / model.images[normalTexture.source].uri).string();
                sNormalTexName = model.images[normalTexture.source].uri;
            }

            // Roughness
            std::string sRoughnessTexPath = "assets/Materials/white5x5.png";
            std::string sRoughnessTexName = "defaultRoughness";
            if (gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index != -1)
            {
                aUVIndices[Material::TEX_ROUGHNESS] =
                    gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.texCoord;
                const tinygltf::Texture &metalnessTextrue =
                    model.textures[gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index];
                sRoughnessTexPath = (sceneDir / model.images[metalnessTextrue.source].uri).string();
                sRoughnessTexName = model.images[metalnessTextrue.source].uri;
            }

            // Occulution
            std::string sOcclusionTexPath = "assets/Materials/white5x5.png";
            std::string sOcclusionTexName = "defaultOcclusion";
            if (gltfMaterial.occlusionTexture.index != -1)
            {
                aUVIndices[Material::TEX_AO] = gltfMaterial.occlusionTexture.texCoord;
                const tinygltf::Texture &occlusionTexture = model.textures[gltfMaterial.occlusionTexture.index];
                sOcclusionTexPath = (sceneDir / model.images[occlusionTexture.source].uri).string();
                sOcclusionTexName = model.images[occlusionTexture.source].uri;
            }

            // Emissive
            std::string sEmissiveTexPath = "assets/Materials/white5x5.png";
            std::string sEmissiveTexName = "defaultEmissive";
            if (gltfMaterial.emissiveTexture.index != -1)
            {
                aUVIndices[Material::TEX_EMISSIVE] = gltfMaterial.emissiveTexture.texCoord;
                const tinygltf::Texture &emissiveTexture = model.textures[gltfMaterial.emissiveTexture.index];
                sEmissiveTexPath = (sceneDir / model.images[emissiveTexture.source].uri).string();
                sEmissiveTexName = model.images[emissiveTexture.source].uri;
            }

            const glm::vec3 vEmissiveFactors((float)gltfMaterial.emissiveFactor[0],
                                             (float)gltfMaterial.emissiveFactor[1],
                                             (float)gltfMaterial.emissiveFactor[2]);

            bIsMeshEmissive = (vEmissiveFactors != glm::vec3(0.0f));

            if (bIsMeshEmissive)
            {
                // Mesh is possibly a light source
                if (geomNode.GetName().starts_with("LIGHT_RECT"))
                {
                    geomNode.SetLightSourceType(GeometryLightSourceType::AREA);
                }
                else if (geomNode.GetName().starts_with("LIGHT_TUBE"))
                {
                    geomNode.SetLightSourceType(GeometryLightSourceType::TUBE);
                }
                else if (geomNode.GetName().starts_with("LIGHT_SPHERE"))
                {
                    geomNode.SetLightSourceType(GeometryLightSourceType::SPHERE);
                }
            }

            // PBR factors
            PBRMaterial pbrMaterial = {
                glm::vec4((float)gltfMaterial.pbrMetallicRoughness.baseColorFactor[0],
                          (float)gltfMaterial.pbrMetallicRoughness.baseColorFactor[1],
                          (float)gltfMaterial.pbrMetallicRoughness.baseColorFactor[2],
                          (float)gltfMaterial.pbrMetallicRoughness.baseColorFactor[3]),  // Base Color
                (float)gltfMaterial.pbrMetallicRoughness.roughnessFactor,                // Roughness
                (float)gltfMaterial.pbrMetallicRoughness.metallicFactor,                 // Metallic
                {aUVIndices[0], aUVIndices[1],                                           // UVs
                 aUVIndices[2], aUVIndices[3], aUVIndices[4], aUVIndices[5]},
                {vEmissiveFactors.x, vEmissiveFactors.y, vEmissiveFactors.z},
                {0, 0, 0, 0, 0, 0}};

            material.SetMaterialParameterFactors(pbrMaterial, gltfMaterial.name)
                .LoadTexture(Material::TEX_ALBEDO, sAlbedoTexPath, sAlbedoTexName)
                .LoadTexture(Material::TEX_NORMAL, sNormalTexPath, sNormalTexName)
                .LoadTexture(Material::TEX_METALNESS, sMetalnessTexPath, sMetalnessTexName)
                .LoadTexture(Material::TEX_ROUGHNESS, sRoughnessTexPath, sRoughnessTexName)
                .LoadTexture(Material::TEX_AO, sOcclusionTexPath, sOcclusionTexName)
                .LoadTexture(Material::TEX_EMISSIVE, sEmissiveTexPath, sEmissiveTexName);

            // `pbrMaterial` is fully initialised by the braced initialiser above, so this only needs to
            // hand its texture ids to the material. The `memcpy(&pbrMaterial, &pbrMaterial, ...)` that used
            // to sit here was a self-copy - a no-op that warned `-Wrestrict` in every configuration (#121).
            material.FillPbrTextureIndices(pbrMaterial.textureIds);

            material.AllocateDescriptorSet();
            RequireGltf(material.GetDescriptorSet() != VK_NULL_HANDLE, m_sceneFile,
                        "material descriptor set was not allocated");
            if (gltfMaterial.alphaMode != "OPAQUE")
            {
                RequireGltf(gltfMaterial.alphaMode == "BLEND", m_sceneFile,
                            "unsupported alpha mode '" + gltfMaterial.alphaMode + "'");
                material.SetTransparent();
            }
            if (gltfMaterial.doubleSided)
            {
            }
        }

        vSubmeshes.back()->SetMaterialIndex(material.GetMaterialIndex());
        if (material.IsTransparent())
        {
            bIsMeshTransparent = true;
        }
    }
    Geometry *pGeometry = new Geometry(vSubmeshes);
    geomNode.SetAABB({vAABBMin, vAABBMax});
    // Setup world transformation uniform buffer object
    // Use pointer address as string
    std::stringstream ss;
    ss << static_cast<void *>(pGeometry);
    auto *worldMatBuffer = GetRenderResourceManager()->GetUniformBuffer<glm::mat4>(ss.str());
    pGeometry->SetWorldMatrixUniformBuffer(worldMatBuffer);
    pGeometry->SetWorldMatrix(glm::mat4(1.0));
    GetGeometryManager()->vpGeometries.emplace_back(pGeometry);
    geomNode.SetGeometry(pGeometry);
    if (bIsMeshTransparent)
    {
        geomNode.SetTransparent();
    }
    if (bIsMeshEmissive)
    {
        geomNode.SetEmissive();
    }
}

}  // namespace Muyo
