#pragma once
#include "RenderResourceManager.h"
#include "SharedStructures.h"

namespace Muyo
{
class PerObjResourceManager
{
public:
    // Append per object data and return the index in the array
    size_t AppendPerObjData(const PerObjData& perObjData);
    void Upload()
    {
        m_pPerObjDataGPU = GetRenderResourceManager()->GetStorageBuffer("PerObjData", m_vPerObjDataCPU);
        m_bUploaded = true;
    }
    bool HasUploaded() const { return m_bUploaded; }
    const StorageBuffer<PerObjData>* GetPerObjResource()
    {
        assert(m_bUploaded);
        return m_pPerObjDataGPU;
    }
    /// @return The CPU-side copy, for a consumer that needs a *flat* view of the submesh records.
    ///
    /// The heap path cannot read `PerObjData::vSubmeshDatas` (an array-typed member) on NVIDIA's
    /// descriptor-heap/untyped-pointer lowering - it returns garbage (#262). The GBUFFER node
    /// therefore builds a flat material-index buffer from this copy on the host instead.
    const std::vector<PerObjData>& GetPerObjDataCPU() const { return m_vPerObjDataCPU; }

private:
    std::vector<PerObjData> m_vPerObjDataCPU;
    StorageBuffer<PerObjData>* m_pPerObjDataGPU = nullptr;

    bool m_bUploaded = false;
};
PerObjResourceManager* GetPerObjResourceManager();
}  // namespace Muyo
