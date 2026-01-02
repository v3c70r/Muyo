#include "ShaderReflectionFetcher.h"

#include <algorithm>
#include <cassert>

#include "ShaderAsset.h"
#include "VkRenderDevice.h"

namespace Muyo
{

ShaderReflection FetchShaderReflection(const SpirvCode& spirvCode)
{
    SpvReflectShaderModule module;
    SpvReflectResult result =
        spvReflectCreateShaderModule(sizeof(uint32_t) * spirvCode.size(), spirvCode.data(), &module);
    assert(result == SPV_REFLECT_RESULT_SUCCESS);

    ShaderReflection reflection;
    // Extract ShaderReflection structure
    {
        // Descriptor bindings
        uint32_t setCount = 0;
        result = spvReflectEnumerateDescriptorSets(&module, &setCount, nullptr);
        if (result == SPV_REFLECT_RESULT_SUCCESS && setCount > 0)
        {
            std::vector<SpvReflectDescriptorSet*> sets(setCount);
            spvReflectEnumerateDescriptorSets(&module, &setCount, sets.data());
            for (const auto* set : sets)
            {
                for (uint32_t i = 0; i < set->binding_count; ++i)
                {
                    const auto* binding = set->bindings[i];
                    ShaderReflection::DescriptorBinding desc;
                    desc.set = set->set;
                    desc.binding = binding->binding;
                    desc.type = static_cast<VkDescriptorType>(binding->descriptor_type);
                    desc.count = binding->count;
                    desc.stageFlags = static_cast<VkShaderStageFlags>(module.shader_stage);
                    desc.name = binding->name ? binding->name : "";
                    desc.arrayDims.assign(binding->array.dims, binding->array.dims + binding->array.dims_count);
                    // desc.bindingFlags = static_cast<VkDescriptorBindingFlags>(binding->binding_flags);
                    desc.bindingFlags = 0;  // SPIRV-Reflect does not expose binding flags directly
                    reflection.descriptorBindings.push_back(std::move(desc));
                }
            }
        }

        // Push constant ranges
        uint32_t pcCount = 0;
        result = spvReflectEnumeratePushConstantBlocks(&module, &pcCount, nullptr);
        if (result == SPV_REFLECT_RESULT_SUCCESS && pcCount > 0)
        {
            std::vector<SpvReflectBlockVariable*> pcs(pcCount);
            spvReflectEnumeratePushConstantBlocks(&module, &pcCount, pcs.data());
            for (const auto* pc : pcs)
            {
                ShaderReflection::PushConstantRange range;
                range.offset = pc->offset;
                range.size = pc->size;
                range.stageFlags = static_cast<VkShaderStageFlags>(module.shader_stage);
                reflection.pushConstantRanges.push_back(range);
            }
        }

        // Input variables
        uint32_t inputCount = 0;
        result = spvReflectEnumerateInputVariables(&module, &inputCount, nullptr);
        if (result == SPV_REFLECT_RESULT_SUCCESS && inputCount > 0)
        {
            std::vector<SpvReflectInterfaceVariable*> inputs(inputCount);
            spvReflectEnumerateInputVariables(&module, &inputCount, inputs.data());
            for (const auto* var : inputs)
            {
                ShaderReflection::IOVariable io;
                io.location = var->location;
                io.name = var->name ? var->name : "";
                io.type = var->format;
                io.arrayDims.assign(var->array.dims, var->array.dims + var->array.dims_count);
                io.isBuiltIn = ((var->decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN) != 0U);
                if (io.isBuiltIn)
                {
                    io.builtInName = var->name ? var->name : "";
                }
                else
                {
                    io.builtInName = "";
                }
                reflection.inputVariables.push_back(io);
            }
        }

        // Output variables
        uint32_t outputCount = 0;
        result = spvReflectEnumerateOutputVariables(&module, &outputCount, nullptr);
        if (result == SPV_REFLECT_RESULT_SUCCESS && outputCount > 0)
        {
            std::vector<SpvReflectInterfaceVariable*> outputs(outputCount);
            spvReflectEnumerateOutputVariables(&module, &outputCount, outputs.data());
            for (const auto* var : outputs)
            {
                ShaderReflection::IOVariable io;
                io.location = var->location;
                io.name = var->name ? var->name : "";
                io.type = var->format;
                io.arrayDims.assign(var->array.dims, var->array.dims + var->array.dims_count);
                io.isBuiltIn = ((var->decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN) != 0U);
                if (io.isBuiltIn)
                {
                    io.builtInName = var->name ? var->name : "";
                }
                else
                {
                    io.builtInName = "";
                }
                reflection.outputVariables.push_back(io);
            }
        }
    }

    // Entry points
    {
        uint32_t entryCount = module.entry_point_count;
        for (uint32_t i = 0; i < entryCount; ++i)
        {
            const SpvReflectEntryPoint& entry = module.entry_points[i];
            ShaderReflection::EntryPoint ep;
            ep.name = entry.name ? entry.name : "";
            ep.stage = static_cast<VkShaderStageFlagBits>(entry.shader_stage);

            // Workgroup size (for compute shaders)
            if (entry.shader_stage == SPV_REFLECT_SHADER_STAGE_COMPUTE_BIT)
            {
                ShaderReflection::WorkgroupSize wg;
                wg.x = entry.local_size.x;
                wg.y = entry.local_size.y;
                wg.z = entry.local_size.z;
                ep.workgroupSize = wg;
            }
            reflection.entryPoints.push_back(ep);
        }
    }

    // Specialization constants
    {
        uint32_t specCount = 0;
        SpvReflectResult result = spvReflectEnumerateSpecializationConstants(&module, &specCount, nullptr);
        if (result == SPV_REFLECT_RESULT_SUCCESS && specCount > 0)
        {
            std::vector<SpvReflectSpecializationConstant*> specs(specCount);
            spvReflectEnumerateSpecializationConstants(&module, &specCount, specs.data());
            for (const auto* spec : specs)
            {
                ShaderReflection::SpecializationConstant sc;
                sc.id = spec->constant_id;
                sc.name = spec->name ? spec->name : "";
                // TODO(qgu): make this work when we need to get SpecializationConstant
                // sc.type = spec->format;
                // sc.defaultValue = spec->default_value;
                reflection.specializationConstants.push_back(sc);
            }
        }
    }

    // Input attachments
    // TODO(qgu): Handle input attachments as part of the descriptor set when we need to handle subpasses
    //{
    //    uint32_t inputAttachmentCount = 0;
    //    SpvReflectResult result = spvReflectEnumerateInputVariables(&module, &inputAttachmentCount, nullptr);
    //    if (result == SPV_REFLECT_RESULT_SUCCESS && inputAttachmentCount > 0)
    //    {
    //        std::vector<SpvReflectInterfaceVariable*> vars(inputAttachmentCount);
    //        spvReflectEnumerateInputVariables(&module, &inputAttachmentCount, vars.data());
    //        for (const auto* var : vars)
    //        {
    //            if (var->storage_class == SpvStorageClassInput &&
    //                var->input_attachment_index != SPV_REFLECT_INPUT_ATTACHMENT_INDEX_NOT_SET)
    //            {
    //                ShaderReflection::InputAttachment ia;
    //                ia.set = var->set;
    //                ia.binding = var->binding;
    //                ia.inputAttachmentIndex = var->input_attachment_index;
    //                ia.name = var->name ? var->name : "";
    //                reflection.inputAttachments.push_back(ia);
    //            }
    //        }
    //    }
    //}

    // TODO(qgu): Revisit it when we need to handle decorations
    // Resource decorations (readonly, writeonly, coherent, volatile)
    //{
    //    uint32_t resourceCount = 0;
    //    SpvReflectResult result = spvReflectEnumerateDescriptorBindings(&module, &resourceCount, nullptr);
    //    if (result == SPV_REFLECT_RESULT_SUCCESS && resourceCount > 0)
    //    {
    //        std::vector<SpvReflectDescriptorBinding*> bindings(resourceCount);
    //        spvReflectEnumerateDescriptorBindings(&module, &resourceCount, bindings.data());
    //        for (const auto* binding : bindings)
    //        {
    //            ShaderReflection::ResourceDecoration deco;
    //            deco.set = binding->set;
    //            deco.binding = binding->binding;
    //            deco.readonly = (binding->accessed & SPV_REFLECT_ACCESS_READ) != 0;
    //            deco.writeonly = (binding->accessed & SPV_REFLECT_ACCESS_WRITE) != 0;
    //            deco.coherent = (binding->coherent != 0);
    //            deco.volatile_ = (binding->volatile_ != 0);
    //            reflection.resourceDecorations.push_back(deco);
    //        }
    //    }
    //}

    spvReflectDestroyShaderModule(&module);
    return reflection;
}

ShaderReflection MergeShaderReflections(const std::vector<ShaderReflection>& reflections)
{
    ShaderReflection merged;

    // Merge descriptor bindings by (set, binding)
    struct BindingKey
    {
        uint32_t set, binding;
        bool operator==(const BindingKey& other) const { return set == other.set && binding == other.binding; }
    };
    struct BindingKeyHash
    {
        size_t operator()(const BindingKey& k) const
        {
            return (static_cast<size_t>(k.set) << 16) ^ static_cast<size_t>(k.binding);
        }
    };
    std::unordered_map<BindingKey, ShaderReflection::DescriptorBinding, BindingKeyHash> bindingMap;

    for (const auto& refl : reflections)
    {
        for (const auto& desc : refl.descriptorBindings)
        {
            BindingKey key{.set=desc.set, .binding=desc.binding};
            auto it = bindingMap.find(key);
            if (it == bindingMap.end())
            {
                bindingMap[key] = desc;
            }
            else
            {
                // Validate type/count/name compatibility
                if (it->second.type != desc.type || it->second.count != desc.count || it->second.name != desc.name)
                {
                    throw std::runtime_error(
                        "Incompatible descriptor binding across shader stages (set=" + std::to_string(desc.set) +
                        ", binding=" + std::to_string(desc.binding) + ")");
                }
                // Merge stage flags
                it->second.stageFlags |= desc.stageFlags;
            }
        }
    }
    for (const auto& [_, desc] : bindingMap)
    {
        merged.descriptorBindings.push_back(desc);
    }
    // Sort for deterministic order
    std::sort(merged.descriptorBindings.begin(), merged.descriptorBindings.end(), [](const auto& a, const auto& b) { return std::tie(a.set, a.binding) < std::tie(b.set, b.binding); });

    // Merge push constant ranges (combine overlapping ranges and stage flags)
    for (const auto& refl : reflections)
    {
        for (const auto& pc : refl.pushConstantRanges)
        {
            bool mergedRange = false;
            for (auto& mpc : merged.pushConstantRanges)
            {
                // Overlap or adjacent
                uint32_t pcEnd = pc.offset + pc.size;
                uint32_t mpcEnd = mpc.offset + mpc.size;
                if (pcEnd > mpc.offset && mpcEnd > pc.offset)
                {
                    // Merge ranges
                    uint32_t newStart = std::min(mpc.offset, pc.offset);
                    uint32_t newEnd = std::max(mpcEnd, pcEnd);
                    mpc.offset = newStart;
                    mpc.size = newEnd - newStart;
                    mpc.stageFlags |= pc.stageFlags;
                    mergedRange = true;
                    break;
                }
            }
            if (!mergedRange)
            {
                merged.pushConstantRanges.push_back(pc);
            }
        }
    }

    // Merge input/output variables, entry points, specialization constants, etc.
    auto mergeUnique = [](auto& dst, const auto& src)
    {
        for (const auto& v : src)
        {
            if (std::find(dst.begin(), dst.end(), v) == dst.end()) dst.push_back(v);
        }
    };
    for (const auto& refl : reflections)
    {
        mergeUnique(merged.inputVariables, refl.inputVariables);
        mergeUnique(merged.outputVariables, refl.outputVariables);
        mergeUnique(merged.entryPoints, refl.entryPoints);
        mergeUnique(merged.specializationConstants, refl.specializationConstants);
        mergeUnique(merged.rayTracingInfos, refl.rayTracingInfos);
        mergeUnique(merged.inputAttachments, refl.inputAttachments);
        mergeUnique(merged.resourceDecorations, refl.resourceDecorations);
        mergeUnique(merged.requiredFeatures, refl.requiredFeatures);
        mergeUnique(merged.requiredExtensions, refl.requiredExtensions);
    }

    // Validate that all entry points and stages make sense (e.g., no duplicate stages for same entry point)
    std::unordered_map<std::string, VkShaderStageFlagBits> entryStageMap;
    for (const auto& ep : merged.entryPoints)
    {
        if (auto it = entryStageMap.find(ep.name); it != entryStageMap.end())
        {
            if (it->second == ep.stage)
            {
                throw std::runtime_error("Duplicate entry point '" + ep.name + "' for stage " +
                                         std::to_string(ep.stage));
            }
        }
        else
        {
            entryStageMap[ep.name] = ep.stage;
        }
    }

    return merged;
}

}  // namespace Muyo
