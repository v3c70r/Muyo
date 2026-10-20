#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <string>
#include <vector>

#include "Debug/RenderGraphLaneLayout.h"
#include "Debug/RenderGraphTracePage.h"

using namespace Muyo;
using RenderGraph::QueueType;
using RenderGraph::RenderGraphTrace;
using RenderGraph::TraceEdge;
using RenderGraph::TraceEdgeKind;
using RenderGraph::TraceNode;
using RenderGraph::TraceSegment;

namespace
{
/// Minimal trace builder: node names in compiled order, segments, edges and host nodes.
struct TraceFixture
{
    RenderGraphTrace trace;

    TraceFixture& Node(const std::string& name, QueueType queue = QueueType::GRAPHICS)
    {
        trace.nodes.push_back(TraceNode{.index = trace.nodes.size(), .name = name, .queueType = queue});
        return *this;
    }
    TraceFixture& Segment(QueueType queue, std::size_t begin, std::size_t end)
    {
        TraceSegment segment;
        segment.index = trace.segments.size();
        segment.queueType = queue;
        segment.begin = begin;
        segment.end = end;
        trace.segments.push_back(std::move(segment));
        return *this;
    }
    TraceFixture& Edge(std::size_t from, std::size_t to, const std::string& resource, TraceEdgeKind kind)
    {
        trace.segments[to].dependsOn.push_back(TraceEdge{.from = from, .to = to, .resource = resource, .kind = kind});
        return *this;
    }
    TraceFixture& HostNode(std::size_t index)
    {
        trace.cpuNodes.push_back(index);
        return *this;
    }
};

LaneViewport Viewport()
{
    LaneViewport viewport;
    viewport.width = 1000.0F;
    viewport.leftGutter = 72.0F;
    viewport.laneHeight = 24.0F;
    viewport.laneGap = 6.0F;
    return viewport;
}

const LaneRect& BlockOfSegment(const LaneLayout& layout, std::size_t segment)
{
    for (const LaneRect& block : layout.blocks)
    {
        if (!block.bHostBlock && block.segment == segment)
        {
            return block;
        }
    }
    FAIL("no block for segment " << segment);
    return layout.blocks.front();
}
}  // namespace

TEST_CASE("RenderGraphLaneLayout: a host node between GPU segments splits the lane by eye", "[LaneLayout]")
{
    // The property the panel exists for: a CPU node that splits a run of GPU work into two segments is visible as two
    // blocks with a host block between them. If the layout ignored host nodes, the two segments would abut and the
    // split would be invisible - which is exactly what a reader is trying to check.
    const TraceFixture fixture = TraceFixture()
                                     .Node("shadow", QueueType::GRAPHICS)
                                     .Node("gbuffer", QueueType::GRAPHICS)
                                     .Node("upload", QueueType::CPU)
                                     .Node("lighting", QueueType::GRAPHICS)
                                     .Segment(QueueType::GRAPHICS, 0, 2)
                                     .Segment(QueueType::GRAPHICS, 3, 4)
                                     .HostNode(2);

    const LaneLayout layout = BuildLaneLayout(fixture.trace, Viewport());

    REQUIRE(layout.blocks.size() == 3);
    // Execution order, left to right: segment 0, host node, segment 1.
    CHECK_FALSE(layout.blocks[0].bHostBlock);
    CHECK(layout.blocks[1].bHostBlock);
    CHECK_FALSE(layout.blocks[2].bHostBlock);
    CHECK(layout.blocks[0].segment == 0);
    CHECK(layout.blocks[2].segment == 1);

    // The host block occupies horizontal space between them, and it is on its own lane.
    const LaneRect& first = layout.blocks[0];
    const LaneRect& host = layout.blocks[1];
    const LaneRect& second = layout.blocks[2];
    CHECK(host.lane == QueueType::CPU);
    CHECK(host.x >= first.x + first.w);
    CHECK(second.x >= host.x + host.w);
    // Both GPU segments are on the graphics lane, so the split is a *gap in one lane* filled by another lane's block.
    CHECK(first.lane == QueueType::GRAPHICS);
    CHECK(second.lane == QueueType::GRAPHICS);
    CHECK(host.y != first.y);
}

