#include "RenderGraphBuilder.h"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "PipelineStateBuilder.h"
#include "RenderGraph/ResourceUseResolver.h"
#include "ShaderReflectionFetcher.h"
#include "VkExtFuncsLoader.h"
#include "vulkan/vulkan_core.h"

namespace
{
bool IsDepthFormat(VkFormat format)
{
    switch (format)
    {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return true;
        default:
            return false;
    }
}

VkImageAspectFlags AspectForFormat(VkFormat format)
{
    if (IsDepthFormat(format)) return VK_IMAGE_ASPECT_DEPTH_BIT;
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

uint32_t AlignUp(uint32_t nSize, uint32_t nAlignment) { return (nSize + nAlignment - 1) / nAlignment * nAlignment; }
}  // namespace

namespace Muyo::RenderGraph
{
namespace
{
/// Reject a declaration that cannot describe a valid allocation (#127).
///
/// A desc is input from the author: a zero size, an undefined format or a zero usage flag set cannot be
/// turned into a valid resource, and letting it through moves the failure to the allocator or to a driver
/// VUID that names a handle. Rejected here, at the declaration line.
/// @param handle Resource being declared, so the message names it.
/// @param desc   Its declaration.
void ValidateResourceDesc(const std::string& handle, const ResourceDesc& desc)
{
    std::visit(
        [&](const auto& d)
        {
            using T = std::decay_t<decltype(d)>;
            if constexpr (std::is_same_v<T, BufferResourceDesc>)
            {
                if (d.count == 0)
                {
                    throw std::runtime_error("AddResource('" + handle + "') declares a buffer with count 0.");
                }
                if (d.stride == 0)
                {
                    throw std::runtime_error("AddResource('" + handle + "') declares a buffer with stride 0.");
                }
            }
            else
            {
                if (d.extent.width == 0 || d.extent.height == 0)
                {
                    throw std::runtime_error("AddResource('" + handle + "') declares an image with a zero extent (" +
                                             std::to_string(d.extent.width) + "x" + std::to_string(d.extent.height) +
                                             ").");
                }
                if (d.format == VK_FORMAT_UNDEFINED)
                {
                    throw std::runtime_error("AddResource('" + handle +
                                             "') declares an image with VK_FORMAT_UNDEFINED.");
                }
                if (d.mips == 0 || d.layers == 0)
                {
                    throw std::runtime_error("AddResource('" + handle + "') declares an image with " +
                                             std::to_string(d.mips) + " mips and " + std::to_string(d.layers) +
                                             " layers; both must be at least 1.");
                }
            }
            if (d.usage == 0)
            {
                throw std::runtime_error("AddResource('" + handle +
                                         "') declares usage 0, so no operation would be valid for it.");
            }
        },
        desc);
}
}  // namespace

namespace
{
/// Reject a service the graph cannot run without, **before** anything dereferences it. `m_services` is
/// declared ahead of the members whose construction dereferences it, and a member initialiser list runs
/// in declaration order, so a missing entry becomes a named error here rather than a null dereference
/// inside another member's constructor. A throw and not an assert, because a caller can falsify this and
/// the error path has to survive `NDEBUG` (AGENTS.md section 3).
const RenderGraphServices& RequireServices(const RenderGraphServices& services)
{
    if (services.device == nullptr)
    {
        throw std::runtime_error(
            "RenderGraphBuilder: services.device is null. The graph has no device to create pipelines, "
            "command buffers or synchronisation with.");
    }
    if (services.resourceManager == nullptr)
    {
        throw std::runtime_error(
            "RenderGraphBuilder: services.resourceManager is null. The graph allocates its declared "
            "resources through it.");
    }
    if (services.descriptorManager == nullptr)
    {
        throw std::runtime_error(
            "RenderGraphBuilder: services.descriptorManager is null. The graph allocates descriptor set "
            "layouts and sets through it.");
    }
    return services;
}
}  // namespace

RenderGraphBuilder::RenderGraphBuilder(const RenderGraphServices& services)
    : m_services(RequireServices(services))
    , m_shaderAssetManager(m_services.device->GetDevice())
    , m_vkDevice(m_services.device->GetDevice())
    , m_executor(m_services.device)
    , m_descriptorSetCache(m_vkDevice, *m_services.descriptorManager)
{
    VkPhysicalDeviceProperties deviceProperties{};
    vkGetPhysicalDeviceProperties(m_services.device->GetPhysicalDevice(), &deviceProperties);
    m_minUniformBufferOffsetAlignment = deviceProperties.limits.minUniformBufferOffsetAlignment;
    m_minStorageBufferOffsetAlignment = deviceProperties.limits.minStorageBufferOffsetAlignment;
}

RenderGraphBuilder::RenderGraphBuilder(VkRenderDevice* renderDevice)
    : RenderGraphBuilder(RenderGraphServices{.device = renderDevice,
                                             .resourceManager = GetRenderResourceManager(),
                                             .descriptorManager = GetDescriptorManager()})
{
}

std::pair<VkDeviceSize, VkDeviceSize> RenderGraphBuilder::ResolveBufferRange(const ResourceHandle& nodeName,
                                                                             const ResolvedResourceUse& use,
                                                                             const BufferResource& buffer,
                                                                             VkDescriptorType descriptorType) const
{
    if (!use.bufferRange.has_value())
    {
        return {0, buffer.GetSize()};
    }

    const bool bUniform = descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                          descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    const VkDeviceSize nAlignment = bUniform ? m_minUniformBufferOffsetAlignment : m_minStorageBufferOffsetAlignment;
    ValidateBufferRange(nodeName, use.handle, *use.bufferRange, buffer.GetSize(), nAlignment);
    return {static_cast<VkDeviceSize>(use.bufferRange->offset), static_cast<VkDeviceSize>(use.bufferRange->size)};
}

RenderGraphBuilder::~RenderGraphBuilder()
{
    // Before destroying anything the GPU may still be using. `Execute(RenderGraphExecuteInfo)` submits
    // without waiting, so a builder destroyed right after a non-blocking Execute would free its pipelines
    // and descriptor sets while its command buffers are still pending
    // (`VUID-vkDestroyPipeline-pipeline-00765`). The executor's own destructor waits, but it runs after
    // this body, which is too late.
    m_executor.WaitIdle();
    m_compiledGraph.Destroy(m_vkDevice, m_services.descriptorManager->GetDescriptorPool());
    // After the compiled graph, because its descriptors and pipelines name these buffers, and before the
    // device goes away, because the blocks hold memory this class owns.
    DestroyAliasedTransients();
    m_vkDevice = VK_NULL_HANDLE;
}

RenderGraphBuilder& RenderGraphBuilder::AddResource(const ResourceHandle& handle, ResourceDesc desc)
{
    const ResourceLifetime lifetime = std::visit([](const auto& d) { return d.lifetime; }, desc);
    if (lifetime == ResourceLifetime::Imported)
    {
        // AddResource means "the graph allocates this", so a desc asking to be Imported contradicts the
        // call. Reported here rather than at Build because it is a mistake at the line that made it.
        throw std::runtime_error("AddResource('" + handle +
                                 "') declares lifetime Imported, but AddResource is the call that makes the "
                                 "graph allocate a resource. Use ImportResource for one the graph does not own.");
    }
    ValidateResourceDesc(handle, desc);
    m_resourceDescRegistry[handle] = std::move(desc);
    return *this;
}

RenderGraphBuilder& RenderGraphBuilder::ImportResource(const ResourceHandle& handle, const IRenderResource* resource)
{
    m_importedResources[handle] = resource;
    return *this;
}

std::optional<ResourceDesc> RenderGraphBuilder::GetResourceDesc(const ResourceHandle& handle) const
{
    auto it = m_resourceDescRegistry.find(handle);
    return it != m_resourceDescRegistry.end() ? std::optional<ResourceDesc>{it->second} : std::nullopt;
}

const IRenderResource* RenderGraphBuilder::ResolveResource(const ResourceHandle& handle) const
{
    if (auto it = m_importedResources.find(handle); it != m_importedResources.end())
    {
        return it->second;
    }
    // Before the manager: an aliased buffer is not in the manager at all, and looking there first would
    // find a same-named resource from an earlier build if one ever existed.
    if (auto it = m_aliasedBuffers.find(handle); it != m_aliasedBuffers.end())
    {
        return it->second.get();
    }
    if (auto it = m_aliasedImages.find(handle); it != m_aliasedImages.end())
    {
        return it->second.get();
    }
    return m_services.resourceManager->template GetResource<IRenderResource>(handle);
}

namespace
{
/// @param desc A resource declaration.
/// @return True when this build gives the resource its own memory inside an aliasing block instead of
///         letting the resource manager allocate one for it. Buffers only: images still take the
///         manager's path, and the planner's image classes are ready for the slice that wires them.
bool IsAliasedTransient(const ResourceDesc& desc)
{
    return std::visit([](const auto& d) { return d.lifetime == ResourceLifetime::Transient; }, desc);
}

/// Destroys a half-compiled node's GPU objects if its compilation leaves by exception.
///
/// A guard rather than a destroy call beside each `throw`, because the failure path is the one nobody
/// exercises while writing a new declaration check: a check added here later is covered by construction,
/// whereas a check added beside a bare `throw` is covered only if its author remembers to add one. This is
/// #55's exception-safety half, and it is what lets a rejected declaration reach the caller as its own
/// message instead of as a leaked-object VUID raised inside `vkDestroyDevice` - which the debug callback
/// turns into an abort, so the message naming the node and resource is the one thing that never appears.
class HalfCompiledNodeGuard
{
public:
    HalfCompiledNodeGuard(CompiledRenderGraphNode& node, VkDevice device, VkDescriptorPool descriptorPool)
        : m_node(node), m_device(device), m_descriptorPool(descriptorPool)
    {
    }
    ~HalfCompiledNodeGuard()
    {
        if (!m_committed) m_node.Destroy(m_device, m_descriptorPool);
    }

    /// Hand the node to the caller: it is no longer destroyed when this scope exits.
    void Commit() { m_committed = true; }

