#include "GraphicsTestEnv.h"
#include "RenderPassParameters.h"
#include "catch2/catch_message.hpp"
#include "RenderGraph/RenderGraphBuilder.h"
#include "RenderGraph/RenderGraphResourceDesc.h"

namespace Muyo
{

//class RenderPassRDGParameters : public RenderGraphParameters
//{
//    protected:
//        Muyo::RenderPassParameters m_renderPassParameters;
//        VkPipeline m_pipeline;
//};
//
//TEST_CASE("RenderToTarget", "[RenderGraphBuilder]")
//{
//    class DrawGeometryPass : public RenderPassRDGParameters
//    {
//        public:
//        void OnGraphBuild() override
//        {
//            // Allocate resources
//            for (auto& inputResource : vInputResources)
//            {
//                std::visit(
//                    [&](auto&& arg)
//                    {
//                        auto* pResource = Allocate(arg, GetRenderResourceManager());
//                        m_renderPassParameters.AddParameter(pResource, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
//                                                            VK_SHADER_STAGE_COMPUTE_BIT);
//                    },
//                    inputResource.GetResourceDesc());
//            }
//            for (auto& outputResource : vOutputResources)
//            {
//                std::visit(
//                    [&](auto&& arg)
//                    {
//                        auto* pResource = Allocate(arg, GetRenderResourceManager());
//                        m_renderPassParameters.AddParameter(pResource, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
//                                                            VK_SHADER_STAGE_COMPUTE_BIT, 1);
//                    },
//                    outputResource.GetResourceDesc());
//            }
//            m_renderPassParameters.AddPushConstantParameter<uint32_t>(VK_SHADER_STAGE_COMPUTE_BIT);
//            m_renderPassParameters.Finalize("DrawGeometry");
//
//            {
//                // Create pipeline, descriptor sets, etc.
//            }
//        }
//
//    };
//}
}
