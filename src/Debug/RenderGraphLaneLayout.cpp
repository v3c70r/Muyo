#include "RenderGraphLaneLayout.h"

#include <algorithm>
#include <unordered_map>

namespace Muyo
{
namespace
{
using RenderGraph::QueueType;

/// Lanes are ordered by first appearance in the trace, so a graph's own order decides the picture rather than a fixed
/// table: a graphics-only graph has one lane, and an async graph shows whichever segment came first on top.
std::vector<QueueType> CollectLanes(const RenderGraph::RenderGraphTrace& trace)
{
    std::vector<QueueType> lanes;
    for (const RenderGraph::TraceSegment& segment : trace.segments)
    {
        if (std::find(lanes.begin(), lanes.end(), segment.queueType) == lanes.end())
        {
            lanes.push_back(segment.queueType);
        }
    }
    // A host node needs a lane of its own even when the graph has no segment at all.
    const bool bHasHostWork = !trace.cpuNodes.empty();
    if (bHasHostWork && std::find(lanes.begin(), lanes.end(), QueueType::CPU) == lanes.end())
    {
        lanes.push_back(QueueType::CPU);
    }
    return lanes;
}

/// Node names a block covers, for its label and tooltip.
std::string LabelFor(const RenderGraph::RenderGraphTrace& trace, std::size_t first, std::size_t count)
{
    std::string label;
    for (std::size_t i = first; i < first + count && i < trace.nodes.size(); ++i)
    {
        if (!label.empty())
        {
            label += ", ";
        }
        label += trace.nodes[i].name;
    }
    return label;
}
}  // namespace

LaneLayout BuildLaneLayout(const RenderGraph::RenderGraphTrace& trace, const LaneViewport& viewport)
{
    LaneLayout layout;
    layout.lanes = CollectLanes(trace);
    if (layout.lanes.empty())
    {
        return layout;
    }

    const auto laneIndex = [&layout](QueueType type)
    {
        const auto it = std::find(layout.lanes.begin(), layout.lanes.end(), type);
        return static_cast<std::size_t>(std::distance(layout.lanes.begin(), it));
    };
    const auto laneY = [&viewport, &laneIndex](QueueType type)
    { return static_cast<float>(laneIndex(type)) * (viewport.laneHeight + viewport.laneGap); };

    // ── Blocks, in execution order. ──────────────────────────────────────────────────────────────────────────────
    // Host nodes belong to no segment, so they are placed between the segments whose node indices they sit between.
    // That is what makes a CPU node's split visible: two blocks on one lane with a host block between them, rather
    // than one block that silently absorbed the pause (measured by the test).
    struct OrderedBlock
    {
        std::size_t firstNode = 0;
        QueueType lane = QueueType::GRAPHICS;
        std::size_t segment = 0;
        bool bHost = false;
    };
    std::vector<OrderedBlock> ordered;
    ordered.reserve(trace.segments.size() + trace.cpuNodes.size());
    for (const RenderGraph::TraceSegment& segment : trace.segments)
    {
        ordered.push_back(OrderedBlock{segment.begin, segment.queueType, segment.index, false});
    }
    for (const std::size_t node : trace.cpuNodes)
    {
        ordered.push_back(OrderedBlock{node, QueueType::CPU, node, true});
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const OrderedBlock& a, const OrderedBlock& b) { return a.firstNode < b.firstNode; });

    // Node counts give the relative widths, so a segment of four nodes is four times as wide as a single node.
    std::size_t totalNodes = 0;
    for (const OrderedBlock& block : ordered)
    {
        if (block.bHost)
        {
            totalNodes += 1;
            continue;
        }
        const RenderGraph::TraceSegment& segment = trace.segments[block.segment];
        totalNodes += std::max<std::size_t>(1, segment.end - segment.begin);
    }
    if (totalNodes == 0)
    {
        return layout;
    }

    const float usable = std::max(1.0F, viewport.width) * std::max(0.0F, viewport.zoom);
    const float unit = usable / static_cast<float>(totalNodes);

    float cursor = viewport.leftGutter;
    for (const OrderedBlock& block : ordered)
    {
        LaneRect rect;
        rect.lane = block.lane;
        rect.segment = block.segment;
        rect.bHostBlock = block.bHost;
        rect.y = laneY(block.lane);
        if (block.bHost)
        {
            rect.firstNode = block.firstNode;
            rect.nodeCount = 1;
            rect.x = cursor;
            rect.w = std::max(viewport.minSegmentWidth, unit);
            rect.label = LabelFor(trace, block.firstNode, 1);
        }
        else
        {
            const RenderGraph::TraceSegment& segment = trace.segments[block.segment];
            rect.firstNode = segment.begin;
            rect.nodeCount = std::max<std::size_t>(1, segment.end - segment.begin);
            rect.x = cursor;
            rect.w = std::max(viewport.minSegmentWidth, unit * static_cast<float>(rect.nodeCount));
            rect.label = LabelFor(trace, segment.begin, rect.nodeCount);
        }
        cursor += rect.w;
        layout.blocks.push_back(std::move(rect));
    }

    // ── Arrows, from the producer's right edge to the consumer's left edge. ─────────────────────────────────────
    // Looked up by segment index rather than by position in `layout.blocks`, because a host block between two segments
    // would shift every position after it.
    std::unordered_map<std::size_t, std::size_t> blockOfSegment;
    for (std::size_t i = 0; i < layout.blocks.size(); ++i)
    {
        if (!layout.blocks[i].bHostBlock)
        {
            blockOfSegment[layout.blocks[i].segment] = i;
        }
    }
    for (const RenderGraph::TraceSegment& segment : trace.segments)
    {
        for (const RenderGraph::TraceEdge& edge : segment.dependsOn)
        {
            const auto producerIt = blockOfSegment.find(edge.from);
            const auto consumerIt = blockOfSegment.find(edge.to);
            if (producerIt == blockOfSegment.end() || consumerIt == blockOfSegment.end())
            {
                continue;
            }
            const LaneRect& producer = layout.blocks[producerIt->second];
            const LaneRect& consumer = layout.blocks[consumerIt->second];
            LaneArrow arrow;
            arrow.from = producer.lane;
            arrow.to = consumer.lane;
            arrow.producerSegment = edge.from;
            arrow.consumerSegment = edge.to;
            arrow.x0 = producer.x + producer.w;
            arrow.x1 = consumer.x;
            arrow.y0 = producer.y + viewport.laneHeight * 0.5F;
            arrow.y1 = consumer.y + viewport.laneHeight * 0.5F;
            arrow.label = edge.resource.empty() ? std::string(RenderGraph::NameOf(edge.kind))
                                                : edge.resource + " (" + RenderGraph::NameOf(edge.kind) + ")";
            layout.arrows.push_back(std::move(arrow));
        }
    }

    layout.width = cursor;
    layout.height = static_cast<float>(layout.lanes.size()) * (viewport.laneHeight + viewport.laneGap);
    return layout;
}
}  // namespace Muyo
