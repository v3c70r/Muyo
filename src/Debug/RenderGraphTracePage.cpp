#include "RenderGraphTracePage.h"

#include <imgui.h>

#include <algorithm>
#include <utility>

namespace Muyo
{
namespace
{
/// Colour per queue, so a lane is recognisable without reading its label.
ImU32 LaneColor(RenderGraph::QueueType lane)
{
    switch (lane)
    {
        case RenderGraph::QueueType::GRAPHICS:
            return IM_COL32(70, 110, 170, 220);
        case RenderGraph::QueueType::COMPUTE:
            return IM_COL32(60, 150, 110, 220);
        case RenderGraph::QueueType::COPY:
            return IM_COL32(160, 130, 60, 220);
        case RenderGraph::QueueType::RAY_TRACING:
            return IM_COL32(150, 90, 160, 220);
        case RenderGraph::QueueType::CPU:
            return IM_COL32(120, 120, 130, 220);
        case RenderGraph::QueueType::COUNT:
            break;
    }
    return IM_COL32(120, 120, 120, 220);
}

/// Name for a lane label, short enough for the gutter.
const char* LaneLabel(RenderGraph::QueueType lane)
{
    switch (lane)
    {
        case RenderGraph::QueueType::GRAPHICS:
            return "gfx";
        case RenderGraph::QueueType::COMPUTE:
            return "compute";
        case RenderGraph::QueueType::COPY:
            return "copy";
        case RenderGraph::QueueType::RAY_TRACING:
            return "rt";
        case RenderGraph::QueueType::CPU:
            return "cpu";
        case RenderGraph::QueueType::COUNT:
            break;
    }
    return "?";
}
}  // namespace

RenderGraphTracePage::RenderGraphTracePage(std::string sName, std::size_t capacity)
    : IDebugUIPage(std::move(sName)), m_capacity(std::max<std::size_t>(1, capacity))
{
}

void RenderGraphTracePage::Push(RenderGraph::RenderGraphTrace trace)
{
    m_history.push_back(std::move(trace));
    while (m_history.size() > m_capacity)
    {
        m_history.pop_front();
    }
    // A selection shifted out of the window moves with the data rather than pointing at a different execution.
    if (m_selected >= 0 && static_cast<std::size_t>(m_selected) >= m_history.size())
    {
        m_selected = static_cast<int>(m_history.size()) - 1;
    }
}

void RenderGraphTracePage::Render() const
{
    ImGui::Begin(m_sName.c_str());
    if (m_history.empty())
    {
        ImGui::TextUnformatted("No executions recorded. Install a trace sink on the builder (SetTraceSink).");
        ImGui::End();
        return;
    }

    // Selection is newest-first in the UI and oldest-first in the history, because a reader wants "how did the last
    // frame look" by default and "step back" as an explicit act.
    if (m_selected < 0 || static_cast<std::size_t>(m_selected) >= m_history.size())
    {
        m_selected = static_cast<int>(m_history.size()) - 1;
    }
    int selected = m_selected;
    const bool bNewest = (static_cast<std::size_t>(selected) + 1 == m_history.size());

    ImGui::Text("%zu execution(s) retained", m_history.size());
    ImGui::SameLine();
    // A scrub control: the value is the execution's own label, not its index, so a reader sees the number the trace
    // carries rather than a position in a ring buffer.
    ImGui::SetNextItemWidth(120.0F);
    if (ImGui::SliderInt("##execution", &selected, 0, static_cast<int>(m_history.size()) - 1, ""))
    {
        m_selected = selected;
    }
    ImGui::SameLine();
    ImGui::Text("execution %llu%s", static_cast<unsigned long long>(m_history[selected].execution),
                bNewest ? " (newest)" : "");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0F);
    ImGui::SliderFloat("zoom", &m_zoom, 1.0F, 8.0F, "%.1fx");
    ImGui::SameLine();
    ImGui::Checkbox("barriers", &m_bShowBarriers);

    const RenderGraph::RenderGraphTrace& trace = m_history[static_cast<std::size_t>(selected)];
    // The scope line, because the same widget can be showing a run from a device with a dedicated compute family and
    // one without: the picture means different things, exactly like a `[test device]` line.
    ImGui::TextDisabled("%s  |  families gfx=%u compute=%u  |  dedicated-compute %s  |  in-flight %u",
                        trace.scope.deviceName.c_str(), trace.scope.graphicsFamily, trace.scope.computeFamily,
                        trace.scope.bDedicatedCompute ? "yes" : "no", trace.scope.inFlightCount);

    if (ImGui::BeginChild("##lanes", ImVec2(0, 0), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysVerticalScrollbar))
    {
        LaneViewport viewport;
        viewport.width = std::max(ImGui::GetContentRegionAvail().x, 320.0F);
        viewport.zoom = m_zoom;
        DrawLanes(trace, viewport, m_bShowBarriers);
    }
    ImGui::EndChild();
    ImGui::End();
}