TEST_CASE("RenderGraphLaneLayout: an async segment lands on its own lane and the handover spans lanes", "[LaneLayout]")
{
    // A host node sits between the two segments on purpose: with adjacent blocks the producer's right edge and the
    // consumer's left edge are the *same coordinate*, so an arrow with its ends swapped would still pass. That is how
    // the first version of this case failed to catch the mutation that swapped them - the gap is what makes the two
    // endpoints distinguishable.
    const TraceFixture fixture = TraceFixture()
                                     .Node("shadow", QueueType::GRAPHICS)
                                     .Node("upload", QueueType::CPU)
                                     .Node("bloomDown", QueueType::COMPUTE)
                                     .Node("bloomBlur", QueueType::COMPUTE)
                                     .Segment(QueueType::GRAPHICS, 0, 1)
                                     .Segment(QueueType::COMPUTE, 2, 4)
                                     .HostNode(1)
                                     .Edge(0, 1, "gbuffer.albedo", TraceEdgeKind::Transfer);

    const LaneLayout layout = BuildLaneLayout(fixture.trace, Viewport());

    REQUIRE(layout.lanes.size() == 3);
    // First appearance decides lane order, so a graphics-then-compute graph reads top to bottom.
    CHECK(layout.lanes[0] == QueueType::GRAPHICS);
    CHECK(layout.lanes[1] == QueueType::COMPUTE);

    const LaneRect& producer = BlockOfSegment(layout, 0);
    const LaneRect& consumer = BlockOfSegment(layout, 1);
    CHECK(producer.lane == QueueType::GRAPHICS);
    CHECK(consumer.lane == QueueType::COMPUTE);
    // The compute segment has two nodes against the graphics segment's one, so it is wider.
    CHECK(consumer.w > producer.w);
    // Strictly greater: the host block occupies the space between them, which is what makes the next two checks
    // meaningful rather than tautological.
    CHECK(consumer.x > producer.x + producer.w);

    REQUIRE(layout.arrows.size() == 1);
    const LaneArrow& arrow = layout.arrows[0];
    CHECK(arrow.from == QueueType::GRAPHICS);
    CHECK(arrow.to == QueueType::COMPUTE);
    CHECK(arrow.producerSegment == 0);
    CHECK(arrow.consumerSegment == 1);
    // Endpoints sit on the blocks, not on the lanes' origins: the arrow starts where the producer ends and finishes
    // where the consumer begins.
    CHECK(arrow.x0 == producer.x + producer.w);
    CHECK(arrow.x1 == consumer.x);
    CHECK(arrow.y0 == producer.y + Viewport().laneHeight * 0.5F);
    CHECK(arrow.y1 == consumer.y + Viewport().laneHeight * 0.5F);
    CHECK(arrow.label == "gbuffer.albedo (transfer)");
}

TEST_CASE("RenderGraphLaneLayout: zoom scales the blocks without reordering them", "[LaneLayout]")
{
    // Zoom is what makes a long graph readable, so it has to be an affine map: same order, same lanes, only wider.
    // A layout that re-fitted instead of scaling would keep every block the same width and change nothing.
    const TraceFixture fixture = TraceFixture()
                                     .Node("a")
                                     .Node("b")
                                     .Node("c")
                                     .Segment(QueueType::GRAPHICS, 0, 1)
                                     .Segment(QueueType::GRAPHICS, 1, 2)
                                     .Segment(QueueType::GRAPHICS, 2, 3)
                                     .Edge(0, 1, "x", TraceEdgeKind::QueueOrder);

    LaneViewport once = Viewport();
    LaneViewport twice = Viewport();
    twice.zoom = 2.0F;

    const LaneLayout unzoomed = BuildLaneLayout(fixture.trace, once);
    const LaneLayout zoomed = BuildLaneLayout(fixture.trace, twice);

    REQUIRE(unzoomed.blocks.size() == zoomed.blocks.size());
    for (std::size_t i = 0; i < unzoomed.blocks.size(); ++i)
    {
        CHECK(zoomed.blocks[i].segment == unzoomed.blocks[i].segment);
        CHECK(zoomed.blocks[i].lane == unzoomed.blocks[i].lane);
        CHECK(zoomed.blocks[i].w > unzoomed.blocks[i].w);
        // The gutter does not scale: labels stay put and only the lane area grows.
        CHECK(zoomed.blocks[i].x >= unzoomed.blocks[i].x);
    }
    CHECK(zoomed.width > unzoomed.width);
}

