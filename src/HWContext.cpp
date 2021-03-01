#include "HWContext.h"

VulkanContext& VulkanContext::EnableInstanceExtension(const std::string& sName, bool bIsOptional)
{
    m_vInstanceExtensions.emplace_back(sName, bIsOptional);
    return *this;
}
VulkanContext& VulkanContext::EnableInstanceLayer(const std::string& sName, bool bIsOptional)
{
    m_vInstanceLayers.emplace_back(sName, bIsOptional);
    return *this;
}
VulkanContext& VulkanContext::EnableDeviceExtension(const std::string& sName, bool bIsOptional, void* pFeatureStruct, uint32_t nVersion)
{
    m_vDeviceExtensions.emplace_back(sName, bIsOptional, pFeatureStruct, nVersion);
    return *this;
}

VulkanContext& VulkanContext::DisableInstanceExtension(const std::string& sName)
{
    RemoveEntry(m_vInstanceExtensions, sName);
    return *this;
}
VulkanContext& VulkanContext::DisableInstanceLayer(const std::string& sName)
{
    RemoveEntry(m_vInstanceLayers, sName);
    return *this;
}
VulkanContext& VulkanContext::DisableDeviceExtension(const std::string& sName)
{
    RemoveEntry(m_vDeviceExtensions, sName);
    return *this;
}

