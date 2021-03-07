#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <cstring>
#include <cassert>

// Setup required instance extensions, device extensions and features
struct SEntry
{
    SEntry(const std::string& entryName, bool isOptional = false, void* pointerFeatureStruct = nullptr, uint32_t checkVersion = 0)
        : m_sName(entryName), m_bOptional(isOptional), m_pFeatureStruct(pointerFeatureStruct), m_uVersion(checkVersion)
    {
    }
    std::string m_sName;
    bool m_bOptional = false;
    void* m_pFeatureStruct = nullptr;
    uint32_t m_uVersion = 0;
};

class VulkanContext
{
public:
    VulkanContext& EnableInstanceExtension(const std::string& sName, bool bIsOptional = false);
    VulkanContext& EnableInstanceLayer(const std::string& sName, bool bIsOptional = false);
    VulkanContext& EnableDeviceExtension(const std::string& sName, bool bIsOptional = false, void* pFeatureStruct = nullptr, uint32_t nVersion = 0);

    VulkanContext& DisableInstanceExtension(const std::string& sName);
    VulkanContext& DisableInstanceLayer(const std::string& sName);
    VulkanContext& DisableDeviceExtension(const std::string& sName);

    const std::vector<SEntry> GetEnabledInstanceExtensions() const
    {
        return m_vInstanceExtensions;
    }
    const std::vector<SEntry> GetEnabledInstanceLayers() const
    {
        return m_vInstanceLayers;
    }
    const std::vector<SEntry> GetEnabledDeviceExtensions() const
    {
        return m_vDeviceExtensions;
    }

    static VkResult FillFilteredNameArray(std::vector<const char*>& used,
                                          const std::vector<VkExtensionProperties>& properties,
                                          const std::vector<SEntry>& requested,
                                          std::vector<void*>& featureStructs)
    {
        for (const auto& itr : requested)
        {
            bool found = false;
            for (const auto& property : properties)
            {
                if (strcmp(itr.m_sName.c_str(), property.extensionName) == 0 && (itr.m_uVersion == 0 || itr.m_uVersion == property.specVersion))
                {
                    found = true;
                    break;
                }
            }

            if (found)
            {
                used.push_back(itr.m_sName.c_str());
                if (itr.m_pFeatureStruct)
                {
                    featureStructs.push_back(itr.m_pFeatureStruct);
                }
            }
            else if (!itr.m_bOptional)
            {
                assert("VK_ERROR_EXTENSION_NOT_PRESENT" );
                return VK_ERROR_EXTENSION_NOT_PRESENT;
            }
        }

        return VK_SUCCESS;
    }

private:
    void RemoveEntry(std::vector<SEntry>& vEntries, const std::string& sName)
    {
        for (size_t i = 0; i < vEntries.size(); i++)
        {
            if (vEntries[i].m_sName == sName)
            {
                vEntries.erase(vEntries.begin() + i);
            }
        }
    }

private:
    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;

    std::vector<SEntry> m_vInstanceLayers;
    std::vector<SEntry> m_vInstanceExtensions;
    std::vector<SEntry> m_vDeviceExtensions;
};

