#pragma once
#include <meshoptimizer.h>

namespace Muyo
{
class Submesh;
}
class MeshProcessor
{
public:
    static void ProcessSubmesh(Muyo::Submesh& submesh);
private:
    static constexpr size_t MAX_VERTICES = 64;
    static constexpr size_t MAX_TRIANGLES = 124;
    static constexpr float CONE_WEIGHT = 0.0f;
};
