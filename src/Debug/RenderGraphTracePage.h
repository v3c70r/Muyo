#pragma once
#include <cstddef>
#include <deque>
#include <string>

#include "DebugUI.h"
#include "RenderGraph/RenderGraphTrace.h"
#include "RenderGraphLaneLayout.h"

namespace Muyo
{
/// @brief Where the selection points after one execution was appended and the oldest may have been dropped.
///
/// A free function because the alternative is untested arithmetic inside `Push`: dropping the oldest entry shifts every
/// index down by one, so a selection that was not adjusted silently slides to the *successor* execution - a wrong
/// picture rather than a crash, which is the hardest kind to notice in a UI.
///
/// @param selected Current selection; negative means "nothing selected yet".
/// @param size     Retained count after the append and the drop.
/// @return The adjusted selection: unchanged for a negative input, decremented when it was not already the oldest, and
///         clamped into the retained range.
int AdjustSelectionAfterPush(int selected, std::size_t size);

/// Draws recorded executions as lanes, and keeps the history to scrub through.
///
/// Not a `RenderPassUI` page: the graph app has no `RenderPassManager`, and this is deliberately independent of one
/// (#48 counts the dependency the other way). It is an `IDebugUIPage` so either app can host it, and the graph app
/// constructs it directly.
///
/// **This is a sequence view, not a time view.** Block widths follow node counts; the picture answers "what ran, in
/// what order, on which queue, and what was handed to what". Durations are phase 2 (#56), and only the width formula
/// in `BuildLaneLayout` changes then.
///
/// Thread affinity: `Push` is called from the builder's trace sink during `Execute`, and `Render` from the UI pass.
/// Both are the render thread in this engine, so the history is not locked. A caller that executes on another thread
/// must hand the trace over itself.
class RenderGraphTracePage : public IDebugUIPage
{
public:
    /// @param sName Window title.
    /// @param capacity How many executions to keep for scrubbing. The oldest is dropped.
    explicit RenderGraphTracePage(std::string sName, std::size_t capacity = 240);

    /// Record one execution.
    /// @param trace Trace handed over by the builder's sink; the page keeps it.
    void Push(RenderGraph::RenderGraphTrace trace);

    /// @return How many executions are retained.
    std::size_t GetCount() const { return m_history.size(); }

    void Render() const override;
    bool ShouldRender() const override { return true; }
    ~RenderGraphTracePage() override = default;

private:
    /// Draw one execution's lanes into the current window.
    /// @param trace Execution to draw.
    /// @param viewport Metrics for the available space.
    /// @param bShowBarriers Whether to expand the per-segment barrier tables.
    void DrawLanes(const RenderGraph::RenderGraphTrace& trace, const LaneViewport& viewport, bool bShowBarriers) const;

    std::deque<RenderGraph::RenderGraphTrace> m_history;  ///< Retained executions, oldest first.
    std::size_t m_capacity = 240;                         ///< Maximum retained.
    mutable int m_selected = -1;                          ///< Index into the history; -1 selects the newest each frame.
    mutable float m_zoom = 1.0F;                          ///< Horizontal scale for the lane view.
    mutable bool m_bShowBarriers = false;                 ///< Whether the barrier tables are expanded.
};
}  // namespace Muyo
