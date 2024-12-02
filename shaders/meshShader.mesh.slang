#extension GL_EXT_mesh_shader : require
struct MeshOutput
{
    float4 position : SV_Position;
};

[outputtopology("triangle")]
[numthreads(1, 1, 1)]

[shader("mesh")]
void main(out indices uint3 triangles[1], out vertices MeshOutput vertices[3])
{

    // Set the mesh output counts
    SetMeshOutputCounts(3, 1);

    // Define the triangle indices
    triangles[0] = uint3(0, 1, 2);

    // Define the vertex positions and colors
    vertices[0].position = float4(-0.5, 0.5, 0.0, 1.0);

    vertices[1].position = float4(0.5, 0.5, 0.0, 1.0);

    vertices[2].position = float4(0.0, -0.5, 0.0, 1.0);
}