    HalfCompiledNodeGuard(const HalfCompiledNodeGuard&) = delete;
    HalfCompiledNodeGuard& operator=(const HalfCompiledNodeGuard&) = delete;

private:
    CompiledRenderGraphNode& m_node;
    VkDevice m_device;
    VkDescriptorPool m_descriptorPool;
    bool m_committed = false;
};
}  // namespace

CompiledRenderGraphNode RenderGraphBuilder::CompileRenderGraphNode(const RenderGraphBuilder::RenderGraphNode& rgn)
{
    // Compile pipeline and pipeline layout from node
    CompiledRenderGraphNode result;
    // From here on the node is owed to this scope: if any declaration check below rejects it, the objects
    // already created for it are destroyed before the exception leaves.
    HalfCompiledNodeGuard releaseOnThrow(result, m_vkDevice, m_services.descriptorManager->GetDescriptorPool());
    // Copy the node's metadata: the compiled node carries everything needed at record time, so it
    // does not point back at the authoring structures.
    result.name = rgn.name;
    result.resourceUses = rgn.resourceUses;
    result.attachmentClearValues = rgn.attachmentClearValues;
    result.async = rgn.async;
    result.queueType = rgn.queueType;
    result.isRayTracing = (rgn.queueType == QueueType::RAY_TRACING);
    result.bindingPoint = (rgn.queueType == QueueType::COMPUTE)       ? VK_PIPELINE_BIND_POINT_COMPUTE
                          : (rgn.queueType == QueueType::RAY_TRACING) ? VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR
                                                                      : VK_PIPELINE_BIND_POINT_GRAPHICS;

    // Retrive and merge shader reflections
    std::vector<ShaderReflection> shaderReflections;
    std::vector<VkShaderModule> shaderModules;
    for (const auto& shaderKey : rgn.shaders)
    {
        if (shaderKey.IsValid())
        {
            const auto* shaderAsset = m_shaderAssetManager.GetShaderAsset(shaderKey);
            if (shaderAsset)
            {
                shaderReflections.push_back(shaderAsset->shaderReflection);
                shaderModules.push_back(shaderAsset->shaderModule);
            }
        }
    }
    // Ray tracing pipeline stages, in the order the RT builder expects (raygen, miss, hit).
    std::vector<VkShaderModule> rtShaderModules;
    for (const auto& shaderKey : rgn.rtShaders)
    {
        if (shaderKey.IsValid())
        {
            const auto* shaderAsset = m_shaderAssetManager.GetShaderAsset(shaderKey);
            if (shaderAsset)
            {
                shaderReflections.push_back(shaderAsset->shaderReflection);
                shaderModules.push_back(shaderAsset->shaderModule);
                rtShaderModules.push_back(shaderAsset->shaderModule);
            }
        }
    }
    result.execute = rgn.execute;

    if (shaderReflections.size() > 0)
    {
        ShaderReflection mergedReflection = MergeShaderReflections(shaderReflections);

        // Descriptor set layouts, derived from the shader reflection for *every* node. Graphics,
        // compute and ray tracing all take the same path now: a use names its (set, binding)
        // explicitly, reflection supplies the layout and the descriptor type, and the set is written
        // here at Build(). The engine's PER_VIEW/PER_OBJ/MATERIAL convention is a consumer concern
        // (RenderGraphSemanticBindings.h), not a graph one.
        BuildReflectionDescriptorSets(result, rgn, mergedReflection);

        std::vector<VkPushConstantRange> pushConstantRanges;
        for (const auto& pcRange : mergedReflection.pushConstantRanges)
        {
            pushConstantRanges.push_back(
                {.stageFlags = pcRange.stageFlags, .offset = pcRange.offset, .size = pcRange.size});
        }
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = static_cast<uint32_t>(result.descriptorSetLayouts.size()),
            .pSetLayouts = result.descriptorSetLayouts.empty() ? nullptr : result.descriptorSetLayouts.data(),
            .pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size()),
            .pPushConstantRanges = pushConstantRanges.empty() ? nullptr : pushConstantRanges.data()};
        VK_ASSERT(vkCreatePipelineLayout(m_vkDevice, &pipelineLayoutInfo, nullptr, &result.pipelineLayout));

        // Inspect the node's resolved resource uses to determine attachment formats.
        std::vector<VkFormat> colorAttachmentFormats;
        VkFormat depthAttachmentFormat = VK_FORMAT_UNDEFINED;
        for (const auto& resourceUse : rgn.resourceUses)
        {
            if (resourceUse.usage == ResourceUsage::COLOR_ATTACHMENT || resourceUse.usage == ResourceUsage::PRESENT)
            {
                // An imported image has no ResourceDesc, so its format comes from the resource. Without
                // this the render pass would be created for the default format and disagree with the
                // image it renders to (the swapchain's format for a present target).
                VkFormat format = resourceUse.format;
                if (format == VK_FORMAT_UNDEFINED)
                {
                    if (const auto* pImage = dynamic_cast<const ImageResource*>(ResolveResource(resourceUse.handle)))
                    {
                        format = pImage->GetImageFormat();
                    }
                }
                colorAttachmentFormats.push_back(format != VK_FORMAT_UNDEFINED ? format
                                                                               : VK_FORMAT_R16G16B16A16_SFLOAT);
            }
            else if (resourceUse.usage == ResourceUsage::DEPTH_STENCIL_ATTACHMENT)
            {
                depthAttachmentFormat =
                    resourceUse.format != VK_FORMAT_UNDEFINED ? resourceUse.format : VK_FORMAT_D32_SFLOAT;
            }
        }

        // Specialization constants. The ids are validated against the node's merged reflection first:
        // the driver ignores map entries for constants a shader does not declare, so a typo would leave
        // that constant on its baked default and surface as a wrong number far from the cause. The width
        // comes from reflection rather than from the caller for the same reason.
        std::vector<VkSpecializationMapEntry> specializationEntries;
        std::vector<uint64_t> specializationValues;
        specializationEntries.reserve(rgn.specializationConstants.size());
        specializationValues.reserve(rgn.specializationConstants.size());
        for (const SpecializationValue& supplied : rgn.specializationConstants)
        {
            const auto& reflected = mergedReflection.specializationConstants;
            const auto it = std::find_if(reflected.begin(), reflected.end(),
                                         [&supplied](const auto& constant) { return constant.id == supplied.id; });
            if (it == reflected.end())
            {
                throw std::runtime_error("Node '" + rgn.name + "' supplies specialization constant " +
                                         std::to_string(supplied.id) + ", which none of its shaders declares.");
            }
            if (it->size == 0 || it->size > sizeof(uint64_t))
            {
                throw std::runtime_error("Node '" + rgn.name + "' supplies specialization constant " +
                                         std::to_string(supplied.id) + " whose reflected size (" +
                                         std::to_string(it->size) + " bytes) is not 1-8 bytes.");
            }
            VkSpecializationMapEntry entry{};
            entry.constantID = supplied.id;
            entry.offset = static_cast<uint32_t>(specializationValues.size() * sizeof(uint64_t));
            entry.size = it->size;
            specializationEntries.push_back(entry);
            specializationValues.push_back(supplied.value);
        }
        // The entries address the values as a fixed-stride blob, of which Vulkan reads `size` bytes per
        // entry. It has to outlive the pipeline-creation calls below, which is why it lives here.
        VkSpecializationInfo specializationInfo{};
        specializationInfo.mapEntryCount = static_cast<uint32_t>(specializationEntries.size());
        specializationInfo.pMapEntries = specializationEntries.empty() ? nullptr : specializationEntries.data();
        specializationInfo.dataSize = specializationValues.size() * sizeof(uint64_t);
        specializationInfo.pData = specializationValues.empty() ? nullptr : specializationValues.data();
        const VkSpecializationInfo* pSpecializationInfo = specializationEntries.empty() ? nullptr : &specializationInfo;

        VkPipelineRenderingCreateInfo renderingInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = static_cast<uint32_t>(colorAttachmentFormats.size()),
            .pColorAttachmentFormats = colorAttachmentFormats.empty() ? nullptr : colorAttachmentFormats.data(),
            .depthAttachmentFormat = depthAttachmentFormat,
        };

        if (rgn.queueType == QueueType::GRAPHICS)
        {
            result.bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            // Create pipeline
            result.pipeline = CreatePipelineFromPSODesc(rgn.psoDesc, m_vkDevice, shaderModules, result.pipelineLayout,
                                                        renderingInfo, pSpecializationInfo);
        }
        else if (rgn.queueType == QueueType::COMPUTE)
        {
            result.bindingPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
            VkPipelineShaderStageCreateInfo stageInfo{};
            stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            stageInfo.module = shaderModules.front();
            stageInfo.pName = "main";
            stageInfo.pSpecializationInfo = pSpecializationInfo;
            VkComputePipelineCreateInfo pipelineInfo{};
            pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            pipelineInfo.stage = stageInfo;
            pipelineInfo.layout = result.pipelineLayout;
            VK_ASSERT(
                vkCreateComputePipelines(m_vkDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &result.pipeline));
        }
        else if (rgn.queueType == QueueType::RAY_TRACING)
        {
            // Refused rather than ignored: the ray tracing pipeline path does not apply specialization
            // constants yet, and silently keeping the baked defaults is the failure this whole change
            // exists to remove. Tracked by #62.
            if (!rgn.specializationConstants.empty())
            {
                throw std::runtime_error("Ray tracing node '" + rgn.name +
                                         "' supplies specialization constants, which the ray tracing pipeline path "
                                         "does not apply yet (#62).");
            }
            result.bindingPoint = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
            BuildRayTracingPipeline(result, rgn, rtShaderModules);

            // Trace extent: use the first storage image the node writes to.
            for (const auto& resourceUse : rgn.resourceUses)
            {
                if (resourceUse.usage == ResourceUsage::STORAGE_IMAGE && resourceUse.extent.width > 0)
                {
                    result.traceExtent = {resourceUse.extent.width, resourceUse.extent.height};
                    break;
                }
            }
        }
    }

    releaseOnThrow.Commit();
    return result;
}

