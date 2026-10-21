#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <vector>

#include "RenderGraph/RenderGraphNodeResource.h"
#include "RenderGraph/RenderGraphTrace.h"

using namespace Muyo::RenderGraph;

namespace
{
using UseList = std::vector<TraceUseView>;

/// A trace whose shape is the interesting part: a graphics segment, an async compute segment that consumes a
/// handed-over resource, then two further graphics segments split by a CPU node. Small enough to read, and it
/// exercises every edge kind and both families.
struct Fixture
{
    UseList shadowUses;
    UseList gbufferUses;
    UseList bloomDownUses;
    UseList bloomBlurUses;
    UseList presentUses;
    UseList uploadUses;

    std::vector<TraceNodeView> nodes;
    RenderGraphExecutionPlan plan;
    TraceScope scope;

    Fixture()
    {
        shadowUses = {{.handle = "shadow.depth", .io = ResourceIOType::WRITE}};
        gbufferUses = {{.handle = "gbuffer.albedo", .io = ResourceIOType::WRITE},
                       {.handle = "shadow.depth", .io = ResourceIOType::READ}};
        bloomDownUses = {{.handle = "gbuffer.albedo", .io = ResourceIOType::READ},
                         {.handle = "bloom.mips", .io = ResourceIOType::WRITE}};
        bloomBlurUses = {{.handle = "bloom.mips", .io = ResourceIOType::READ_WRITE}};
        presentUses = {{.handle = "swapchain", .io = ResourceIOType::TRANSFER_WRITE}};
        uploadUses = {{.handle = "particles", .io = ResourceIOType::HOST_WRITE}};

        nodes = {
            {.name = "shadow", .queueType = QueueType::GRAPHICS, .bAsync = false, .uses = shadowUses},
            {.name = "gbuffer", .queueType = QueueType::GRAPHICS, .bAsync = false, .uses = gbufferUses},
            {.name = "bloomDown", .queueType = QueueType::COMPUTE, .bAsync = true, .uses = bloomDownUses},
            {.name = "bloomBlur", .queueType = QueueType::COMPUTE, .bAsync = true, .uses = bloomBlurUses},
            {.name = "present", .queueType = QueueType::GRAPHICS, .bAsync = false, .uses = presentUses},
            {.name = "upload", .queueType = QueueType::CPU, .bAsync = false, .uses = uploadUses},
        };

        // gfx[0..2) -> compute[2..4) -> gfx[4..5) -> gfx[5..6), with the CPU node after all GPU work.
        plan.segments = {
            {.queueType = QueueType::GRAPHICS, .begin = 0, .end = 2},
            {.queueType = QueueType::COMPUTE, .begin = 2, .end = 4},
            {.queueType = QueueType::GRAPHICS, .begin = 4, .end = 5},
            {.queueType = QueueType::GRAPHICS, .begin = 5, .end = 6},
        };
        plan.cpuNodes = {};
        plan.transfers = {
            {.producer = 0, .consumer = 1, .handle = "gbuffer.albedo", .producerFamily = 0, .consumerFamily = 1}};

        scope = {.deviceName = "test device",
                 .graphicsFamily = 0,
                 .computeFamily = 1,
                 .bDedicatedCompute = true,
                 .inFlightCount = 1};
    }

    RenderGraphTrace Build() const { return TracePlan(plan, nodes, scope); }
};
}  // namespace

