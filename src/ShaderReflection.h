#pragma once
#include <cstdint>
namespace Muyo
{
// Interface structure
using SpirvCode = std::vector<uint32_t>;
struct ShaderReflection
{
    struct DescriptorBinding
    {
        uint32_t set;
        uint32_t binding;
        VkDescriptorType type;
        uint32_t count;
        VkShaderStageFlags stageFlags;
        std::string name;
        std::vector<uint32_t> arrayDims;
        VkDescriptorBindingFlags bindingFlags;
    };
    std::vector<DescriptorBinding> descriptorBindings;

    struct PushConstantRange
    {
        uint32_t offset;
        uint32_t size;
        VkShaderStageFlags stageFlags;
    };
    std::vector<PushConstantRange> pushConstantRanges;

    struct IOVariable
    {
        uint32_t location;
        std::string name;
        uint32_t type;  // Optionally use VkFormat or custom enum
        std::vector<uint32_t> arrayDims;
        bool isBuiltIn;
        std::string builtInName;  // if isBuiltIn
        bool operator==(const IOVariable& other) const
        {
            return location == other.location && name == other.name && type == other.type && arrayDims == other.arrayDims && isBuiltIn == other.isBuiltIn && builtInName == other.builtInName;
        }
    };
    std::vector<IOVariable> inputVariables;
    std::vector<IOVariable> outputVariables;

    struct WorkgroupSize
    {
        uint32_t x, y, z;
        bool operator==(const WorkgroupSize& other) const
        {
            return x == other.x && y == other.y && z == other.z;
        }
    };
    struct EntryPoint
    {
        std::string name;
        VkShaderStageFlagBits stage;
        std::optional<WorkgroupSize> workgroupSize;

        bool operator==(const EntryPoint& other) const
        {
            return name == other.name && stage == other.stage && workgroupSize == other.workgroupSize;
        }
    };
    std::vector<EntryPoint> entryPoints;

    struct SpecializationConstant
    {
        uint32_t id;
        std::string name;
        uint32_t type;
        uint32_t defaultValue;
        bool operator==(const SpecializationConstant& other) const
        {
            return id == other.id && name == other.name && type == other.type && defaultValue == other.defaultValue;
        }
    };
    std::vector<SpecializationConstant> specializationConstants;

    struct RayTracingInfo
    {
        VkShaderStageFlagBits stage;
        std::string entryPoint;
        uint32_t payloadSize;
        uint32_t attributeSize;
        bool operator==(const RayTracingInfo& other) const
        {
            return stage == other.stage && entryPoint == other.entryPoint && payloadSize == other.payloadSize && attributeSize == other.attributeSize;
        }
    };
    std::vector<RayTracingInfo> rayTracingInfos;


    struct InputAttachment
    {
        uint32_t set;
        uint32_t binding;
        uint32_t inputAttachmentIndex;
        std::string name;
        bool operator==(const InputAttachment& other) const
        {
            return set == other.set && binding == other.binding && inputAttachmentIndex == other.inputAttachmentIndex && name == other.name;
        }
    };
    std::vector<InputAttachment> inputAttachments;

    struct ResourceDecoration
    {
        uint32_t set;
        uint32_t binding;
        bool readonly;
        bool writeonly;
        bool coherent;
        bool volatile_;
        bool operator==(const ResourceDecoration& other) const
        {
            return set == other.set && binding == other.binding && readonly == other.readonly && writeonly == other.writeonly && coherent == other.coherent && volatile_ == other.volatile_;
        }
    };
    std::vector<ResourceDecoration> resourceDecorations;

    std::vector<std::string> requiredFeatures;
    std::vector<std::string> requiredExtensions;
};
}  // namespace Muyo
