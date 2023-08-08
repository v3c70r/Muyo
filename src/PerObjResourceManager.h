#pragma once
#include "RenderResourceManager.h"
#include "SharedStructures.h"

namespace Muyo
{
class PerObjResourceManager
{
public:
    // Append per object data and return the index in the array
    size_t AppendPerObjData(const PerObjData&& perObjData);
    void Upload()
    {
        m_pPerObjDataGPU = GetRenderResourceManager()->GetStorageBuffer("PerObjData", m_vPerObjDataCPU);
        m_bUploaded = true;
    }
    bool HasUploaded() const {return m_bUploaded;}
    const StorageBuffer<PerObjData>* GetPerObjResource()
    {
        assert(m_bUploaded);
        return m_pPerObjDataGPU;
    }

private:
    std::vector<PerObjData> m_vPerObjDataCPU;
    StorageBuffer<PerObjData> *m_pPerObjDataGPU = nullptr;

    bool m_bUploaded = false;
};
PerObjResourceManager *GetPerObjResourceManager();
}  // namespace Muyo