TEST_CASE("RenderGraphTrace: the plan is recorded with its edges and their causes", "[RenderGraphTrace]")
{
    const Fixture fixture;
    const RenderGraphTrace trace = fixture.Build();

    REQUIRE(trace.nodes.size() == fixture.nodes.size());
    CHECK(trace.nodes[2].name == "bloomDown");
    CHECK(trace.nodes[2].bAsync);
    // Uses are split by `IsWrite`, so a read-write use lands in `writes` and a host write is a write.
    CHECK(trace.nodes[1].reads == std::vector<ResourceHandle>{"shadow.depth"});
    CHECK(trace.nodes[1].writes == std::vector<ResourceHandle>{"gbuffer.albedo"});
    CHECK(trace.nodes[3].writes == std::vector<ResourceHandle>{"bloom.mips"});
    CHECK(trace.nodes[5].writes == std::vector<ResourceHandle>{"particles"});

    REQUIRE(trace.segments.size() == 4);
    // Queue keys resolve to families through the scope, so the trace says which family actually ran the work.
    CHECK(trace.segments[0].family == 0);
    CHECK(trace.segments[1].queueType == QueueType::COMPUTE);
    CHECK(trace.segments[1].family == 1);
    CHECK(trace.segments[2].family == 0);

    // The transfer is an edge *with its cause*, not just a pair of indices.
    REQUIRE(trace.segments[1].dependsOn.size() == 1);
    CHECK(trace.segments[1].dependsOn[0].from == 0);
    CHECK(trace.segments[1].dependsOn[0].to == 1);
    CHECK(trace.segments[1].dependsOn[0].resource == "gbuffer.albedo");
    CHECK(trace.segments[1].dependsOn[0].kind == TraceEdgeKind::Transfer);

    // Different queue families share no program order, so no edge is recorded between them beyond the transfer.
    CHECK(trace.segments[2].dependsOn.empty());

    // Two adjacent segments on one queue are ordered by program order, and that edge is recorded too - it is what
    // makes a split visible when the plan shows two segments where a reader expected one.
    REQUIRE(trace.segments[3].dependsOn.size() == 1);
    CHECK(trace.segments[3].dependsOn[0].from == 2);
    CHECK(trace.segments[3].dependsOn[0].kind == TraceEdgeKind::QueueOrder);
    CHECK(trace.segments[3].dependsOn[0].resource.empty());

    // Time is reserved and unfilled: nothing may read a zero as "instantaneous".
    for (const TraceSegment& segment : trace.segments)
    {
        CHECK_FALSE(segment.gpu.valid);
    }
    CHECK_FALSE(trace.scope.timestamps.bKnown);
}

TEST_CASE("RenderGraphTrace: a CPU node is recorded as host work, in order", "[RenderGraphTrace]")
{
    Fixture fixture;
    fixture.plan.cpuNodes = {5};
    const RenderGraphTrace trace = fixture.Build();

    CHECK(trace.cpuNodes == std::vector<std::size_t>{5});
    CHECK(trace.nodes[5].queueType == QueueType::CPU);
    // The CPU node belongs to no segment, so the plan's segments are untouched by its presence.
    CHECK(trace.segments.size() == fixture.plan.segments.size());
}

TEST_CASE("RenderGraphNodeResource: every usage has a diagnostic name", "[RenderGraphBuilder]")
{
    // The enumerators are contiguous from SAMPLED to PUSH_CONSTANTS. A new one added without a case in
    // `NameOf` stops the build (`-Wswitch`); this test pins what a diagnostic prints once it has a case.
    // `PRESENT` was the enumerator that had none, so any message naming a present use said
    // "(unknown usage)" (#169).
    for (uint8_t value = static_cast<uint8_t>(ResourceUsage::SAMPLED);
         value <= static_cast<uint8_t>(ResourceUsage::PUSH_CONSTANTS); ++value)
    {
        const auto usage = static_cast<ResourceUsage>(value);
        INFO("usage value " << static_cast<int>(value));
        CHECK(std::string(NameOf(usage)) != "(unknown usage)");
    }
    CHECK(std::string(NameOf(ResourceUsage::PRESENT)) == "PRESENT");
}

