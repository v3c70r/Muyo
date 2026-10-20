#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "RenderGraph/RenderGraphTrace.h"

namespace Muyo
{
/// How much space the lane view has, and how it is subdivided.
///
/// Widths are in view units, not pixels: the caller scales the result to whatever its framebuffer is, which keeps this
/// function independent of the window and of ImGui.
struct LaneViewport
{
    float width = 1000.0F;         ///< Horizontal space for the lanes, excluding `leftGutter`.
    float leftGutter = 72.0F;      ///< Space reserved for lane labels.
    float laneHeight = 24.0F;      ///< Vertical space per lane.
    float laneGap = 6.0F;          ///< Vertical space between lanes.
    float minSegmentWidth = 8.0F;  ///< A one-node segment still has to be clickable.
    float zoom = 1.0F;             ///< Horizontal scale; 1 fits the viewport, 2 scrolls.
};

/// One block in the lane view: a segment, or a host-side node.
struct LaneRect
{
    RenderGraph::QueueType lane = RenderGraph::QueueType::GRAPHICS;  ///< Lane the block is drawn in.
    std::size_t segment = 0;    ///< Segment index, or the node index for a host block (`bHostBlock`).
    std::size_t firstNode = 0;  ///< First compiled node the block covers.
    std::size_t nodeCount = 0;  ///< How many nodes it covers; its width follows from this.
    bool bHostBlock = false;    ///< True for a host-side node, which belongs to no segment.
    float x = 0.0F;             ///< Left edge, in view units.
    float w = 0.0F;             ///< Width, in view units.
    float y = 0.0F;             ///< Top edge, in view units.
    std::string label;          ///< Node names, for the block's tooltip.
};

/// A dependency between two blocks, drawn from one lane to another.
struct LaneArrow
{
    RenderGraph::QueueType from = RenderGraph::QueueType::GRAPHICS;  ///< Lane the producer is drawn in.
    RenderGraph::QueueType to = RenderGraph::QueueType::GRAPHICS;    ///< Lane the consumer is drawn in.
    std::size_t producerSegment = 0;                                 ///< Producing segment.
    std::size_t consumerSegment = 0;                                 ///< Consuming segment.
    float x0 = 0.0F;                                                 ///< Tail, at the producer's right edge.
    float x1 = 0.0F;                                                 ///< Head, at the consumer's left edge.
    float y0 = 0.0F;                                                 ///< Tail's lane centre.
    float y1 = 0.0F;                                                 ///< Head's lane centre.
    std::string label;  ///< The resource that forces the order, and the edge kind.
};

/// Lanes and blocks for one execution, in view units.
struct LaneLayout
{
    std::vector<RenderGraph::QueueType> lanes;  ///< Lane order, top to bottom.
    std::vector<LaneRect> blocks;               ///< Segments and host blocks, in execution order.
    std::vector<LaneArrow> arrows;              ///< Dependency edges between segments.
    float width = 0.0F;                         ///< Total width used, including the gutter.
    float height = 0.0F;                        ///< Total height used.
};

/// Lay one execution out as lanes.
///
/// **This is a sequence view, not a time view.** A block's width follows its node count, so the picture answers
/// "what ran, in what order, on which queue, and what was handed to what" - which is what validates a plan by eye.
/// Durations are phase 2 (#56): when they exist, only the width formula changes here and the widget keeps its shape.
///
/// Device-free and pure, so the geometry is asserted without a window, an ImGui context or a GPU. That split is the
/// point: the panel's *drawing* cannot be tested in this repository, and its *layout* can.
/// @param trace   Execution to lay out.
/// @param viewport Space available, zoom and per-lane metrics.
/// @return Blocks, arrows and lane order, in view units.
LaneLayout BuildLaneLayout(const RenderGraph::RenderGraphTrace& trace, const LaneViewport& viewport);
}  // namespace Muyo
