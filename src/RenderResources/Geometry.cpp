#include "Geometry.h"

#include <tiny_gltf.h>
#include <tiny_obj_loader.h>

#include <cassert>

namespace Muyo
{

static GeometryManager s_geometryManager;
GeometryManager *GetGeometryManager()
{
    return &s_geometryManager;
}
}  // namespace Muyo
