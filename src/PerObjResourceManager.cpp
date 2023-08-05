#include "PerObjResourceManager.h"

namespace Muyo
{
static PerObjResourceManager s_perObjResourceManager;
PerObjResourceManager *GetPerObjResourceManager()
{
    return &s_perObjResourceManager;
}

size_t PerObjResourceManager::AppendPerObjData(const PerObjData&& perObjData)
{
     m_vPerObjDataCPU.push_back(perObjData);
     return m_vPerObjDataCPU.size() - 1;
}
};
