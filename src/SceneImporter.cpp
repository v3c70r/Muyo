#include "SceneImporter.h"

#include <tiny_gltf.h>

#include <cassert>
#include <functional>
#include <glm/gtc/quaternion.hpp>

#include "Geometry.h"
#include "Material.h"
#include "SceneImporter.h"

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
            // For each root node in scene
            for (int nNodeIdx : tinyScene.nodes)
            {
                tinygltf::Node node = model.nodes[nNodeIdx];
                SceneNode *pSceneNode = new SceneNode(node.name);

                std::function<void(SceneNode &, const tinygltf::Node &)>
                    CopyGLTFNodeRecursive =
                        [&](SceneNode &sceneNode,
                            const tinygltf::Node &gltfNode) {
                            for (int nNodeIdx : gltfNode.children)
                            {
                                const tinygltf::Node &node =
                                    model.nodes[nNodeIdx];
                                SceneNode *pSceneNode = nullptr;
                                if (node.mesh != -1)
                                {
                                    const tinygltf::Mesh mesh =
                                        model.meshes[node.mesh];
                                    pSceneNode = new GeometrySceneNode;
                                    CopyGLTFNode(sceneNode, gltfNode);
                                    ConstructGeometryNode(
                                        static_cast<GeometrySceneNode &>(
                                            *pSceneNode),
                                        mesh, model);
                                }
                                else
                                {
                                    pSceneNode = new SceneNode;
                                    CopyGLTFNode(sceneNode, gltfNode);
                                }

                                CopyGLTFNodeRecursive(*pSceneNode, node);
                                sceneNode.AppendChild(pSceneNode);
                            }
                        };
                CopyGLTFNodeRecursive(*pSceneNode, node);
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

    {
        glm::vec3 vT(0.0f);
        if (gltfNode.translation.size() > 0)
        {
            vT.x = (float)gltfNode.translation[1];
            vT.y = (float)gltfNode.translation[2];
            vT.z = (float)gltfNode.translation[3];
        }

        glm::quat qR;
        if (gltfNode.rotation.size() > 0)
        {
            qR.x = (float)gltfNode.rotation[0];
            qR.y = (float)gltfNode.rotation[1];
            qR.z = (float)gltfNode.rotation[2];
            qR.w = (float)gltfNode.rotation[3];
        }

        glm::vec3 vS(0.0f);
        if (gltfNode.scale.size() > 0)
        {
            vS.x = (float)gltfNode.scale[0];
            vS.x = (float)gltfNode.scale[1];
            vS.x = (float)gltfNode.scale[2];
        }

        glm::mat4 mMat;
        glm::translate(mMat, vT);
        mMat = mMat * glm::mat4_cast(qR);
        glm::scale(mMat, vS);
        sceneNode.SetMatrix(mMat);
    }
}

