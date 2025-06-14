#pragma once
#include <concepts>
namespace Muyo
{
class RenderGraphParameters
{
    std::vector<RenderGraphResourceHandle> m_vInputResources;
    std::vector<RenderGraphResourceHandle> m_vOutputResources;
};
class RenderGraphBuilder
{
public:
    // Requires T to be a subclass of RenderGraphParameters
    template <typename T>
    concept DerivedFromRenderGraphParameters = std::derived_from<RenderGraphParameters, T>;
    template <DerivedFromRenderGraphParameters T>
    T* AllocateRenderPassParameters()
    {

    }

    void AddPass();
};
}  // namespace Muyo