void RenderGraphBuilder::BuildReflectionDescriptorSets(CompiledRenderGraphNode& rgn, const RenderGraphNode& logicalNode,
                                                       const ShaderReflection& mergedReflection)
{
    if (mergedReflection.descriptorBindings.empty()) return;

    // Group the reflected bindings by set index.
    uint32_t maxSet = 0;
    for (const auto& binding : mergedReflection.descriptorBindings)
    {
        maxSet = std::max(maxSet, binding.set);
    }
    const uint32_t setCount = maxSet + 1;

    std::vector<std::vector<VkDescriptorSetLayoutBinding>> bindingsPerSet(setCount);
    for (const auto& binding : mergedReflection.descriptorBindings)
    {
        bindingsPerSet[binding.set].push_back({.binding = binding.binding,
                                               .descriptorType = binding.type,
                                               .descriptorCount = binding.count,
                                               .stageFlags = binding.stageFlags,
                                               .pImmutableSamplers = nullptr});
    }
    // Canonical order, so the cache key is stable: reflection does not promise one.
    for (auto& bindings : bindingsPerSet)
    {
        std::sort(bindings.begin(), bindings.end(),
                  [](const VkDescriptorSetLayoutBinding& a, const VkDescriptorSetLayoutBinding& b)
                  { return a.binding < b.binding; });
    }

    rgn.descriptorSetLayouts.resize(setCount, VK_NULL_HANDLE);
    rgn.descriptorSets.resize(setCount, VK_NULL_HANDLE);
    // The cache owns the layouts and sets now, so the node must not free them.
    rgn.ownsDescriptorSets = false;
    rgn.ownsDescriptorSetLayouts = false;

    // Bind each set by content: two nodes with the same reflected layout and the same bound resources
    // share one set, so a bindless array is paid once rather than once per node (#15, #16 criterion 4).
    for (uint32_t set = 0; set < setCount; ++set)
    {
        std::vector<DescriptorSetKey::Bound> bound;
        for (const auto& use : logicalNode.resourceUses)
        {
            if (!use.descriptorBinding.has_value() || use.descriptorBinding->set != set) continue;
            const IRenderResource* pResource = ResolveResource(use.handle);
            if (pResource == nullptr) continue;

            // The descriptor type comes from the reflection entry that matches this set/binding.
            VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
            for (const auto& reflected : mergedReflection.descriptorBindings)
            {
                if (reflected.set == set && reflected.binding == use.descriptorBinding->binding)
                {
                    descriptorType = reflected.type;
                    break;
                }
            }
            if (descriptorType == VK_DESCRIPTOR_TYPE_MAX_ENUM) continue;

            DescriptorSetKey::Bound b{
                .binding = use.descriptorBinding->binding, .type = descriptorType, .resource = pResource};
            if (const auto* pBuffer = dynamic_cast<const BufferResource*>(pResource))
            {
                const auto [nOffset, nRange] = ResolveBufferRange(logicalNode.name, use, *pBuffer, descriptorType);
                b.bufferOffset = nOffset;
                b.bufferRange = nRange;
            }
            else if (const auto* pImage = dynamic_cast<const ImageResource*>(pResource))
            {
                b.imageLayout = (descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                                    ? VK_IMAGE_LAYOUT_GENERAL
                                    : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                if (descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                {
                    // A combined image sampler needs a sampler, which a plain ImageResource does not carry.
                    // Only a TextureResource has one, and a combined binding to anything else is a
                    // declaration error rather than a descriptor written with a null sampler.
                    const auto* pTexture = dynamic_cast<const TextureResource*>(pImage);
                    if (pTexture == nullptr)
                    {
                        throw std::runtime_error("Node '" + logicalNode.name + "' binds image '" + use.handle +
                                                 "' to a combined image sampler, but the resource carries no "
                                                 "sampler. Import a TextureResource, or bind a sampled image instead.");
                    }
                    b.sampler = pTexture->getSamper();
                }
            }
            else if (dynamic_cast<const AccelerationStructure*>(pResource) == nullptr)
            {
                continue;
            }
            bound.push_back(b);
        }
        std::sort(bound.begin(), bound.end());

        const DescriptorSetKey key{.set = set, .layout = bindingsPerSet[set], .bound = bound};
        const DescriptorSetCache::Entry entry = m_descriptorSetCache.Acquire(
            key, [this, &bound](VkDescriptorSet descriptorSet) { WriteDescriptorSetBindings(descriptorSet, bound); });
        rgn.descriptorSetLayouts[set] = entry.layout;
        rgn.descriptorSets[set] = entry.set;
    }
}

void RenderGraphBuilder::WriteDescriptorSetBindings(VkDescriptorSet descriptorSet,
                                                    const std::vector<DescriptorSetKey::Bound>& bound)
{
    if (bound.empty()) return;

    // Every pNext/pBufferInfo/pImageInfo below points into these vectors, which stay put for the single
    // vkUpdateDescriptorSets call. Reserving up front means the addresses are stable even if a push
    // would otherwise reallocate.
    std::vector<VkWriteDescriptorSet> writes(bound.size());
    std::vector<VkDescriptorBufferInfo> bufferInfos(bound.size());
    std::vector<VkDescriptorImageInfo> imageInfos(bound.size());
    std::vector<VkWriteDescriptorSetAccelerationStructureKHR> asWrites(
        bound.size(), VkWriteDescriptorSetAccelerationStructureKHR{
                          VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR});

    for (size_t i = 0; i < bound.size(); ++i)
    {
        const DescriptorSetKey::Bound& b = bound[i];
        VkWriteDescriptorSet& write = writes[i];
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSet;
        write.dstBinding = b.binding;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = b.type;

        if (const auto* pAccel = dynamic_cast<const AccelerationStructure*>(b.resource))
        {
            asWrites[i].accelerationStructureCount = 1;
            asWrites[i].pAccelerationStructures = &pAccel->GetAccelerationStructure();
            write.pNext = &asWrites[i];
        }
        else if (const auto* pBuffer = dynamic_cast<const BufferResource*>(b.resource))
        {
            bufferInfos[i].buffer = pBuffer->buffer();
            bufferInfos[i].offset = b.bufferOffset;
            bufferInfos[i].range = b.bufferRange;
            write.pBufferInfo = &bufferInfos[i];
        }
        else if (const auto* pImage = dynamic_cast<const ImageResource*>(b.resource))
        {
            imageInfos[i].imageView = pImage->getView();
            imageInfos[i].imageLayout = b.imageLayout;
            imageInfos[i].sampler = b.sampler;
            write.pImageInfo = &imageInfos[i];
        }
    }

    vkUpdateDescriptorSets(m_vkDevice, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void RenderGraphBuilder::BuildRayTracingPipeline(CompiledRenderGraphNode& rgn, const RenderGraphNode& logicalNode,
                                                 const std::vector<VkShaderModule>& shaderModules)
{
    if (shaderModules.size() < 3)
    {
        throw std::runtime_error("Ray tracing node '" + logicalNode.name +
                                 "' needs raygen, miss and closest-hit shaders");
    }

    RayTracingPipelineBuilder builder;
    builder.AddShaderModule(shaderModules[0], VK_SHADER_STAGE_RAYGEN_BIT_KHR)
        .AddShaderModule(shaderModules[1], VK_SHADER_STAGE_MISS_BIT_KHR)
        .AddShaderModule(shaderModules[2], VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);

    builder.SetPipelineLayout(rgn.pipelineLayout).SetMaxRecursionDepth(1);
    VkRayTracingPipelineCreateInfoKHR createInfo = builder.Build();
    VK_ASSERT(VkExt::vkCreateRayTracingPipelinesKHR(m_vkDevice, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &createInfo, nullptr,
                                                    &rgn.pipeline));

    // ── Shader binding table ──────────────────────────────────────────────
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
    m_services.device->GetPhysicalDeviceProperties(rtProps);

    const uint32_t nHandleSize = rtProps.shaderGroupHandleSize;
    const uint32_t nHandleSizeAligned = AlignUp(nHandleSize, rtProps.shaderGroupHandleAlignment);

    // 1 raygen, 1 miss, 1 hit group.
    VkStridedDeviceAddressRegionKHR& rgenRegion = rgn.sbtRegions[0];
    VkStridedDeviceAddressRegionKHR& missRegion = rgn.sbtRegions[1];
    VkStridedDeviceAddressRegionKHR& hitRegion = rgn.sbtRegions[2];

    rgenRegion.stride = AlignUp(nHandleSizeAligned, rtProps.shaderGroupBaseAlignment);
    rgenRegion.size = rgenRegion.stride;
    missRegion.stride = nHandleSizeAligned;
    missRegion.size = AlignUp(nHandleSizeAligned, rtProps.shaderGroupBaseAlignment);
    hitRegion.stride = nHandleSizeAligned;
    hitRegion.size = AlignUp(nHandleSizeAligned, rtProps.shaderGroupBaseAlignment);

    const uint32_t sbtSize = rgenRegion.size + missRegion.size + hitRegion.size;
    const uint32_t nHandleCount = 3;
    std::vector<uint8_t> handles(nHandleCount * nHandleSize);
    VK_ASSERT(VkExt::vkGetRayTracingShaderGroupHandlesKHR(m_vkDevice, rgn.pipeline, 0, nHandleCount, handles.size(),
                                                          handles.data()));

    ShaderBindingTableBuffer* pSBT =
        m_services.resourceManager->GetShaderBindingTableBuffer("SBT_" + logicalNode.name, sbtSize);
    const VkDeviceAddress sbtAddress = m_services.device->GetBufferDeviceAddress(pSBT->buffer());
    rgenRegion.deviceAddress = sbtAddress;
    missRegion.deviceAddress = sbtAddress + rgenRegion.size;
    hitRegion.deviceAddress = sbtAddress + rgenRegion.size + missRegion.size;

    uint8_t* pMapped = static_cast<uint8_t*>(pSBT->Map());
    memcpy(pMapped, handles.data() + 0 * nHandleSize, nHandleSize);
    memcpy(pMapped + rgenRegion.size, handles.data() + 1 * nHandleSize, nHandleSize);
    memcpy(pMapped + rgenRegion.size + missRegion.size, handles.data() + 2 * nHandleSize, nHandleSize);
    pSBT->Unmap();
}

namespace
{
/// The allocation usage a semantic role requires, and the kinds that admit it.
///
/// A role selects a barrier policy, but it also promises something about the allocation: `STORAGE_IMAGE`
/// needs the image created with `STORAGE_BIT`, `UNIFORM_BUFFER` needs `UNIFORM_BUFFER_BIT`. Declaring the
/// role without the bit reaches the driver as an invalid usage for the resource, so the pair is checked
/// against the declaration here (#128).
struct UsageAllocationRequirement
{
    bool bBuffer = false;               ///< The role is valid for a buffer.
    bool bImage = false;                ///< The role is valid for an image.
    const char* bufferFlagName = "";    ///< Symbolic name of the buffer bit, for messages.
    VkBufferUsageFlags bufferFlag = 0;  ///< Bit a buffer must carry.
    const char* imageFlagName = "";     ///< Symbolic name of the image bit, for messages.
    VkImageUsageFlags imageFlag = 0;    ///< Bit an image must carry.
};

/// @param usage The role a node declares.
/// @return What it requires of an allocation, or nothing when the role carries no allocation flag
///         (payloads, push constants) and therefore has nothing to check.
std::optional<UsageAllocationRequirement> GetUsageAllocationRequirement(ResourceUsage usage)
{
    switch (usage)
    {
        case ResourceUsage::SAMPLED:
            return UsageAllocationRequirement{
                .bImage = true, .imageFlagName = "VK_IMAGE_USAGE_SAMPLED_BIT", .imageFlag = VK_IMAGE_USAGE_SAMPLED_BIT};
        case ResourceUsage::STORAGE_IMAGE:
            return UsageAllocationRequirement{
                .bImage = true, .imageFlagName = "VK_IMAGE_USAGE_STORAGE_BIT", .imageFlag = VK_IMAGE_USAGE_STORAGE_BIT};
        case ResourceUsage::COLOR_ATTACHMENT:
            return UsageAllocationRequirement{.bImage = true,
                                              .imageFlagName = "VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT",
                                              .imageFlag = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};
        case ResourceUsage::DEPTH_STENCIL_ATTACHMENT:
            return UsageAllocationRequirement{.bImage = true,
                                              .imageFlagName = "VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT",
                                              .imageFlag = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT};
        case ResourceUsage::INPUT_ATTACHMENT:
            return UsageAllocationRequirement{.bImage = true,
                                              .imageFlagName = "VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT",
                                              .imageFlag = VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT};
        case ResourceUsage::RESOLVE_ATTACHMENT:
        case ResourceUsage::PRESENT:
            return UsageAllocationRequirement{.bImage = true,
                                              .imageFlagName = "VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT",
                                              .imageFlag = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};
        case ResourceUsage::UNIFORM_BUFFER:
            return UsageAllocationRequirement{.bBuffer = true,
                                              .bufferFlagName = "VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT",
                                              .bufferFlag = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT};
        case ResourceUsage::STORAGE_BUFFER:
            return UsageAllocationRequirement{.bBuffer = true,
                                              .bufferFlagName = "VK_BUFFER_USAGE_STORAGE_BUFFER_BIT",
                                              .bufferFlag = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
        case ResourceUsage::VERTEX_BUFFER:
            return UsageAllocationRequirement{.bBuffer = true,
                                              .bufferFlagName = "VK_BUFFER_USAGE_VERTEX_BUFFER_BIT",
                                              .bufferFlag = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT};
        case ResourceUsage::INDEX_BUFFER:
            return UsageAllocationRequirement{.bBuffer = true,
                                              .bufferFlagName = "VK_BUFFER_USAGE_INDEX_BUFFER_BIT",
                                              .bufferFlag = VK_BUFFER_USAGE_INDEX_BUFFER_BIT};
        case ResourceUsage::INDIRECT_BUFFER:
        case ResourceUsage::DRAW_COMMAND_BUFFER:
            return UsageAllocationRequirement{.bBuffer = true,
                                              .bufferFlagName = "VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT",
                                              .bufferFlag = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT};
        case ResourceUsage::TRANSFER_SOURCE:
            return UsageAllocationRequirement{.bBuffer = true,
                                              .bImage = true,
                                              .bufferFlagName = "VK_BUFFER_USAGE_TRANSFER_SRC_BIT",
                                              .bufferFlag = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                              .imageFlagName = "VK_IMAGE_USAGE_TRANSFER_SRC_BIT",
                                              .imageFlag = VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
        case ResourceUsage::TRANSFER_DEST:
            return UsageAllocationRequirement{.bBuffer = true,
                                              .bImage = true,
                                              .bufferFlagName = "VK_BUFFER_USAGE_TRANSFER_DST_BIT",
                                              .bufferFlag = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                              .imageFlagName = "VK_IMAGE_USAGE_TRANSFER_DST_BIT",
                                              .imageFlag = VK_IMAGE_USAGE_TRANSFER_DST_BIT};
        case ResourceUsage::ACCEL_STRUCTURE:
        case ResourceUsage::TASK_PAYLOAD:
        case ResourceUsage::MESH_PAYLOAD:
        case ResourceUsage::WORKGRAPH_NODE:
        case ResourceUsage::PUSH_CONSTANTS:
            return std::nullopt;
    }
    return std::nullopt;
}

/// @param kind A resource kind.
/// @return Its lower-case name, for messages.
const char* NameOfKind(ResourceKind kind)
{
    switch (kind)
    {
        case ResourceKind::BUFFER:
            return "buffer";
        case ResourceKind::IMAGE:
            return "image";
        case ResourceKind::ACCELERATION_STRUCTURE:
            return "acceleration structure";
        case ResourceKind::SAMPLER:
            return "sampler";
    }
    return "(unknown kind)";
}

/// @param kind A resource kind.
/// @return The indefinite article that reads correctly before `NameOfKind(kind)`.
const char* ArticleForKind(ResourceKind kind)
{
    return (kind == ResourceKind::IMAGE || kind == ResourceKind::ACCELERATION_STRUCTURE) ? "an " : "a ";
}

/// @param resource An imported resource.
/// @return The kind a use has to declare for it.
ResourceKind KindOfResource(const IRenderResource* resource)
{
    if (dynamic_cast<const BufferResource*>(resource) != nullptr) return ResourceKind::BUFFER;
    if (dynamic_cast<const ImageResource*>(resource) != nullptr) return ResourceKind::IMAGE;
    if (dynamic_cast<const AccelerationStructure*>(resource) != nullptr) return ResourceKind::ACCELERATION_STRUCTURE;
    throw std::runtime_error("The imported resource is neither a buffer, an image nor an acceleration structure.");
}

/// Check a use against the resource it names (#128).
///
/// @param nodeName Node declaring the use, for the message.
/// @param use      The use.
/// @param desc     The declared resource's description, or nothing when the resource is imported.
/// @param imported The imported resource, or null when the resource is declared.
void ValidateUseAgainstResource(const std::string& nodeName, const ResourceUse& use,
                                const std::optional<ResourceDesc>& desc, const IRenderResource* imported)
{
    ResourceKind resourceKind = ResourceKind::BUFFER;
    if (desc.has_value())
    {
        resourceKind = std::holds_alternative<BufferResourceDesc>(*desc) ? ResourceKind::BUFFER : ResourceKind::IMAGE;
    }
    else if (imported != nullptr)
    {
        resourceKind = KindOfResource(imported);
    }

    // U1: the kind a use declares must be the kind the resource actually is.
    if (use.kind != resourceKind)
    {
        throw std::runtime_error("Node '" + nodeName + "' uses resource '" + use.handle + "' as " +
                                 ArticleForKind(use.kind) + NameOfKind(use.kind) + ", but the resource is " +
                                 ArticleForKind(resourceKind) + NameOfKind(resourceKind) + ".");
    }

    // U2: the role must admit that kind, and a declared resource must carry the allocation bit it implies.
    const std::optional<UsageAllocationRequirement> requirement = GetUsageAllocationRequirement(use.usage);
    if (!requirement.has_value()) return;

    const bool bAdmitsKind = resourceKind == ResourceKind::BUFFER ? requirement->bBuffer : requirement->bImage;
    if (!bAdmitsKind)
    {
        throw std::runtime_error("Node '" + nodeName + "' uses " + NameOfKind(resourceKind) + " '" + use.handle +
                                 "' with usage " + NameOf(use.usage) + ", which is not a " + NameOfKind(resourceKind) +
                                 " usage.");
    }

    // An imported resource carries no desc, so its usage flags are the caller's promise; the kind checks
    // above are what the graph can verify for it.
    if (!desc.has_value()) return;

    if (resourceKind == ResourceKind::BUFFER)
    {
        const auto& bufferDesc = std::get<BufferResourceDesc>(*desc);
        if ((bufferDesc.usage & requirement->bufferFlag) == 0)
        {
            throw std::runtime_error("Node '" + nodeName + "' uses buffer '" + use.handle + "' as " +
                                     NameOf(use.usage) + ", but it was declared without " +
                                     requirement->bufferFlagName + ".");
        }
    }
    else
    {
        const auto& imageDesc = std::get<ImageResourceDesc>(*desc);
        if ((imageDesc.usage & requirement->imageFlag) == 0)
        {
            throw std::runtime_error("Node '" + nodeName + "' uses image '" + use.handle + "' as " + NameOf(use.usage) +
                                     ", but it was declared without " + requirement->imageFlagName + ".");
        }
    }
}
}  // namespace

void RenderGraphBuilder::AddNode(const RenderGraphNodeCreateInfo& nodeCreateInfo)
{
    const std::string& nodeName = nodeCreateInfo.nodeName;

    if (m_renderGraphNodes.find(nodeName) != m_renderGraphNodes.end())
    {
        throw std::runtime_error("Node with name '" + nodeCreateInfo.nodeName +
                                 "' already exists in the render graph.");
    }

    m_renderGraphNodes[nodeName] = {.name = nodeName};
    m_dependencyGraph.AddNode(nodeName);

    auto& rgn = m_renderGraphNodes.at(nodeName);
    rgn.queueType = nodeCreateInfo.queueType;
    rgn.async = nodeCreateInfo.async;
    rgn.costHint = nodeCreateInfo.costHint;
    rgn.attachmentClearValues = nodeCreateInfo.attachmentClearValues;
    rgn.specializationConstants = nodeCreateInfo.specializationConstants;

    // Load shaders
    if (nodeCreateInfo.shaderNames.size() > static_cast<size_t>(MAX_SHADER_STAGES))
    {
        throw std::runtime_error("Node '" + nodeName + "' declares more shaders than MAX_SHADER_STAGES.");
    }
    int shaderIdx = 0;
    for (const auto& shaderName : nodeCreateInfo.shaderNames)
    {
        auto key = m_shaderAssetManager.LoadShader(shaderName);
        if (!key)
        {
            // Skipping the stage here produced a pipeline compiled without it, which failed much later
            // at draw time with a descriptor or push-constant mismatch far from the cause. A shader the
            // node asked for and the build did not produce is a hard error.
            throw std::runtime_error("Node '" + nodeName + "' declares shader '" + shaderName +
                                     "', which could not be loaded.");
        }
        rgn.shaders[shaderIdx++] = key.value();
    }

    // Load ray tracing shaders (raygen / miss / closest hit).
    if (nodeCreateInfo.rtShaderNames.size() > rgn.rtShaders.size())
    {
        throw std::runtime_error("Node '" + nodeName + "' declares more ray tracing shaders than the " +
                                 std::to_string(rgn.rtShaders.size()) + " supported (raygen / miss / closest hit).");
    }
    for (size_t i = 0; i < nodeCreateInfo.rtShaderNames.size(); ++i)
    {
        auto key = m_shaderAssetManager.LoadShader(nodeCreateInfo.rtShaderNames[i]);
        if (!key)
        {
            throw std::runtime_error("Ray tracing node '" + nodeName + "' declares shader '" +
                                     nodeCreateInfo.rtShaderNames[i] + "', which could not be loaded.");
        }
        rgn.rtShaders[i] = key.value();
    }

    // Resolve resource uses
    for (const auto& resourceUse : nodeCreateInfo.resourceUses)
    {
        if (m_resourceDescRegistry.find(resourceUse.handle) == m_resourceDescRegistry.end() &&
            m_importedResources.find(resourceUse.handle) == m_importedResources.end())
        {
            throw std::runtime_error("Node '" + nodeName + "' uses resource '" + resourceUse.handle +
                                     "' which is neither declared with AddResource nor imported.");
        }
        const std::optional<ResourceDesc> resourceDesc = GetResourceDesc(resourceUse.handle);
        const IRenderResource* pImportedResource =
            resourceDesc.has_value() ? nullptr : m_importedResources.at(resourceUse.handle);
        ValidateUseAgainstResource(nodeName, resourceUse, resourceDesc, pImportedResource);
        ResolvedResourceUse resolved = ResolveResourceUse(resourceUse);
        // A range on a buffer bound into a descriptor is validated in
        // `BuildReflectionDescriptorSets`, which knows the descriptor type from reflection; that runs
        // at Build() too, so a bad range still fails there rather than mid-record in Execute.
        // Fill in allocation-time properties (format / extent) so pipeline + barrier generation can use them.
        if (const auto desc = GetResourceDesc(resourceUse.handle))
        {
            if (const auto* imageDesc = std::get_if<ImageResourceDesc>(&*desc))
            {
                resolved.format = imageDesc->format;
                resolved.extent = {imageDesc->extent.width, imageDesc->extent.height, 1};
            }
        }
        else if (resourceUse.kind == ResourceKind::IMAGE)
        {
            // An imported image has no ResourceDesc, so its format and extent come from the resource
            // itself. Without them the render pass and the render area would be built from zeros.
            if (const auto* pImage = dynamic_cast<const ImageResource*>(ResolveResource(resourceUse.handle)))
            {
                resolved.format = pImage->GetImageFormat();
                const VkExtent2D extent = pImage->GetImageExtent();
                resolved.extent = {extent.width, extent.height, 1};
            }
        }
        rgn.resourceUses.push_back(std::move(resolved));
    }
    rgn.psoDesc = nodeCreateInfo.psoDesc;
    rgn.execute = nodeCreateInfo.execute;
}

void RenderGraphBuilder::AddDependency(const std::string& fromNode, const std::string& toNode)
{
    if (m_renderGraphNodes.find(fromNode) == m_renderGraphNodes.end())
    {
        throw std::runtime_error("Node '" + fromNode + "' does not exist in the render graph.");
    }

    if (m_renderGraphNodes.find(toNode) == m_renderGraphNodes.end())
    {
        throw std::runtime_error("Node '" + toNode + "' does not exist in the render graph.");
    }

    // CPU nodes are executed host-side before any GPU segment is submitted, so they cannot
    // consume GPU results. Reject the ordering rather than silently running it too early.
    if (m_renderGraphNodes.at(toNode).queueType == QueueType::CPU &&
        m_renderGraphNodes.at(fromNode).queueType != QueueType::CPU)
    {
        throw std::runtime_error("Dependency '" + fromNode + "' -> '" + toNode +
                                 "': CPU nodes run before GPU work is submitted and cannot depend on a GPU node.");
    }

    if (!m_dependencyGraph.AddEdge(fromNode, toNode))
    {
        throw std::runtime_error("Adding dependency from '" + fromNode + "' to '" + toNode +
                                 "' creates a cycle in the render graph.");
    }
}

void RenderGraphBuilder::ValidateTransientLifetimes(const std::vector<std::string>& executionOrder)
{
    m_transientLiveRanges.clear();
    std::unordered_set<ResourceHandle> written;

    for (size_t nNode = 0; nNode < executionOrder.size(); ++nNode)
    {
        const RenderGraphNode& node = m_renderGraphNodes.at(executionOrder[nNode]);
        for (const ResolvedResourceUse& use : node.resourceUses)
        {
            // Imported resources belong to the caller, so their content is not this execution's to
            // produce. That is the whole difference between an import and a graph-owned resource.
            if (m_importedResources.find(use.handle) != m_importedResources.end()) continue;

            const std::optional<ResourceDesc> desc = GetResourceDesc(use.handle);
            if (!desc.has_value()) continue;
            if (std::visit([](const auto& d) { return d.lifetime; }, *desc) != ResourceLifetime::Transient)
            {
                continue;
            }

            // TRANSFER_WRITE counts: a copy or an upload is a write like any other, and leaving it out
            // rejected a graph in which one node's transfer fills a transient and the next reads it -
            // which is what transfer roles are for. Found by the transfer round trip test, and it was
            // reachable before that through DRAW_COMMAND_BUFFER with TRANSFER_WRITE, which no test used.
            const bool bWrites = use.io == ResourceIOType::WRITE || use.io == ResourceIOType::READ_WRITE ||
                                 use.io == ResourceIOType::TRANSFER_WRITE;
            if (!bWrites && written.find(use.handle) == written.end())
            {
                // Named, because the whole value of the check is telling the author which line is wrong.
                // The message also names the escape, since the mistake is usually a missing Persistent.
                throw std::runtime_error(
                    "Node '" + executionOrder[nNode] + "' reads transient resource '" + use.handle +
                    "' with nothing writing it first. A transient's content is valid only from its first "
                    "write within one execution, so this read would take whatever the memory happens to "
                    "hold. Declare it Persistent if it is meant to carry content across executions.");
            }
            if (bWrites)
            {
                written.insert(use.handle);
            }

            // The live interval. A read cannot come before the first write in a graph that passed the
            // check above, so the first use is a write and the two definitions coincide here.
            auto it = m_transientLiveRanges.find(use.handle);
            if (it == m_transientLiveRanges.end())
            {
                m_transientLiveRanges.emplace(use.handle, TransientLiveRange{nNode, nNode});
            }
            else
            {
                it->second.lastUse = nNode;
            }
        }
    }
}

void RenderGraphBuilder::ValidateDescriptorBindings(const std::vector<std::string>& executionOrder)
{
    for (const std::string& nodeName : executionOrder)
    {
        const RenderGraphNode& node = m_renderGraphNodes.at(nodeName);

        // Merge the node's reflected bindings. AddNode already loaded the shader assets, so this needs no
        // device and can run before any resource exists.
        std::vector<ShaderReflection> reflections;
        const auto collectReflections = [&](const auto& shaderKeys)
        {
            for (const ShaderKey& shaderKey : shaderKeys)
            {
                if (!shaderKey.IsValid()) continue;
                if (const auto* pAsset = m_shaderAssetManager.GetShaderAsset(shaderKey))
                {
                    reflections.push_back(pAsset->shaderReflection);
                }
            }
        };
        collectReflections(node.shaders);
        collectReflections(node.rtShaders);
        const ShaderReflection merged = MergeShaderReflections(reflections);

        for (const ResolvedResourceUse& use : node.resourceUses)
        {
            if (!use.descriptorBinding.has_value()) continue;
            const uint32_t nSet = use.descriptorBinding->set;
            const uint32_t nBinding = use.descriptorBinding->binding;

            const bool bDeclared = std::any_of(merged.descriptorBindings.begin(), merged.descriptorBindings.end(),
                                               [nSet, nBinding](const ShaderReflection::DescriptorBinding& reflected)
                                               { return reflected.set == nSet && reflected.binding == nBinding; });
            if (bDeclared) continue;

            // Name the binding and list what the shader does declare: the whole value of the check is
            // telling the author which line is wrong, without a history search or a debugger.
            std::string declared;
            for (const auto& reflected : merged.descriptorBindings)
            {
                if (!declared.empty()) declared += ", ";
                declared += "set " + std::to_string(reflected.set) + " binding " + std::to_string(reflected.binding);
            }
            throw std::runtime_error("Node '" + nodeName + "' binds resource '" + use.handle + "' at set " +
                                     std::to_string(nSet) + ", binding " + std::to_string(nBinding) +
                                     ", but its shader declares no descriptor there. Declared bindings: " +
                                     (declared.empty() ? std::string("none") : declared) + ".");
        }
    }
}

void RenderGraphBuilder::DestroyAliasedTransients()
{
    // Resources first, then their memory: a block cannot be released while a resource is still bound to
    // it, which is the order VMA's own implementation uses (destroy the buffer, then free).
    m_aliasedBuffers.clear();
    m_aliasedImages.clear();
    for (VmaAllocation& block : m_aliasBlocks) GetMemoryAllocator()->FreeAliasingBlock(block);
    m_aliasBlocks.clear();
    m_transientMemoryPlan = TransientMemoryPlan{};
}

void RenderGraphBuilder::BuildAliasedTransients(const std::vector<std::string>& executionOrder)
{
    std::vector<TransientMemoryRequest> requests;
    requests.reserve(m_resourceDescRegistry.size());

    // A resource may share a slot only with one whose uses run on the same queue family. Within a family
    // the graph's own barriers order the hand-over; across families nothing does, because the executor
    // inserts a wait only for a *shared handle* and an aliased pair is two handles. So each resource is
    // given the family its GPU uses run on, or none for a host-written one, and one used on both families
    // is marked as sharing with nothing.
    std::unordered_map<ResourceHandle, uint32_t> transientFamily;
    std::unordered_set<ResourceHandle> spansFamilies;
    for (const std::string& nodeName : executionOrder)
    {
        const RenderGraphNode& node = m_renderGraphNodes.at(nodeName);
        if (node.queueType == QueueType::CPU) continue;
        const uint32_t family = GetQueueFamilyForType(GetQueueKey(node.queueType, node.async));
        for (const ResolvedResourceUse& use : node.resourceUses)
        {
            const auto [it, inserted] = transientFamily.emplace(use.handle, family);
            if (!inserted && it->second != family) spansFamilies.insert(use.handle);
        }
    }

    for (const auto& [handle, desc] : m_resourceDescRegistry)
    {
        if (!IsAliasedTransient(desc)) continue;
        if (m_importedResources.find(handle) != m_importedResources.end()) continue;

        const auto liveRange = m_transientLiveRanges.find(handle);
        if (liveRange == m_transientLiveRanges.end()) continue;

        VkMemoryRequirements requirements = {};
        TransientMemoryClass classKey;

        if (const auto* pBufferDesc = std::get_if<BufferResourceDesc>(&desc))
        {
            const VkDeviceSize nSize = pBufferDesc->count * pBufferDesc->stride;
            auto resource =
                std::make_unique<TransientBufferResource>(nSize, pBufferDesc->usage, pBufferDesc->memoryProperties);
            VkBuffer buffer = VK_NULL_HANDLE;
            GetMemoryAllocator()->CreateUnboundBuffer(nSize, pBufferDesc->usage, buffer);
            resource->Adopt(buffer);
            requirements = GetMemoryAllocator()->QueryBufferRequirements(buffer);
            classKey = ClassOf(desc, requirements.memoryTypeBits);
            m_aliasedBuffers.emplace(handle, std::move(resource));
        }
        else
        {
            const auto& imageDesc = std::get<ImageResourceDesc>(desc);
            // The same usage computation the resource manager uses, from the same function: an image's
            // aliasing class is its create info, so the two paths must not be able to disagree.
            const VkImageUsageFlags usage = EffectiveImageUsageFlags(imageDesc.format, imageDesc.usage);
            auto resource = std::make_unique<TransientImageResource>(imageDesc, usage);
            VkImage image = VK_NULL_HANDLE;
            GetMemoryAllocator()->CreateUnboundImage(resource->ImageInfoForCreate(), image);
            resource->Adopt(image);
            requirements = GetMemoryAllocator()->QueryImageRequirements(image);
            classKey = ClassOf(desc, requirements.memoryTypeBits);
            // Images have no memory usage in their declaration, and a render target's storage is
            // device-local by definition, so the block's usage is not something the caller chose here.
            classKey.memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY;
            m_aliasedImages.emplace(handle, std::move(resource));
        }

        TransientMemoryRequest request;
        request.handle = handle;
        request.classKey = classKey;
        request.size = requirements.size;
        request.alignment = requirements.alignment;
        request.memoryTypeBits = requirements.memoryTypeBits;
        request.firstUse = liveRange->second.firstUse;
        request.lastUse = liveRange->second.lastUse;
        // Domain 0 is "no GPU use", so a single family is stored one-based to keep the two apart.
        const auto familyIt = transientFamily.find(handle);
        request.sharingDomain = (familyIt == transientFamily.end()) ? 0u : static_cast<uint64_t>(1u) + familyIt->second;
        request.canShareSlot = spansFamilies.find(handle) == spansFamilies.end();
        requests.push_back(std::move(request));
    }

    m_transientMemoryPlan = PlanTransientMemory(requests);

    for (const TransientMemoryBlock& block : m_transientMemoryPlan.blocks)
    {
        VkMemoryRequirements blockRequirements = {};
        blockRequirements.size = block.size;
        blockRequirements.alignment = block.alignment;
        blockRequirements.memoryTypeBits = block.memoryTypeBits;

        VmaAllocation allocation =
            GetMemoryAllocator()->AllocateAliasingBlock(blockRequirements, block.classKey.memoryUsage);
        m_aliasBlocks.push_back(allocation);

        for (const TransientMemorySlot& slot : block.slots)
        {
            for (const ResourceHandle& member : slot.members)
            {
                if (block.classKey.isImage)
                {
                    TransientImageResource* pResource = m_aliasedImages.at(member).get();
                    GetMemoryAllocator()->BindImageToBlock(pResource->getImage(), allocation, slot.offset);
                    // After the bind, never before: a view of an image with no memory bound is a validation
                    // error, and it is the one order this path has to get right.
                    pResource->CreateView();
                    continue;
                }
                TransientBufferResource* pResource = m_aliasedBuffers.at(member).get();
                GetMemoryAllocator()->BindBufferToBlock(pResource->buffer(), allocation, slot.offset);
                // Told now rather than at creation: the offset is what the plan just decided, and it is
                // what `Map` needs to find this resource's bytes inside the block.
                pResource->BindTo(allocation, slot.offset);
            }
        }
    }
}

void RenderGraphBuilder::Build()
{
    if (m_dependencyGraph.HasCycle())
    {
        throw std::runtime_error("Render graph contains a cycle!");
    }

    m_compiledGraph.Destroy(m_vkDevice, m_services.descriptorManager->GetDescriptorPool());
    // Before anything is created: a resource that is aliased into a block, or whose offsets may differ
    // from the last build's, belongs to the build that planned it and not to the builder.
    DestroyAliasedTransients();
    // And the descriptor sets are rebuilt from this build's resources: a rebuild can resolve a resource
    // to a new handle, so a set from the previous build would hold a stale view. Frees them, because the
    // cache owns them now and a node only references the handles.
    m_descriptorSetCache.Clear();
    m_compiledGraph.ReserveNodes(m_renderGraphNodes.size());
    std::vector<std::string> executionOrder = m_dependencyGraph.TopologicalSort();
    if (executionOrder.size() != m_renderGraphNodes.size())
    {
        throw std::runtime_error("Render graph contains a cycle; topological sort is incomplete.");
    }

    // Before allocating anything: a declaration error should not cost an allocation, and this needs only
    // the order and the declarations.
    ValidateTransientLifetimes(executionOrder);
    ValidateDescriptorBindings(executionOrder);

    // Allocate all graph-owned resources before compiling nodes. A buffer transient is skipped here and
    // created unbound below instead: its memory belongs to a block, and the manager's path would give it
    // memory of its own.
    for (const auto& [handle, desc] : m_resourceDescRegistry)
    {
        if (m_importedResources.find(handle) != m_importedResources.end()) continue;
        if (IsAliasedTransient(desc)) continue;
        AllocateResource(desc, *m_services.resourceManager, handle);
    }
    BuildAliasedTransients(executionOrder);

    try
    {
        std::unordered_map<ResourceHandle, uint32_t> resourceCurrentVersions;
        for (const auto& nodeName : executionOrder)
        {
            // Update handle versions
            auto& node = m_renderGraphNodes.at(nodeName);
            for (auto& resource : node.resourceUses)
            {
                ResourceHandle handle = std::string(resource.handle);
                if (resourceCurrentVersions.find(handle) == resourceCurrentVersions.end())
                {
                    resourceCurrentVersions[handle] = 0;
                }
                else
                {
                    resource.version = resourceCurrentVersions[handle];
                    if (resource.io == ResourceIOType::WRITE || resource.io == ResourceIOType::READ_WRITE)
                    {
                        resourceCurrentVersions[handle]++;
                    }
                }
            }

            // Compile RenderGraphNode
            m_compiledGraph.AddNode(CompileRenderGraphNode(node));
        }

        // Derive the scheduling plan (queue segments + cross-queue transfers) once, so Execute() reads
        // it rather than recomputing it every frame.
        //
        // The plan freezes the device's queue family indices, so it must be rebuilt if the device
        // changes. Today Build() always reruns after a device change (a builder is recreated), but a
        // caller that reused a compiled graph across devices would have to rebuild it.
        m_compiledGraph.RebuildExecutionPlan(GetQueueFamilies());
    }
    catch (...)
    {
        // A build that fails leaves an *empty* graph, not a partial one. The nodes compiled before the
        // failing one are destroyed here rather than whenever the builder happens to be destroyed, and the
        // failing node's own objects were released by its compile guard. Two reasons this matters: a graph
        // holding half a pass list is one Execute() would run with a plan that was never derived, and the
        // objects in it would otherwise still be alive when the device is destroyed, which is exactly the
        // leaked-object error that erases the diagnostic this issue exists to deliver.
        m_compiledGraph.Destroy(m_vkDevice, m_services.descriptorManager->GetDescriptorPool());
        throw;
    }
}

bool RenderGraphBuilder::BeginRendering(VkCommandBuffer cmdBuf, const CompiledRenderGraphNode& rgn,
                                        RenderGraphNodeContext& ctx, std::unordered_set<ResourceHandle>& alreadyWritten,
                                        std::vector<VkRenderingAttachmentInfo>& colorAttachments,
                                        std::vector<VkClearValue>& clearValues)
{
    colorAttachments.clear();
    clearValues.clear();

    VkRenderingAttachmentInfo depthAttachment{};
    bool hasDepth = false;
    VkExtent2D renderArea{0, 0};

    uint32_t clearIndex = 0;
    for (const auto& use : rgn.resourceUses)
    {
        if (use.usage != ResourceUsage::COLOR_ATTACHMENT && use.usage != ResourceUsage::DEPTH_STENCIL_ATTACHMENT &&
            use.usage != ResourceUsage::PRESENT)
        {
            continue;
        }

        const auto* image = dynamic_cast<const ImageResource*>(ResolveResource(use.handle));
        if (!image) continue;

        VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        attachment.imageView = image->getView();
        attachment.imageLayout = use.usage == ResourceUsage::DEPTH_STENCIL_ATTACHMENT
                                     ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                     : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        const bool firstWrite = alreadyWritten.find(use.handle) == alreadyWritten.end();
        // Clear only a pure first write. A READ_WRITE (or READ) attachment means "keep what is
        // already there", e.g. blending on top of a previous pass, so it must LOAD.
        const bool bClearAttachment = firstWrite && use.io == ResourceIOType::WRITE;
        attachment.loadOp = bClearAttachment ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        if (bClearAttachment)
        {
            VkClearValue clearValue{};
            if (use.usage == ResourceUsage::DEPTH_STENCIL_ATTACHMENT)
            {
                clearValue.depthStencil = {1.0f, 0};
            }
            else if (clearIndex < rgn.attachmentClearValues.size())
            {
                clearValue = rgn.attachmentClearValues[clearIndex++];
            }
            attachment.clearValue = clearValue;
            alreadyWritten.insert(use.handle);
        }
        else if (use.usage == ResourceUsage::COLOR_ATTACHMENT || use.usage == ResourceUsage::PRESENT)
        {
            ++clearIndex;
        }

        if (use.usage == ResourceUsage::DEPTH_STENCIL_ATTACHMENT)
        {
            depthAttachment = attachment;
            hasDepth = true;
        }
        else
        {
            colorAttachments.push_back(attachment);
        }

        if (renderArea.width == 0 && renderArea.height == 0)
        {
            renderArea = {use.extent.width, use.extent.height};
        }
    }

    if (colorAttachments.empty() && !hasDepth) return false;

    VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderingInfo.renderArea = {.offset = {0, 0}, .extent = renderArea};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
    renderingInfo.pColorAttachments = colorAttachments.empty() ? nullptr : colorAttachments.data();
    if (hasDepth) renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmdBuf, &renderingInfo);

    // Viewport / scissor from the render area.
    VkViewport viewport = {0.0F, 0.0F, static_cast<float>(renderArea.width), static_cast<float>(renderArea.height),
                           0.0F, 1.0F};
    VkRect2D scissor = {{0, 0}, renderArea};
    vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
    vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

    return true;
}

void RenderGraphBuilder::RecordPresentTransition(VkCommandBuffer cmdBuf,
                                                 const std::vector<ResolvedResourceUse>& resourceUses)
{
    std::vector<VkImageMemoryBarrier2> barriers;
    for (const auto& use : resourceUses)
    {
        if (use.usage != ResourceUsage::PRESENT || use.kind != ResourceKind::IMAGE) continue;
        const auto* image = dynamic_cast<const ImageResource*>(ResolveResource(use.handle));
        if (image == nullptr) continue;

        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        // Present does not read through a pipeline, so it has no stage or access of its own.
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier.dstAccessMask = 0;
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image->getImage();
        barrier.subresourceRange = {AspectForFormat(image->GetImageFormat()), 0, 1, 0, 1};
        barriers.push_back(barrier);
    }
    if (barriers.empty()) return;

    VkDependencyInfo dependencyInfo{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependencyInfo.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
    dependencyInfo.pImageMemoryBarriers = barriers.data();
    vkCmdPipelineBarrier2(cmdBuf, &dependencyInfo);
}

void RenderGraphBuilder::RecordBarriers(VkCommandBuffer cmdBuf, const std::vector<ResolvedResourceUse>& resourceUses,
                                        uint32_t queueFamily)
{
    std::vector<VkImageMemoryBarrier2> imageBarriers;
    std::vector<VkBufferMemoryBarrier2> bufferBarriers;

    // Conservative stage masks: use ALL_COMMANDS so any prior stage is flushed and any later stage is blocked.
    // HOST must be included explicitly on *both* sides: ALL_COMMANDS does not imply it, and a barrier whose
    // srcAccess or dstAccess is HOST_WRITE needs HOST in the matching stage mask
    // (VUID-VkBufferMemoryBarrier2-dstAccessMask-03917 / -srcAccessMask-03916). The source side had it and
    // the destination side did not, which went unnoticed until a GPU node wrote a STORAGE_BUFFER - the
    // resolver's {STORAGE_BUFFER, WRITE} profile is HOST_WRITE, so the first such node emitted a barrier
    // this rejected. The *semantic* fix, giving a GPU writer a SHADER_WRITE destination instead of a host
    // profile, is #12's subject and deliberately not here.
    const VkPipelineStageFlags2 srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_2_HOST_BIT;
    const VkPipelineStageFlags2 dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_2_HOST_BIT;

    for (const auto& use : resourceUses)
    {
        ResourceAccessState& state = m_resourceAccessStates[use.handle];

        // A pending acquire means this resource is being handed over from another queue family;
        // the first use emits a queue-family transfer barrier instead of a normal one.
        const bool bAcquire = state.pendingAcquireFamily >= 0;
        const uint32_t acquireSrcFamily =
            bAcquire ? static_cast<uint32_t>(state.pendingAcquireFamily) : VK_QUEUE_FAMILY_IGNORED;

        VkAccessFlags2 srcAccess = 0;
        VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        const bool fromCpu = state.writtenByCpu;
        if (fromCpu)
        {
            // CPU node wrote this resource (host-visible memory); flush before first GPU use.
            srcAccess = VK_ACCESS_2_HOST_WRITE_BIT;
        }
        else if (state.seen)
        {
            srcAccess = state.lastAccess;
            oldLayout = state.lastLayout;
        }

        // A barrier is also required whenever either side of the transition is a write: with the
        // same declared usage the access mask and layout are identical, yet a WAW/WAR hazard remains.
        const bool bHazard = state.lastIo != ResourceIOType::READ || use.io != ResourceIOType::READ;
        const bool needsBarrier = bAcquire || fromCpu || !state.seen || (state.lastAccess != use.access) ||
                                  (state.lastLayout != use.imageLayout) || bHazard;

        if (use.kind == ResourceKind::IMAGE)
        {
            const auto* image = dynamic_cast<const ImageResource*>(ResolveResource(use.handle));
            if (!image) continue;

            VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            barrier.srcStageMask = srcStageMask;
            barrier.dstStageMask = dstStageMask;
            barrier.srcAccessMask = bAcquire ? 0 : srcAccess;
            barrier.dstAccessMask = use.access;
            barrier.oldLayout = state.seen ? oldLayout : VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = use.imageLayout;
            barrier.srcQueueFamilyIndex = bAcquire ? acquireSrcFamily : VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = bAcquire ? queueFamily : VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image->getImage();
            barrier.subresourceRange = {AspectForFormat(image->GetImageFormat()), 0, 1, 0, 1};
            if (needsBarrier)
            {
                imageBarriers.push_back(barrier);
            }
        }
        else if (use.kind == ResourceKind::BUFFER)
        {
            const auto* buffer = dynamic_cast<const BufferResource*>(ResolveResource(use.handle));
            if (!buffer) continue;

            VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
            barrier.srcStageMask = srcStageMask;
            barrier.dstStageMask = dstStageMask;
            barrier.srcAccessMask = bAcquire ? 0 : srcAccess;
            barrier.dstAccessMask = use.access;
            barrier.srcQueueFamilyIndex = bAcquire ? acquireSrcFamily : VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = bAcquire ? queueFamily : VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = buffer->buffer();
            barrier.offset = 0;
            barrier.size = VK_WHOLE_SIZE;
            if (needsBarrier)
            {
                bufferBarriers.push_back(barrier);
            }
        }

        state.seen = true;
        state.lastAccess = use.access;
        state.lastLayout = use.imageLayout;
        state.lastIo = use.io;
        state.queueFamily = queueFamily;
        state.pendingAcquireFamily = -1;
    }

    if (imageBarriers.empty() && bufferBarriers.empty()) return;

    VkDependencyInfo dependencyInfo = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependencyInfo.bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size());
    dependencyInfo.pBufferMemoryBarriers = bufferBarriers.empty() ? nullptr : bufferBarriers.data();
    dependencyInfo.imageMemoryBarrierCount = static_cast<uint32_t>(imageBarriers.size());
    dependencyInfo.pImageMemoryBarriers = imageBarriers.empty() ? nullptr : imageBarriers.data();

    vkCmdPipelineBarrier2(cmdBuf, &dependencyInfo);
}

void RenderGraphBuilder::RecordQueueTransferBarriers(VkCommandBuffer cmdBuf, uint32_t srcQueueFamily,
                                                     uint32_t dstQueueFamily, bool bAcquire,
                                                     const std::vector<ResourceHandle>& handles)
{
    if (bAcquire)
    {
        // The actual barrier is emitted by RecordBarriers on the first use in this queue.
        for (const auto& handle : handles)
        {
            m_resourceAccessStates[handle].pendingAcquireFamily = static_cast<int32_t>(srcQueueFamily);
        }
        return;
    }

    // Release: hand ownership of the resource from the producing queue family to the consumer.
    std::vector<VkImageMemoryBarrier2> imageBarriers;
    std::vector<VkBufferMemoryBarrier2> bufferBarriers;
    for (const auto& handle : handles)
    {
        ResourceAccessState& state = m_resourceAccessStates[handle];
        const IRenderResource* pResource = ResolveResource(handle);
        if (pResource == nullptr) continue;

        if (const auto* image = dynamic_cast<const ImageResource*>(pResource))
        {
            VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.srcAccessMask = state.lastAccess;
            barrier.dstAccessMask = 0;
            barrier.oldLayout = state.lastLayout;
            barrier.newLayout = state.lastLayout;
            barrier.srcQueueFamilyIndex = srcQueueFamily;
            barrier.dstQueueFamilyIndex = dstQueueFamily;
            barrier.image = image->getImage();
            barrier.subresourceRange = {AspectForFormat(image->GetImageFormat()), 0, 1, 0, 1};
            imageBarriers.push_back(barrier);
        }
        else if (const auto* buffer = dynamic_cast<const BufferResource*>(pResource))
        {
            VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.srcAccessMask = state.lastAccess;
            barrier.dstAccessMask = 0;
            barrier.srcQueueFamilyIndex = srcQueueFamily;
            barrier.dstQueueFamilyIndex = dstQueueFamily;
            barrier.buffer = buffer->buffer();
            barrier.offset = 0;
            barrier.size = VK_WHOLE_SIZE;
            bufferBarriers.push_back(barrier);
        }
    }

    if (imageBarriers.empty() && bufferBarriers.empty()) return;

    VkDependencyInfo dependencyInfo = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependencyInfo.bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size());
    dependencyInfo.pBufferMemoryBarriers = bufferBarriers.empty() ? nullptr : bufferBarriers.data();
    dependencyInfo.imageMemoryBarrierCount = static_cast<uint32_t>(imageBarriers.size());
    dependencyInfo.pImageMemoryBarriers = imageBarriers.empty() ? nullptr : imageBarriers.data();

    vkCmdPipelineBarrier2(cmdBuf, &dependencyInfo);
}

uint32_t RenderGraphBuilder::GetQueueFamilyForType(QueueType type) const
{
    return GetQueueFamilyForQueueType(type, GetQueueFamilies());
}

RenderGraphQueueFamilies RenderGraphBuilder::GetQueueFamilies() const
{
    return RenderGraphQueueFamilies{m_services.device->GetGraphicsQueueFamily(),
                                    m_services.device->GetComputeQueueFamily()};
}

void RenderGraphBuilder::EnsureMappingsReleased(const std::string& nodeName) const
{
    // Only aliased buffers can be host-mapped, and they are the reason the rule exists: the pointer
    // points into a block shared with other transients, so it must be released before the next node can
    // be bound to the same bytes. A Persistent or imported resource maps its own storage and is not this
    // rule's subject.
    for (const auto& [handle, pBuffer] : m_aliasedBuffers)
    {
        if (!pBuffer->IsMapped()) continue;
        throw std::runtime_error(
            "Node '" + nodeName + "' left graph-owned transient buffer '" + handle +
            "' mapped after its callback returned. A mapping is valid only for the node whose use declared the "
            "resource: the buffer's bytes are shared with other transients, and a later node may be bound to "
            "them. Unmap the resource before the callback returns.");
    }
}

/// The blocking form: records, submits, and waits for the GPU before returning. Kept as the
/// default so every existing caller keeps the contract it had. The overload below does not wait,
/// but the builder still runs one execution in flight, so calls serialise rather than overlap.
void RenderGraphBuilder::Execute()
{
    Execute(RenderGraphExecuteInfo{});
    m_executor.WaitIdle();
}

void RenderGraphBuilder::Execute(const RenderGraphExecuteInfo& info)
{
    m_resourceAccessStates.clear();

    RenderGraphNodeContext context{
        .queueType = QueueType::GRAPHICS,
        .resourceManager = *m_services.resourceManager,
    };
    // Nodes reach resources through the context, so it has to resolve the way the graph does - which now
    // includes transients the graph owns and the manager therefore does not.
    context.resolveResource = [this](const ResourceHandle& handle) { return ResolveResource(handle); };

    std::unordered_set<ResourceHandle> alreadyWritten;
    std::vector<VkRenderingAttachmentInfo> colorAttachments;
    std::vector<VkClearValue> clearValues;

    const std::vector<CompiledRenderGraphNode>& nodes = m_compiledGraph.GetNodes();
    const RenderGraphExecutionPlan& plan = m_compiledGraph.GetExecutionPlan();

    // ── CPU nodes run host-side, before any GPU segment is recorded. ──────────────────────────
    for (const std::size_t i : plan.cpuNodes)
    {
        const CompiledRenderGraphNode& rgn = nodes[i];
        context.queueType = rgn.queueType;
        context.commandBuffer = VK_NULL_HANDLE;
        context.pipeline = VK_NULL_HANDLE;
        rgn.execute(context);
        EnsureMappingsReleased(rgn.name);
        for (const auto& use : rgn.resourceUses)
        {
            if (use.io == ResourceIOType::WRITE || use.io == ResourceIOType::READ_WRITE)
            {
                ResourceAccessState& state = m_resourceAccessStates[use.handle];
                state.writtenByCpu = true;
                state.seen = false;
            }
        }
    }

    if (plan.IsEmpty()) return;

    // Queue segments and cross-queue transfers were derived from the compiled graph at Build().
    const std::vector<RenderGraphQueueSegment>& segments = plan.segments;
    const std::vector<RenderGraphQueueTransfer>& transfers = plan.transfers;

    // ── Record each segment into its own command buffer. ───────────────────────────────────────
    // The executor owns the command buffers: it can only recycle one once that slot's fence has
    // signalled, which is what makes recording into a slot safe while earlier executions are still
    // in flight. Acquiring blocks while every slot is busy, bounding how far the CPU runs ahead.
    RenderGraphExecutor::ExecutionSlot slot = m_executor.AcquireSlot(plan);
    const std::vector<VkCommandBuffer>& segmentCmdBuffers = slot.segmentCommandBuffers;
    for (size_t s = 0; s < segments.size(); ++s)
    {
        const uint32_t queueFamily = GetQueueFamilyForType(segments[s].queueType);
        VkCommandBuffer cmdBuf = segmentCmdBuffers[s];

        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_ASSERT(vkBeginCommandBuffer(cmdBuf, &beginInfo));

        // Mark resources handed over from another queue; RecordBarriers emits the acquire barrier.
        for (const auto& transfer : transfers)
        {
            if (transfer.consumer == s)
            {
                RecordQueueTransferBarriers(cmdBuf, transfer.producerFamily, transfer.consumerFamily, true,
                                            {transfer.handle});
            }
        }

        for (size_t i = segments[s].begin; i < segments[s].end; ++i)
        {
            const auto& rgn = nodes[i];
            // Segments contain GPU nodes only (see RebuildExecutionPlan); guard anyway so a CPU node
            // can never be recorded as GPU work.
            if (rgn.queueType == QueueType::CPU) continue;
            context.queueType = rgn.queueType;
            context.commandBuffer = cmdBuf;
            context.pipeline = rgn.pipeline;
            context.pipelineLayout = rgn.pipelineLayout;
            context.bindingPoint = rgn.bindingPoint;

            // 1. Barrier batch: transition all resources used by this node.
            RecordBarriers(cmdBuf, rgn.resourceUses, queueFamily);

            // 2. Auto render pass wrap for graphics nodes.
            bool rendering = false;
            if (rgn.queueType == QueueType::GRAPHICS)
            {
                rendering = BeginRendering(cmdBuf, rgn, context, alreadyWritten, colorAttachments, clearValues);
            }

            // 3. Bind pipeline + descriptor sets.
            if (rgn.pipeline != VK_NULL_HANDLE)
            {
                vkCmdBindPipeline(cmdBuf, rgn.bindingPoint, rgn.pipeline);

                if (!rgn.descriptorSetLayouts.empty())
                {
                    // Every node's sets were written at Build() from the reflection-derived layout,
                    // so recording only binds the handles. There is no per-node write here any more,
                    // and no semantic/reflection branch.
                    vkCmdBindDescriptorSets(cmdBuf, rgn.bindingPoint, rgn.pipelineLayout, 0,
                                            static_cast<uint32_t>(rgn.descriptorSets.size()), rgn.descriptorSets.data(),
                                            0, nullptr);
                }
            }

            // 4. User GPU work.
            rgn.execute(context);

            // 4b. Ray tracing nodes issue the trace automatically; the SBT is graph-managed.
            if (rgn.isRayTracing && rgn.pipeline != VK_NULL_HANDLE && rgn.traceExtent.width > 0)
            {
                const VkStridedDeviceAddressRegionKHR callable{0, 0, 0};
                VkExt::vkCmdTraceRaysKHR(cmdBuf, &rgn.sbtRegions[0], &rgn.sbtRegions[1], &rgn.sbtRegions[2], &callable,
                                         rgn.traceExtent.width, rgn.traceExtent.height, 1);
            }

            // 5. End auto render pass.
            if (rendering)
            {
                vkCmdEndRendering(cmdBuf);
            }

            // 5b. A present target leaves the node in COLOR_ATTACHMENT_OPTIMAL; the transition to
            // PRESENT_SRC is after the render pass, because the presentation engine - its consumer - is
            // not a node, so no consumer's barrier can do it.
            RecordPresentTransition(cmdBuf, rgn.resourceUses);

            // 6. Mapping lifetime: the callback's host mapping must not outlive the node. Checked after
            // the render pass is closed, so the throw (a programmer error) cannot leave one open.
            EnsureMappingsReleased(rgn.name);
        }

        // Release ownership of resources consumed by a later queue.
        for (const auto& transfer : transfers)
        {
            if (transfer.producer == s)
            {
                RecordQueueTransferBarriers(cmdBuf, transfer.producerFamily, transfer.consumerFamily, false,
                                            {transfer.handle});
            }
        }

        VK_ASSERT(vkEndCommandBuffer(cmdBuf));
    }

    // ── Submit through the executor, which owns the handover semaphores and the waiting. ───────
    m_executor.Submit(plan, slot, info);
}

std::vector<std::string> RenderGraphBuilder::GetExecutionOrder() const { return m_dependencyGraph.TopologicalSort(); }

}  // namespace Muyo::RenderGraph