void RenderGraphTracePage::DrawLanes(const RenderGraph::RenderGraphTrace& trace, const LaneViewport& viewport,
                                     bool bShowBarriers) const
{
    const LaneLayout layout = BuildLaneLayout(trace, viewport);
    if (layout.blocks.empty())
    {
        ImGui::TextUnformatted("This execution has no GPU work and no host nodes.");
        return;
    }

    ImDrawList* pDraw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    // Lane labels first, so they are not overdrawn by a long block.
    for (std::size_t lane = 0; lane < layout.lanes.size(); ++lane)
    {
        const float y = origin.y + static_cast<float>(lane) * (viewport.laneHeight + viewport.laneGap);
        pDraw->AddText(ImVec2(origin.x, y + viewport.laneHeight * 0.25F), IM_COL32(200, 200, 200, 255),
                       LaneLabel(layout.lanes[lane]));
    }

    for (const LaneRect& block : layout.blocks)
    {
        const ImVec2 topLeft(origin.x + block.x, origin.y + block.y);
        const ImVec2 bottomRight(topLeft.x + block.w, topLeft.y + viewport.laneHeight);
        pDraw->AddRectFilled(topLeft, bottomRight, LaneColor(block.lane), 3.0F);
        // A host block is drawn hollow: it is the thing that explains a gap in a GPU lane, so it must not look like
        // GPU work.
        if (block.bHostBlock)
        {
            pDraw->AddRect(topLeft, bottomRight, IM_COL32(220, 220, 220, 200), 3.0F);
        }

        // A segment's label is the node names, truncated: the tooltip has all of them.
        if (block.w > 24.0F && !block.label.empty())
        {
            const ImVec2 textPos(topLeft.x + 4.0F, topLeft.y + viewport.laneHeight * 0.18F);
            pDraw->PushClipRect(topLeft, bottomRight, true);
            pDraw->AddText(textPos, IM_COL32(235, 235, 235, 255), block.label.c_str());
            pDraw->PopClipRect();
        }

        // Hovering a block names it in full, which is how a truncated label stays usable.
        if (ImGui::IsMouseHoveringRect(topLeft, bottomRight))
        {
            ImGui::BeginTooltip();
            if (block.bHostBlock)
            {
                ImGui::Text("host node %zu: %s", block.firstNode, block.label.c_str());
            }
            else
            {
                ImGui::Text("segment %zu on %s, nodes %zu..%zu", block.segment, LaneLabel(block.lane), block.firstNode,
                            block.firstNode + block.nodeCount);
                ImGui::TextUnformatted(block.label.c_str());
            }
            ImGui::EndTooltip();
        }
    }

    for (const LaneArrow& arrow : layout.arrows)
    {
        const ImVec2 tail(origin.x + arrow.x0, origin.y + arrow.y0);
        const ImVec2 head(origin.x + arrow.x1, origin.y + arrow.y1);
        // A handover between lanes is curved so the two lanes stay distinguishable; an ordering within one lane is a
        // short horizontal link along that lane.
        const bool bCrossLane = arrow.from != arrow.to;
        if (bCrossLane)
        {
            const float midX = (tail.x + head.x) * 0.5F;
            pDraw->AddBezierCubic(tail, ImVec2(midX, tail.y), ImVec2(midX, head.y), head, IM_COL32(230, 200, 90, 220),
                                  1.5F);
        }
        else
        {
            pDraw->AddLine(tail, head, IM_COL32(230, 200, 90, 220), 1.5F);
        }
        pDraw->AddCircleFilled(head, 2.5F, IM_COL32(230, 200, 90, 255));
        if (!arrow.label.empty() && ImGui::IsMouseHoveringRect(tail, head))
        {
            ImGui::BeginTooltip();
            ImGui::Text("segment %zu -> %zu", arrow.producerSegment, arrow.consumerSegment);
            ImGui::TextUnformatted(arrow.label.c_str());
            ImGui::EndTooltip();
        }
    }

    // Reserve the vertical space the lanes used, so the child scrolls.
    ImGui::Dummy(ImVec2(layout.width, layout.height + viewport.laneHeight));

    if (!bShowBarriers)
    {
        return;
    }
    for (const RenderGraph::TraceSegment& segment : trace.segments)
    {
        ImGui::PushID(static_cast<int>(segment.index));
        const std::string header = "segment " + std::to_string(segment.index) + " (" + LaneLabel(segment.queueType) +
                                   ", " + std::to_string(segment.barriers.size()) + " barrier(s))";
        if (ImGui::CollapsingHeader(header.c_str()))
        {
            if (segment.barriers.empty())
            {
                ImGui::TextDisabled("no barriers recorded");
            }
            for (const RenderGraph::TraceBarrier& barrier : segment.barriers)
            {
                ImGui::Text("%s  %s  %s/%s  %s->%s",
                            barrier.resource.empty() ? "<device-wide>" : barrier.resource.c_str(),
                            RenderGraph::NameOf(barrier.kind), barrier.srcStage.c_str(), barrier.dstStage.c_str(),
                            barrier.srcAccess.c_str(), barrier.dstAccess.c_str());
            }
        }
        ImGui::PopID();
    }
}
}  // namespace Muyo