void GLTFImporter::ConstructGeometryNode(GeometrySceneNode &geomNode,
                                         const tinygltf::Mesh &mesh,
                                         const tinygltf::Model &model)
{
    std::vector<std::unique_ptr<Primitive>> vPrimitives;
    for (const auto &primitive : mesh.primitives)
    {
        std::vector<glm::vec3> vPositions;
        std::vector<glm::vec3> vUVs;
        std::vector<glm::vec3> vNormals;

        // vPositions
        {
            const std::string sAttribkey = "POSITION";
            const auto &accessor =
                model.accessors.at(primitive.attributes.at(sAttribkey));
            const auto &bufferView = model.bufferViews[accessor.bufferView];
            const auto &buffer = model.buffers[bufferView.buffer];

            assert(accessor.type == TINYGLTF_TYPE_VEC3);
            assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
            // confirm we use the standard format
            assert(bufferView.byteStride == 12);
            assert(buffer.data.size() >=
                   bufferView.byteOffset + accessor.byteOffset +
                       bufferView.byteStride * accessor.count);

            vPositions.resize(accessor.count);
            memcpy(vPositions.data(),
                   buffer.data.data() + bufferView.byteOffset +
                       accessor.byteOffset,
                   accessor.count * bufferView.byteStride);
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
            assert(bufferView.byteStride == 12);
            assert(buffer.data.size() >=
                   bufferView.byteOffset + accessor.byteOffset +
                       bufferView.byteStride * accessor.count);

            vNormals.resize(accessor.count);
            memcpy(vNormals.data(),
                   buffer.data.data() + bufferView.byteOffset +
                       accessor.byteOffset,
                   accessor.count * bufferView.byteStride);
        }
        // vUVs
        {
            const std::string sAttribkey = "TEXCOORD_0";
            const auto &accessor =
                model.accessors.at(primitive.attributes.at(sAttribkey));
            const auto &bufferView = model.bufferViews[accessor.bufferView];
            const auto &buffer = model.buffers[bufferView.buffer];

            assert(accessor.type == TINYGLTF_TYPE_VEC2);
            assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

            // confirm we use the standard format
            assert(bufferView.byteStride == 8);
            assert(buffer.data.size() >=
                   bufferView.byteOffset + accessor.byteOffset +
                       bufferView.byteStride * accessor.count);
            vUVs.resize(accessor.count);
            memcpy(vUVs.data(),
                   buffer.data.data() + bufferView.byteOffset +
                       accessor.byteOffset,
                   accessor.count * bufferView.byteStride);
        }

        assert(vUVs.size() == vPositions.size());
        assert(vNormals.size() == vPositions.size());
        std::vector<Vertex> vVertices(vUVs.size());
        for (size_t i = 0; i < vVertices.size(); i++)
        {
            Vertex &vertex = vVertices[i];
            {
                glm::vec4 pos(vPositions[i], 1.0);
                vertex.pos = pos;
                glm::vec4 normal(vNormals[i], 1.0);
                vertex.normal = normal;
                vertex.textureCoord = {vUVs[i].x, vUVs[i].y, 0.0};
            }
        }
        // Indices
        std::vector<Index> vIndices;
        {
            const auto &accessor = model.accessors.at(primitive.indices);
            const auto &bufferView = model.bufferViews[accessor.bufferView];
            const auto &buffer = model.buffers[bufferView.buffer];
            assert(accessor.componentType ==
                   TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT);
            vIndices.resize(accessor.count);
            memcpy(vIndices.data(),
                   buffer.data.data() + bufferView.byteOffset +
                       accessor.byteOffset,
                   accessor.count * sizeof(Index));
        }
        vPrimitives.emplace_back(
            std::make_unique<Primitive>(vVertices, vIndices));

        //  =========Material
        const tinygltf::Material &gltfMaterial =
            model.materials[primitive.material];

        // TODO: Get texture manager so we don't load used textures.
        if (GetMaterialManager()->m_mMaterials.find(gltfMaterial.name) ==
            GetMaterialManager()->m_mMaterials.end())
        {
            GetMaterialManager()->m_mMaterials[gltfMaterial.name] =
                std::make_unique<Material>();
            Material *pMaterial =
                GetMaterialManager()->m_mMaterials.at(gltfMaterial.name).get();
            const std::filesystem::path sceneDir = m_sceneFile.parent_path();

            // Load material textures

            // Albedo
            std::string sAlbedoTexPath =
                "assets/Materials/plasticpattern1-ue/"
                "plasticpattern1-albedo.png";
            std::string sAlbedoTexName = "defaultAlbedo";
            if (gltfMaterial.pbrMetallicRoughness.baseColorTexture.index != -1)
            {
                const tinygltf::Texture &albedoTexture =
                    model.textures[gltfMaterial.pbrMetallicRoughness
                                       .baseColorTexture.index];

                sAlbedoTexPath =
                    (sceneDir / model.images[albedoTexture.source].uri)
                        .string();
                sAlbedoTexName = model.images[albedoTexture.source].uri;
                //sAlbedoTexName = model.images[albedoTexture.source].name;
            }

            // Metalness
            std::string sMetalnessTexPath =
                "assets/Materials/plasticpattern1-ue/"
                "plasticpattern1-metalness.png";
            std::string sMetalnessTexName = "defaultMetalness";
            if (gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index != -1)
            {
                const tinygltf::Texture &metalnessTextrue =
                    model.textures[gltfMaterial.pbrMetallicRoughness
                                       .metallicRoughnessTexture.index];
                sMetalnessTexPath =
                    (sceneDir / model.images[metalnessTextrue.source].uri).string();

                sMetalnessTexName = model.images[metalnessTextrue.source].uri;
                //sMetalnessTexName = model.images[metalnessTextrue.source].name;
            }

            // Normal
            std::string sNormalTexPath =
                "assets/Materials/plasticpattern1-ue/"
                "plasticpattern1-normal2b.png";
            std::string sNormalTexName = "defaultNormal";
            if (gltfMaterial.normalTexture.index != -1)
            {
            const tinygltf::Texture &normalTexture =
                model.textures[gltfMaterial.normalTexture.index];
                sNormalTexPath =
                    (sceneDir / model.images[normalTexture.source].uri)
                        .string();
                sNormalTexName = model.images[normalTexture.source].uri;
                //sNormalTexName = model.images[normalTexture.source].name;
            }

            // Roughness
            std::string sRoughnessTexPath =
                "assets/Materials/plasticpattern1-ue/"
                "plasticpattern1-roughness2.png";
            std::string sRoughnessTexName = "defaultRoughness";
            if (gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index != -1)
            {

                const tinygltf::Texture &metalnessTextrue =
                    model.textures[gltfMaterial.pbrMetallicRoughness
                                       .metallicRoughnessTexture.index];
                sRoughnessTexPath =
                    (sceneDir / model.images[metalnessTextrue.source].uri)
                        .string();
                sRoughnessTexName = model.images[metalnessTextrue.source].uri;
                //sRoughnessTexName = model.images[metalnessTextrue.source].name;
            }

            // Occulution
            std::string sOcclusionTexPath = "assets/Materials/white5x5.png";
            std::string sOcclusionTexName = "defaultOcclusion";
            if (gltfMaterial.occlusionTexture.index != -1)
            {
                const tinygltf::Texture &occlusionTexture =
                    model.textures[gltfMaterial.occlusionTexture.index];
                sOcclusionTexPath =
                    (sceneDir / model.images[occlusionTexture.source].uri)
                        .string();
                sOcclusionTexName = model.images[occlusionTexture.source].uri;
                //sOcclusionTexName = model.images[occlusionTexture.source].name;
            }

            pMaterial->loadTexture(Material::TEX_ALBEDO, sAlbedoTexPath, sAlbedoTexName);
            pMaterial->loadTexture(Material::TEX_METALNESS, sMetalnessTexPath, sMetalnessTexName);
            pMaterial->loadTexture(Material::TEX_NORMAL, sNormalTexPath, sNormalTexName);
            pMaterial->loadTexture(Material::TEX_ROUGHNESS, sRoughnessTexPath, sRoughnessTexName);
            pMaterial->loadTexture(Material::TEX_AO, sOcclusionTexPath, sOcclusionTexName);
            pMaterial->AllocateDescriptorSet();
            assert(pMaterial->GetDescriptorSet() != VK_NULL_HANDLE);
            vPrimitives.back()->SetMaterial(pMaterial);
        }
    }
    Geometry *pGeometry = new Geometry(vPrimitives);
    GetGeometryManager()->vpGeometries.emplace_back(pGeometry);
    geomNode.SetGeometry(pGeometry);
}
