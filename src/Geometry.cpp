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
                  0,0}}));
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
        {{-1.0f, -1.0f, 0.0}, {0.0, 0.0, 1.0}, {0.0f, 0.0f, 0.0f, 0.0f}},
        {{1.0f, -1.0f, 0.0}, {0.0, 0.0, 1.0}, {1.0f, 0.0f, 0.0f, 0.0f}},
        {{1.0f, 1.0f, 0.0}, {0.0, 0.0, 1.0}, {1.0f, 1.0f, 1.0f, 0.0f}},
        {{-1.0f, 1.0f, 0.0}, {0.0, 0.0, 1.0}, {0.0f, 1.0f, 1.0f, 0.0f}}};

    static const std::vector<uint32_t> INDICES = {0, 1, 2, 2, 3, 0};
    Geometry* pGeometry = new Geometry(std::make_unique<Primitive>(VERTICES, INDICES));
    GetGeometryManager()->vpGeometries.emplace_back(pGeometry);
    return pGeometry;
}

std::unique_ptr<Geometry> getSkybox()
{
    return loadObj("assets/cube.obj");
}
