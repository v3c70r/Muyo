#include "Material.h"
static MaterialManager s_materialManager;
MaterialManager *GetMaterialManager()
{
    return &s_materialManager;
}
