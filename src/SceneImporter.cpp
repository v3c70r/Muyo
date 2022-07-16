#include "SceneImporter.h"

#include <tiny_gltf.h>

#include <cassert>
#include <functional>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <sstream> // std::stringstream

#include "Geometry.h"
#include "Material.h"
#include "SceneImporter.h"
#include "RenderResourceManager.h"
#include "LightSceneNode.h"

std::vector<Scene> GLTFImporter::ImportScene(const std::string &sSceneFile)
{
    std::vector<Scene> res;
    m_sceneFile = std::filesystem::path(sSceneFile);
    if (std::filesystem::exists(sSceneFile))
    {
        tinygltf::TinyGLTF loader;
        tinygltf::Model model;
        std::string err, warn;
        bool ret =
            loader.LoadASCIIFromFile(&model, &err, &warn, sSceneFile.c_str());
        assert(ret);

        res.resize(model.scenes.size());
        for (size_t i = 0; i < model.scenes.size(); i++)
        {
            tinygltf::Scene &tinyScene = model.scenes[i];
            SceneNode *pSceneRoot = res[i].GetRoot();
            res[i].SetName(tinyScene.name);
            // For each root node in scene
            for (int nNodeIdx : tinyScene.nodes)
            {
                std::function<void(SceneNode **, const tinygltf::Node &)>
                    ConstructTreeFromGLTF = [&](SceneNode **ppSceneNode, const tinygltf::Node &gltfNode) {
                        // Copy current node
                        SceneNode *pSceneNode = nullptr;
                        static const std::string LIGHT_EXT_NAME = "KHR_lights_punctual";

                        if (gltfNode.mesh != -1)
                        {
                            const tinygltf::Mesh mesh = model.meshes[gltfNode.mesh];
                            pSceneNode = new GeometrySceneNode;
                            CopyGLTFNode(*pSceneNode, gltfNode);
                            ConstructGeometryNode(static_cast<GeometrySceneNode &>(*pSceneNode), mesh, model);
                        }
                        else if (gltfNode.extensions.find(LIGHT_EXT_NAME) != gltfNode.extensions.end() && gltfNode.extensions.at(LIGHT_EXT_NAME).Has("light"))
                        {
                            // Check gltf Lights
                            // https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_lights_punctual/README.md
                            int nLightIdx = gltfNode.extensions.at(LIGHT_EXT_NAME).Get("light").Get<int>();
                            tinygltf::Light light = model.lights[nLightIdx];

                            glm::vec3 lightColor(light.color[0], light.color[1], light.color[2]);
                            if (light.type == "point")
                            {
                                // TODO: Figure out how to get radius from gltf
                                pSceneNode = new PointLightNode(lightColor, (float)light.intensity);
                            }
                            else if (light.type == "spot")
                            {
                                pSceneNode = new SpotLightNode(lightColor, (float)light.intensity, (float)light.spot.innerConeAngle, (float)light.spot.outerConeAngle);
                            }
                            else if (light.type == "directional")
                            {
                                pSceneNode = new DirectionalLightNode(lightColor, (float)light.intensity);
                            }
                            else
                            {
                                assert(false);
                            }

                            CopyGLTFNode(*pSceneNode, gltfNode);
                        }
                        else
                        {
                            pSceneNode = new SceneNode;
                            CopyGLTFNode(*pSceneNode, gltfNode);
                        }
                        assert(pSceneNode != nullptr);

                        // recursively copy children
                        for (int nNodeIdx : gltfNode.children)
                        {
                            SceneNode* pChild = nullptr;
                            const tinygltf::Node &node = model.nodes[nNodeIdx];
                            ConstructTreeFromGLTF(&(pChild), node);
                            pSceneNode->AppendChild(pChild);
                        }
                        *ppSceneNode = pSceneNode;
                    };

                SceneNode *pSceneNode = nullptr;
                tinygltf::Node gltfRootNode = model.nodes[nNodeIdx];

                ConstructTreeFromGLTF(&(pSceneNode), gltfRootNode);
                pSceneRoot->AppendChild(pSceneNode);
            }
        }
    }
    return res;
}

void GLTFImporter::CopyGLTFNode(SceneNode &sceneNode,
                                const tinygltf::Node &gltfNode)
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
        assert(Scene::IsMat4Valid(mMat));
        sceneNode.SetMatrix(mMat);
    }
}

