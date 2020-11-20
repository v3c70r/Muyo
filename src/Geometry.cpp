#include "Geometry.h"
#include <cassert>
#include <tiny_obj_loader.h>
#include <tiny_gltf.h>

static GeometryManager s_geometryManager;
GeometryManager *GetGeometryManager()
{
    return &s_geometryManager;
}
std::unique_ptr<Geometry> loadObj(const std::string& path, glm::mat4 mTransformation)
{
    struct TinyObjInfo
    {
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
    };
    TinyObjInfo objInfo;
    std::string err;
    bool ret = tinyobj::LoadObj(&(objInfo.attrib), &(objInfo.shapes),
                                &(objInfo.materials), &err, path.c_str());
    if (!ret)
    {
        std::cerr << err << std::endl;
    }
    else
    {
        std::cout << "Num verts :" << objInfo.attrib.vertices.size() / 3
                  << std::endl;
        std::cout << "Num Normals :" << objInfo.attrib.normals.size() / 3
                  << std::endl;
        std::cout << "Num TexCoords :" << objInfo.attrib.texcoords.size() / 2
                  << std::endl;

        // Print out mesh summery
        for (size_t i = 0; i < objInfo.shapes.size(); i++)
        {
            std::cout << "Shape " << i << ": " << std::endl;
            tinyobj::shape_t &shape = objInfo.shapes[i];
            std::cout << "\t" << shape.mesh.indices.size() << " indices\n";

        }
    }

    std::vector<std::unique_ptr<Primitive>> primitives;
    const glm::mat4 mNormalTransformation = glm::transpose(glm::inverse(mTransformation));
    for (size_t i = 0; i < objInfo.shapes.size(); i++)
    {
        const auto &vIndices = objInfo.shapes[i].mesh.indices;
        size_t numVert = vIndices.size();

        std::vector<Vertex> vertices;
        vertices.reserve(numVert);
        for (const auto &meshIdx : vIndices)
        {
            glm::vec4 pos(objInfo.attrib.vertices[3 * meshIdx.vertex_index],
                          objInfo.attrib.vertices[3 * meshIdx.vertex_index + 1],
                          objInfo.attrib.vertices[3 * meshIdx.vertex_index + 2],
                          1.0);
            pos = pos * mTransformation;

            glm::vec4 normal(
                objInfo.attrib.normals[3 * meshIdx.normal_index],
                objInfo.attrib.normals[3 * meshIdx.normal_index + 1],
                objInfo.attrib.normals[3 * meshIdx.normal_index + 2], 0.0);

            normal = normal * mNormalTransformation;

            vertices.emplace_back(Vertex(
                {{pos.x, pos.y, pos.z},
                 {normal.x, normal.y, normal.z},
                 {objInfo.attrib.texcoords[2 * meshIdx.texcoord_index],
                  objInfo.attrib.texcoords[2 * meshIdx.texcoord_index + 1],
                  0}}));
        }
        std::vector<Index> indices;
        indices.reserve(objInfo.shapes[i].mesh.indices.size());
        for (size_t index = 0; index < objInfo.shapes[i].mesh.indices.size(); index++)
        {
            indices.push_back((Index)index);
        }

        primitives.emplace_back(std::make_unique<Primitive>(vertices, indices));
    }
    return std::make_unique<Geometry>(primitives);
}

Geometry* GetQuad()
{
    static const std::vector<Vertex> VERTICES = {
        {{-1.0f, -1.0f, 0.0}, {0.0, 0.0, 1.0}, {0.0f, 0.0f, 0.0f}},
        {{1.0f, -1.0f, 0.0}, {0.0, 0.0, 1.0}, {1.0f, 0.0f, 0.0f}},
        {{1.0f, 1.0f, 0.0}, {0.0, 0.0, 1.0}, {1.0f, 1.0f, 1.0f}},
        {{-1.0f, 1.0f, 0.0}, {0.0, 0.0, 1.0}, {0.0f, 1.0f, 1.0f}}};

    static const std::vector<uint32_t> INDICES = {0, 1, 2, 2, 3, 0};
    Geometry* pGeometry = new Geometry(std::make_unique<Primitive>(VERTICES, INDICES));
    GetGeometryManager()->vpGeometries.emplace_back(pGeometry);
    return pGeometry;
}

std::unique_ptr<Geometry> getSkybox()
{
    return loadObj("assets/cube.obj");
}

