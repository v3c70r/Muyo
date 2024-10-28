#include "PerObjResourceManager.h"

namespace Muyo
{
static PerObjResourceManager s_perObjResourceManager;
PerObjResourceManager *GetPerObjResourceManager()
{
    return &s_perObjResourceManager;
}

size_t PerObjResourceManager::AppendPerObjData(const PerObjData& perObjData)
{
    m_vPerObjDataCPU.push_back(perObjData);
    memcpy(m_vPerObjDataCPU.back().vSubmeshDatas, perObjData.vSubmeshDatas, sizeof(PerSubmeshData) * perObjData.nSubmeshCount);

    return m_vPerObjDataCPU.size() - 1;
}
};
