#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require


struct PrimitiveDesc
{
    uint64_t vertexAddress;
    uint64_t indexAddress;
};

struct Vertex
{
    vec3 pos;
    vec3 normal;
    vec4 textureCoord;
};

layout(buffer_reference, scalar) buffer Vertices {Vertex v[]; }; // Positions of an object
layout(buffer_reference, scalar) buffer Indices {ivec3 i[]; }; // Triangle indices
layout(set = 1, binding = 2, scalar) buffer PrimDesc_ {PrimitiveDesc i[]; } primDescs;

layout(location = 0) rayPayloadInEXT vec3 vResColor;
hitAttributeEXT vec2 vHitAttribs;


void main()
{
    PrimitiveDesc primDesc = primDescs.i[gl_InstanceCustomIndexEXT + gl_GeometryIndexEXT];
    Indices indices = Indices(primDesc.indexAddress);
    Vertices vertices = Vertices(primDesc.vertexAddress);

    ivec3 triangleIndex = indices.i[gl_PrimitiveID];
    
    // Vertex of the triangle
    Vertex v0 = vertices.v[triangleIndex.x];
    Vertex v1 = vertices.v[triangleIndex.y];
    Vertex v2 = vertices.v[triangleIndex.z];


    const vec3 barycentrics = vec3(1.0f - vHitAttribs.x - vHitAttribs.y, vHitAttribs.x, vHitAttribs.y);

    // Computing the coordinates of the hit position
    const vec3 pos      = v0.pos * barycentrics.x + v1.pos * barycentrics.y + v2.pos * barycentrics.z;
    vec3 worldPos = (gl_ObjectToWorldEXT * vec4(pos, 1.0));  // Transforming the position to world space

    vResColor = vec3(worldPos);

    //vResColor = vec3(1.0, 0.0, 0.0);
}