void GLTFImporter::ConstructGeometryNode(GeometrySceneNode &geomNode,
                                         const tinygltf::Mesh &mesh,
                                         const tinygltf::Model &model)
{
    std::vector<std::unique_ptr<Primitive>> vPrimitives;
    bool bIsMeshTransparent =false;

    // Keep track of local bounding box
    glm::vec3 vAABBMin(std::numeric_limits<float>::max());
    glm::vec3 vAABBMax(std::numeric_limits<float>::min());

    uint32_t nPrimitiveIndex = 0;
    for (const auto &primitive : mesh.primitives)
    {
        std::vector<glm::vec3> vPositions;
        std::vector<glm::vec2> vUV0s;
        std::vector<glm::vec2> vUV1s;
        std::vector<glm::vec3> vNormals;

        // vPositions
        {
            const std::string sAttribkey = "POSITION";
            const auto &accessor =
                model.accessors.at(primitive.attributes.at(sAttribkey));
            const auto &bufferView = model.bufferViews[accessor.bufferView];
            const auto &buffer = model.buffers[bufferView.buffer];

            // Update min max value for this node
            auto minValue = accessor.minValues;
            auto maxValue = accessor.maxValues;

            vAABBMin.x = std::min(vAABBMin.x, (float)minValue[0]);
            vAABBMin.y = std::min(vAABBMin.y, (float)minValue[1]);
            vAABBMin.z = std::min(vAABBMin.z, (float)minValue[2]);

            vAABBMax.x = std::min(vAABBMax.x, (float)maxValue[0]);
            vAABBMax.y = std::min(vAABBMax.y, (float)maxValue[1]);
            vAABBMax.z = std::min(vAABBMax.z, (float)maxValue[2]);

            assert(accessor.type == TINYGLTF_TYPE_VEC3);
            assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
            // confirm we use the standard format

            // Hard code the buffer stride
            size_t nByteStride = 12;
            assert(bufferView.byteStride == 12 || bufferView.byteStride == 0);
            assert(buffer.data.size() >=
                   bufferView.byteOffset + accessor.byteOffset +
                       nByteStride * accessor.count);

            vPositions.resize(accessor.count);
            memcpy(vPositions.data(),
                   buffer.data.data() + bufferView.byteOffset +
                       accessor.byteOffset,
                   accessor.count * nByteStride);
        }
        // vNormals
        {
            const std::string sAttribkey = "NORMAL";
            const auto &accessor =
                model.accessors.at(primitive.attributes.at(sAttribkey));
            const auto &bufferView = model.bufferViews[accessor.bufferView];
            const auto &buffer = model.buffers[bufferView.buffer];

            assert(accessor.type == TINYGLTF_TYPE_VEC3);
            assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
            // confirm we use the standard format
            size_t nByteStride = 12;
            assert(bufferView.byteStride == 12 || bufferView.byteStride == 0);
            assert(buffer.data.size() >=
                   bufferView.byteOffset + accessor.byteOffset +
                       nByteStride * accessor.count);

            vNormals.resize(accessor.count);
            memcpy(vNormals.data(),
                   buffer.data.data() + bufferView.byteOffset +
                       accessor.byteOffset,
                   accessor.count * nByteStride);
        }
        // vUV0s
		{
			const std::string sAttribkey = "TEXCOORD_0";
			if (primitive.attributes.find(sAttribkey) != primitive.attributes.end())
			{
				const auto& accessor =
					model.accessors.at(primitive.attributes.at(sAttribkey));
				const auto& bufferView = model.bufferViews[accessor.bufferView];
				const auto& buffer = model.buffers[bufferView.buffer];

				assert(accessor.type == TINYGLTF_TYPE_VEC2);
				assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

				// confirm we use the standard format
				size_t nByteStride = 8;
				assert(bufferView.byteStride == 8 || bufferView.byteStride == 0);
				assert(buffer.data.size() >=
					bufferView.byteOffset + accessor.byteOffset +
					nByteStride * accessor.count);
				vUV0s.resize(accessor.count);
				memcpy(vUV0s.data(),
					buffer.data.data() + bufferView.byteOffset +
					accessor.byteOffset,
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
                const auto &accessor =
                    model.accessors.at(primitive.attributes.at(sAttribkey));
                const auto &bufferView = model.bufferViews[accessor.bufferView];
                const auto &buffer = model.buffers[bufferView.buffer];

                assert(accessor.type == TINYGLTF_TYPE_VEC2);
                assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

                // confirm we use the standard format
                size_t nByteStride = 8;
                assert(bufferView.byteStride == 8 || bufferView.byteStride == 0);
                assert(buffer.data.size() >=
                       bufferView.byteOffset + accessor.byteOffset +
                           nByteStride * accessor.count);
                vUV1s.resize(accessor.count);
                memcpy(vUV1s.data(),
                       buffer.data.data() + bufferView.byteOffset +
                           accessor.byteOffset,
                       accessor.count * nByteStride);
            }
            else
            {
                // Use UV0 as UV1 if UV1 doesn't exist
                vUV1s = vUV0s;
            }
        }

        assert(vUV0s.size() == vPositions.size());
        assert(vNormals.size() == vPositions.size());
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
                memcpy(vIndices.data(),
                       buffer.data.data() + bufferView.byteOffset +
                           accessor.byteOffset,
                       accessor.count * sizeof(Index));
            }
            else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
            {
                // Convert short index to index

                unsigned short *pData = (unsigned short *)(buffer.data.data() + bufferView.byteOffset + accessor.byteOffset);
                for (size_t i = 0; i < accessor.count; i++)
                {
                    vIndices[i] = (uint32_t)pData[i];
                }
            }
            else
            {
                assert(false && "Unsupported type");
            }
        }

        // Construct primitive name

        std::string sPrimitiveName(mesh.name + "_" + std::to_string(nPrimitiveIndex));
        vPrimitives.emplace_back(std::make_unique<Primitive>(sPrimitiveName, vVertices, vIndices));

        //  =========Material
        //
        // Use default material if primitive has no material
        tinygltf::Material gltfMaterial;
        gltfMaterial.name = "default material";
        if (primitive.material > -1)
        {
            gltfMaterial = model.materials.at(primitive.material);
        }
        auto materialIter = GetMaterialManager()->m_mMaterials.find(gltfMaterial.name);
        Material* pMaterial = nullptr;

        if (materialIter == GetMaterialManager()->m_mMaterials.end())
        {
            GetMaterialManager()->m_mMaterials[gltfMaterial.name] = std::make_unique<Material>();
            pMaterial = GetMaterialManager()->m_mMaterials.at(gltfMaterial.name).get();
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
                    model.textures[gltfMaterial.pbrMetallicRoughness
                                       .baseColorTexture.index];

                sAlbedoTexPath =
                    (sceneDir / model.images[albedoTexture.source].uri)
                        .string();
                sAlbedoTexName = model.images[albedoTexture.source].uri;
            }

            // Metalness
            std::string sMetalnessTexPath = "assets/Materials/white5x5.png";
            std::string sMetalnessTexName = "defaultMetalness";
            if (gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index != -1)
            {
                aUVIndices[Material::TEX_METALNESS] = gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.texCoord;
                const tinygltf::Texture &metalnessTextrue =
                    model.textures[gltfMaterial.pbrMetallicRoughness
                                       .metallicRoughnessTexture.index];
                sMetalnessTexPath =
                    (sceneDir / model.images[metalnessTextrue.source].uri).string();

                sMetalnessTexName = model.images[metalnessTextrue.source].uri;
            }

            // Normal
            std::string sNormalTexPath = "assets/Materials/black5x5.png";
            std::string sNormalTexName = "defaultNormal";
            if (gltfMaterial.normalTexture.index != -1)
            {
                aUVIndices[Material::TEX_NORMAL] = gltfMaterial.normalTexture.texCoord;
                const tinygltf::Texture &normalTexture =
                    model.textures[gltfMaterial.normalTexture.index];
                sNormalTexPath =
                    (sceneDir / model.images[normalTexture.source].uri)
                        .string();
                sNormalTexName = model.images[normalTexture.source].uri;
            }

            // Roughness
            std::string sRoughnessTexPath = "assets/Materials/white5x5.png";
            std::string sRoughnessTexName = "defaultRoughness";
            if (gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index != -1)
            {
                aUVIndices[Material::TEX_ROUGHNESS] = gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.texCoord;
                const tinygltf::Texture &metalnessTextrue =
                    model.textures[gltfMaterial.pbrMetallicRoughness
                                       .metallicRoughnessTexture.index];
                sRoughnessTexPath =
                    (sceneDir / model.images[metalnessTextrue.source].uri)
                        .string();
                sRoughnessTexName = model.images[metalnessTextrue.source].uri;
            }

            // Occulution
            std::string sOcclusionTexPath = "assets/Materials/white5x5.png";
            std::string sOcclusionTexName = "defaultOcclusion";
            if (gltfMaterial.occlusionTexture.index != -1)
            {
                aUVIndices[Material::TEX_AO] = gltfMaterial.occlusionTexture.texCoord;
                const tinygltf::Texture &occlusionTexture =
                    model.textures[gltfMaterial.occlusionTexture.index];
                sOcclusionTexPath =
                    (sceneDir / model.images[occlusionTexture.source].uri)
                        .string();
                sOcclusionTexName = model.images[occlusionTexture.source].uri;
            }

            // Emissive
            std::string sEmissiveTexPath = "assets/Materials/white5x5.png";
            std::string sEmissiveTexName = "defaultEmissive";
            if (gltfMaterial.emissiveTexture.index != -1)
            {
                aUVIndices[Material::TEX_EMISSIVE] = gltfMaterial.emissiveTexture.texCoord;
                const tinygltf::Texture &emissiveTexture =
                    model.textures[gltfMaterial.emissiveTexture.index];
                sEmissiveTexPath =
                    (sceneDir / model.images[emissiveTexture.source].uri)
                        .string();
                sEmissiveTexName = model.images[emissiveTexture.source].uri;
            }

            // PBR factors
            Material::PBRFactors pbrFactors = {
                {(float)gltfMaterial.pbrMetallicRoughness.baseColorFactor[0], (float)gltfMaterial.pbrMetallicRoughness.baseColorFactor[1],
                 (float)gltfMaterial.pbrMetallicRoughness.baseColorFactor[2], (float)gltfMaterial.pbrMetallicRoughness.baseColorFactor[3]},  // Base Color
                (float)gltfMaterial.pbrMetallicRoughness.metallicFactor,                                                                     // Metallic
                (float)gltfMaterial.pbrMetallicRoughness.roughnessFactor,                                                                    // Roughness
                {aUVIndices[0], aUVIndices[1],                                                                                               // UVs
                 aUVIndices[2], aUVIndices[3],
                 aUVIndices[4], aUVIndices[4]},
                {(float)gltfMaterial.emissiveFactor[0], (float)gltfMaterial.emissiveFactor[1], (float)gltfMaterial.emissiveFactor[2]},
                0.0f};

            pMaterial->LoadTexture(Material::TEX_ALBEDO, sAlbedoTexPath, sAlbedoTexName);
            pMaterial->LoadTexture(Material::TEX_NORMAL, sNormalTexPath, sNormalTexName);
            pMaterial->LoadTexture(Material::TEX_METALNESS, sMetalnessTexPath, sMetalnessTexName);
            pMaterial->LoadTexture(Material::TEX_ROUGHNESS, sRoughnessTexPath, sRoughnessTexName);
            pMaterial->LoadTexture(Material::TEX_AO, sOcclusionTexPath, sOcclusionTexName);
            pMaterial->LoadTexture(Material::TEX_EMISSIVE, sEmissiveTexPath, sEmissiveTexName);
            pMaterial->SetMaterialParameterFactors(pbrFactors, gltfMaterial.name);

            pMaterial->AllocateDescriptorSet();
            assert(pMaterial->GetDescriptorSet() != VK_NULL_HANDLE);
            if (gltfMaterial.alphaMode != "OPAQUE")
            {
                assert(gltfMaterial.alphaMode == "BLEND" && "Unsupported alpha mode");
                pMaterial->SetTransparent();
            }
            if (gltfMaterial.doubleSided)
            {
            }
        }
        else
        {
            pMaterial = materialIter->second.get();
        }

        vPrimitives.back()->SetMaterial(pMaterial);
        if (pMaterial->IsTransparent())
        {
            bIsMeshTransparent = true;
        }
    }
    Geometry *pGeometry = new Geometry(vPrimitives);
    geomNode.SetAABB({vAABBMin, vAABBMax});
    // Setup world transformation uniform buffer object
    // Use pointer address as string
    std::stringstream ss;
    ss << static_cast<void*>(pGeometry);
    auto *worldMatBuffer = GetRenderResourceManager()->getUniformBuffer<glm::mat4>(ss.str());
    pGeometry->SetWorldMatrixUniformBuffer(worldMatBuffer);
    pGeometry->SetWorldMatrix(glm::mat4(1.0));
    GetGeometryManager()->vpGeometries.emplace_back(pGeometry);
    geomNode.SetGeometry(pGeometry);
    if (bIsMeshTransparent)
    {
        geomNode.SetTransparent();
    }
}
