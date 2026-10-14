#pragma once
#include "RenderGraph/RenderGraphNodeResource.h"

/// The engine's built-in graphics descriptor-set convention, as `DescriptorBinding` values.
///
/// This lives **outside** `src/RenderGraph/` on purpose. The graph derives a node's set layout from the
/// shader reflection and takes each resource's (set, binding) from the `ResourceUse`, so the library
/// needs no built-in convention and cannot impose one. The values below are the engine's, mirrored from
/// `shaders/shared/RenderGraph/bindings.h`; a library consumer is free to define its own instead.
///
/// A `DescriptorBinding` names only the slot. The descriptor *type* comes from the shader reflection, so
/// a resource bound here must match the type the shader declares at that location.
namespace Muyo::RenderGraphSemanticBindings
{
using RenderGraph::DescriptorBinding;

/// Set 0, binding 0: the per-frame view/camera constant buffer.
inline constexpr DescriptorBinding PerView{0, 0};
/// Set 1, binding 0: the per-object structured buffer.
inline constexpr DescriptorBinding PerObj{1, 0};
/// Set 2, binding 0: the material parameter buffer.
inline constexpr DescriptorBinding MaterialParams{2, 0};
/// Set 2, binding 1: the bindless texture array.
inline constexpr DescriptorBinding MaterialTextures{2, 1};
}  // namespace Muyo::RenderGraphSemanticBindings