std::unique_ptr<Geometry> loadGLTF(const std::string& path, glm::mat4 mTransformation)
{

    std::vector<std::unique_ptr<Primitive>> vPrimitives;

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, path.c_str());
    std::cout<<"Loading GLTF===\n";
    std::cout<<"Num meshes: "<<model.meshes.size()<<std::endl;
    std::cout<<"Num nodes:" << model.nodes.size()<<std::endl;
    const std::vector<std::string> READ_VALUES = {
        "NORMAL", "POSITION", "TEXCOORD_0"
    };
    for (const auto &node : model.nodes)
    {
        if (node.mesh < 0) continue;
        const auto &mesh = model.meshes[node.mesh];

        // construct transformation matrix;
        glm::mat4 mNodeTransformation = glm::mat4(1.0);
        glm::mat4 mNormalTransformation = glm::mat4(1.0);
        if (node.matrix.size() > 0)
        {
            assert(node.matrix.size() == 16);
            for (size_t i = 0; i < 4; i++)
            {
                for (int j = 0; j < 4; j++)
                {
                    mNodeTransformation[i][j] = (float)node.matrix[i * 4 + j];
                }
            }
            mNormalTransformation =
                glm::transpose(glm::inverse(mNodeTransformation));
        }

        std::cout<<"Node name: "<<node.name<<std::endl;
        if (node.children.size() > 0)
        {
            std::cout << "Num children" << node.children.size()<<std::endl;
        }

    //for (const auto &mesh : model.meshes)
    //{
        //std::cout << "Mesh Name: " << mesh.name << std::endl;
        // List attributes
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
                assert(accessor.componentType ==  TINYGLTF_COMPONENT_TYPE_FLOAT);
                // confirm we use the standard format
                assert(bufferView.byteStride == 12);
                assert(buffer.data.size() >= bufferView.byteOffset + accessor.byteOffset + bufferView.byteStride * accessor.count);

                vPositions.resize(accessor.count);
                memcpy(vPositions.data(), buffer.data.data() + bufferView.byteOffset + accessor.byteOffset, accessor.count * bufferView.byteStride);

            }
            // vNormals
            {
                const std::string sAttribkey = "NORMAL";
                const auto &accessor =
                    model.accessors.at(primitive.attributes.at(sAttribkey));
                const auto &bufferView = model.bufferViews[accessor.bufferView];
                const auto &buffer = model.buffers[bufferView.buffer];

                assert(accessor.type == TINYGLTF_TYPE_VEC3);
                assert(accessor.componentType ==  TINYGLTF_COMPONENT_TYPE_FLOAT);
                // confirm we use the standard format
                assert(bufferView.byteStride == 12);
                assert(buffer.data.size() >= bufferView.byteOffset + accessor.byteOffset + bufferView.byteStride * accessor.count);

                vNormals.resize(accessor.count);
                memcpy(vNormals.data(), buffer.data.data() + bufferView.byteOffset + accessor.byteOffset, accessor.count * bufferView.byteStride);
            }
            // vUVs
            {
                const std::string sAttribkey = "TEXCOORD_0";
                const auto &accessor =
                    model.accessors.at(primitive.attributes.at(sAttribkey));
                const auto &bufferView = model.bufferViews[accessor.bufferView];
                const auto &buffer = model.buffers[bufferView.buffer];

                assert(accessor.type == TINYGLTF_TYPE_VEC2);
                assert(accessor.componentType ==  TINYGLTF_COMPONENT_TYPE_FLOAT);

                // confirm we use the standard format
                assert(bufferView.byteStride == 8);
                assert(buffer.data.size() >= bufferView.byteOffset + accessor.byteOffset + bufferView.byteStride * accessor.count);
                vUVs.resize(accessor.count);
                memcpy(vUVs.data(), buffer.data.data() + bufferView.byteOffset + accessor.byteOffset, accessor.count * bufferView.byteStride);
            }

            assert(vUVs.size() == vPositions.size());
            assert(vNormals.size() == vPositions.size());
            std::vector<Vertex> vVertices(vUVs.size());
            for (size_t i=0; i< vVertices.size(); i++ )
            {
                Vertex& vertex = vVertices[i];
                {
                    glm::vec4 pos(vPositions[i], 1.0);
                    pos = mNodeTransformation * pos;
                    vertex.pos = pos;
                    glm::vec4 normal(vNormals[i], 1.0);
                    normal = mNormalTransformation * normal;
                    vertex.normal = normal;
                    vertex.textureCoord = {vUVs[i].x, vUVs[i].y, 0.0};
                }
            }
            // Indices
            std::vector<Index> vIndices;
            {
                const auto &accessor =
                    model.accessors.at(primitive.indices);
                const auto &bufferView = model.bufferViews[accessor.bufferView];
                const auto &buffer = model.buffers[bufferView.buffer];
                assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT);
                vIndices.resize(accessor.count);
                memcpy(vIndices.data(), buffer.data.data() + bufferView.byteOffset + accessor.byteOffset, accessor.count * sizeof(Index));
            }
            vPrimitives.emplace_back(std::make_unique<Primitive>(vVertices, vIndices));
        }

    }
    return std::make_unique<Geometry>(vPrimitives);

    // bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, argv[1]); //
    // for binary glTF(.glb)

    if (!warn.empty())
    {
        printf("Warn: %s\n", warn.c_str());
    }

    if (!err.empty())
    {
        printf("Err: %s\n", err.c_str());
    }

    if (!ret)
    {
        printf("Failed to parse glTF\n");
        //return -1;
    }
    return nullptr;
}