TEST_CASE("RenderGraphLaneLayout: blocks never overlap within a lane, and order follows node indices", "[LaneLayout]")
{
    // The invariant that makes the picture readable at any zoom. Asserted on the layout rather than on a screenshot,
    // because a screenshot cannot be a test.
    const TraceFixture fixture = TraceFixture()
                                     .Node("graphicsA")
                                     .Node("computeA")
                                     .Node("computeB")
                                     .Node("graphicsB")
                                     .Node("host")
                                     .Segment(QueueType::GRAPHICS, 0, 1)
                                     .Segment(QueueType::COMPUTE, 1, 3)
                                     .Segment(QueueType::GRAPHICS, 3, 4)
                                     .HostNode(4);

    for (const float zoom : {1.0F, 3.0F})
    {
        LaneViewport viewport = Viewport();
        viewport.zoom = zoom;
        const LaneLayout layout = BuildLaneLayout(fixture.trace, viewport);

        REQUIRE(layout.blocks.size() == 4);
        float previousEnd = 0.0F;
        for (const LaneRect& block : layout.blocks)
        {
            INFO("zoom " << zoom << " block " << block.segment);
            CHECK(block.x >= previousEnd);
            CHECK(block.w > 0.0F);
            previousEnd = block.x + block.w;
        }
        // Non-decreasing node order is what makes "left to right" mean "execution order".
        for (std::size_t i = 1; i < layout.blocks.size(); ++i)
        {
            CHECK(layout.blocks[i].firstNode >= layout.blocks[i - 1].firstNode);
        }
    }
}

TEST_CASE("RenderGraphLaneLayout: an empty execution produces no lanes and no blocks", "[LaneLayout]")
{
    // A graph whose work is all host-side, or an execution that recorded nothing, must not divide by zero or invent a
    // lane. This is the case a panel hits on its first frame.
    RenderGraphTrace empty;
    const LaneLayout layout = BuildLaneLayout(empty, Viewport());
    CHECK(layout.blocks.empty());
    CHECK(layout.arrows.empty());
    CHECK(layout.lanes.empty());
    CHECK(layout.height == 0.0F);

    // Host work alone still gets its lane: it is a thing that ran.
    RenderGraphTrace hostOnly;
    hostOnly.nodes.push_back(TraceNode{.index = 0, .name = "upload", .queueType = QueueType::CPU});
    hostOnly.cpuNodes.push_back(0);
    const LaneLayout hostLayout = BuildLaneLayout(hostOnly, Viewport());
    REQUIRE(hostLayout.lanes.size() == 1);
    CHECK(hostLayout.lanes[0] == QueueType::CPU);
    REQUIRE(hostLayout.blocks.size() == 1);
    CHECK(hostLayout.blocks[0].bHostBlock);
    CHECK(hostLayout.blocks[0].label == "upload");
}

TEST_CASE("RenderGraphLaneLayout: the lane view's selection follows the data, and only when it moves", "[LaneLayout]")
{
    // P1 from the #168 review, and N1 from its round 2. The first fix decremented on every push, which is only right
    // once the window is full: during the fill phase nothing is dropped, so the selection walked one execution earlier
    // per frame and settled on the *oldest* execution - a wrong picture pointed the other way. Both regimes are
    // asserted here, because either one alone accepts a function that is wrong in the other.
    //
    // Fill phase: nothing dropped, so the indices did not move and the selection must not either.
    CHECK(AdjustSelectionAfterPush(2, 4, /*bDropped=*/false) == 2);
    CHECK(AdjustSelectionAfterPush(0, 1, false) == 0);
    CHECK(AdjustSelectionAfterPush(-1, 1, false) == -1);

    // Full window: the oldest entry is dropped and every surviving index shifts down by one.
    CHECK(AdjustSelectionAfterPush(1, 5, true) == 0);  // followed its execution
    CHECK(AdjustSelectionAfterPush(4, 5, true) == 3);
    CHECK(AdjustSelectionAfterPush(0, 5, true) == 0);  // it *was* the dropped entry: show the oldest survivor
    CHECK(AdjustSelectionAfterPush(9, 5, true) == 4);  // out of range: clamped to newest retained

    // Nothing retained: no range to clamp into.
    CHECK(AdjustSelectionAfterPush(3, 0, true) == 3);

    // The fill-phase property that N1 needed, as a sequence rather than one step: many pushes into a window that is
    // not yet full leave the selection exactly where it was.
    int selected = 0;
    std::size_t size = 0;
    for (int i = 0; i < 10; ++i)
    {
        size += 1;
        CHECK(size <= 240);
        selected = AdjustSelectionAfterPush(selected, size, /*bDropped=*/false);
        CHECK(selected == 0);
    }
    CHECK(selected == 0);
}