TEST_CASE("RenderGraphTrace: stage and access masks render as names, unknown bits as hex", "[RenderGraphTrace]")
{
    CHECK(NameOfPipelineStages(0) == "NONE");
    CHECK(NameOfPipelineStages(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT) == "COLOR_ATTACHMENT_OUTPUT");
    CHECK(NameOfPipelineStages(VK_PIPELINE_STAGE_2_HOST_BIT) == "HOST");

    // The union a merged in-family barrier carries renders as both names, in table order.
    const std::string combined = NameOfPipelineStages(VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                                      VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT);
    CHECK(combined.find("EARLY_FRAGMENT") != std::string::npos);
    CHECK(combined.find("LATE_FRAGMENT") != std::string::npos);

    // A bit the table does not know is *visible*, not dropped: a new stage must not vanish from a trace.
    const std::string unknown = NameOfPipelineStages(VK_PIPELINE_STAGE_2_CONDITIONAL_RENDERING_BIT_EXT);
    CHECK(unknown.find("0x") != std::string::npos);

    CHECK(NameOfAccesses(0) == "NONE");
    CHECK(NameOfAccesses(VK_ACCESS_2_HOST_WRITE_BIT) == "HOST_WRITE");
    const std::string accessUnion = NameOfAccesses(VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
    CHECK(accessUnion.find("SHADER_READ") != std::string::npos);
    CHECK(accessUnion.find("SHADER_WRITE") != std::string::npos);
}

TEST_CASE("RenderGraphTrace: JSON carries every schema field and escapes what it must", "[RenderGraphTrace]")
{
    Fixture fixture;
    RenderGraphTrace trace = fixture.Build();
    trace.execution = 7;
    // A name that would break a naive serialiser. Node names are user-supplied, so this is reachable input.
    trace.nodes[0].name = "shadow\"quoted\"\nnewline";

    TraceBarrier handover;
    handover.resource = "gbuffer.albedo";
    handover.kind = TraceBarrierKind::Image;
    handover.srcStage = NameOfPipelineStages(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    handover.dstStage = NameOfPipelineStages(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    handover.srcAccess = NameOfAccesses(VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    handover.dstAccess = NameOfAccesses(VK_ACCESS_2_SHADER_READ_BIT);
    handover.srcFamily = 0;
    handover.dstFamily = 1;
    trace.segments[0].barriers.push_back(handover);

    const std::string json = ToJson(trace);

    // The field list is data, and this is the check that keeps it complete: a struct member that gains no field
    // fails here rather than silently missing from the contract.
    for (const TraceField& field : TraceFields())
    {
        INFO("missing field: " << field.name);
        CHECK(json.find(std::string("\"") + field.name + "\"") != std::string::npos);
    }

    // NDJSON: one object, one line, so `tail -1` is a valid document.
    CHECK(json.find('\n') == std::string::npos);
    CHECK(json.find("\\\"quoted\\\"") != std::string::npos);
    CHECK(json.find("\\nnewline") != std::string::npos);

    CHECK(json.find("\"COLOR_ATTACHMENT_WRITE\"") != std::string::npos);
    CHECK(json.find("\"src_family\":0") != std::string::npos);
    CHECK(json.find("\"dst_family\":1") != std::string::npos);
    // Reserved capability: a consumer can tell "no timings were collected" from "timings were zero".
    CHECK(json.find("\"timings\":false") != std::string::npos);
    // The stage names, not the masks.
    CHECK(json.find("COLOR_ATTACHMENT_OUTPUT") != std::string::npos);
}

TEST_CASE("RenderGraphTrace: text, dot and mermaid describe the same trace", "[RenderGraphTrace]")
{
    Fixture fixture;
    RenderGraphTrace trace = fixture.Build();
    trace.execution = 3;

    TraceBarrier barrier;
    barrier.resource = "gbuffer.albedo";
    barrier.kind = TraceBarrierKind::Image;
    barrier.srcStage = "COLOR_ATTACHMENT_OUTPUT";
    barrier.dstStage = "COMPUTE_SHADER";
    barrier.srcAccess = "COLOR_ATTACHMENT_WRITE";
    barrier.dstAccess = "SHADER_READ";
    barrier.srcFamily = 0;
    barrier.dstFamily = 1;
    trace.segments[0].barriers.push_back(barrier);

    const std::string text = ToText(trace);
    CHECK(text.find("execution 3") != std::string::npos);
    CHECK(text.find("bloomDown") != std::string::npos);
    CHECK(text.find("timings=unavailable") != std::string::npos);
    // A handover shows both families, which is the fact a reviewer checks by eye.
    CHECK(text.find("family 0->1") != std::string::npos);

    const TraceGraph graph = ToGraph(trace);
    CHECK(graph.nodes.size() == trace.segments.size());
    std::size_t edgeCount = 0;
    for (const TraceSegment& segment : trace.segments)
    {
        edgeCount += segment.dependsOn.size();
    }
    CHECK(graph.edges.size() == edgeCount);

    // Each diagram syntax renders the graph, not a second interpretation of the trace.
    const std::string dot = ToDot(graph);
    const std::string mermaid = ToMermaid(graph);
    CHECK(dot.find("digraph") != std::string::npos);
    CHECK(mermaid.find("flowchart LR") != std::string::npos);
    for (const TraceGraphNode& node : graph.nodes)
    {
        CHECK(dot.find("s" + std::to_string(node.segment)) != std::string::npos);
        CHECK(mermaid.find("s" + std::to_string(node.segment)) != std::string::npos);
    }
    CHECK(dot.find("gbuffer.albedo (transfer)") != std::string::npos);
    CHECK(mermaid.find("gbuffer.albedo (transfer)") != std::string::npos);
}
