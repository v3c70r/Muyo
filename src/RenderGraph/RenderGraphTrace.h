#pragma once
#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "RenderGraphExecutionPlan.h"
#include "RenderGraphNodeResource.h"
#include "RenderGraphResourceHandle.h"

namespace Muyo::RenderGraph
{
/// A time range recorded for a segment or a host-side node.
///
/// **Reserved and unfilled in the structural trace.** Every interval is invalid until timestamps are collected
/// (#56 phase 2), and `valid` is what distinguishes "this took no time" from "nobody measured it" - a zero would
/// read as instantaneous to anything consuming the trace. The field is present from the first version so the
/// recorded shape does not change when the measurements arrive.
struct TraceInterval
{
    bool valid = false;    ///< True once a begin and an end were measured.
    uint64_t beginNs = 0;  ///< Nanoseconds, scaled from ticks at the edge. Meaningless when `!valid`.
    uint64_t endNs = 0;    ///< Nanoseconds, scaled from ticks at the edge. Meaningless when `!valid`.
};

/// Timestamp capability of the queues a trace ran on.
///
/// Recorded so a trace states its own scope: a run whose queue family has no timestamps, or whose compute and
/// graphics timestamps are not comparable, cannot be quoted as if it did.
struct TraceTimestamps
{
    uint32_t validBitsGraphics = 0;  ///< `VkQueueFamilyProperties::timestampValidBits`; 0 means no timings at all.
    uint32_t validBitsCompute = 0;   ///< Same for the compute family; 0 means that lane is unmeasurable.
    double periodNs = 0.0;           ///< `timestampPeriod`: nanoseconds per tick.
    bool bSharedDomain = false;      ///< True when compute and graphics timestamps share one clock domain.
    bool bCalibrated = false;        ///< True when host and GPU clocks were aligned for this execution.
    bool bKnown = false;             ///< False in the structural trace: nothing has been queried yet.
};

/// What produced a trace and what it can therefore be compared against.
///
/// A trace that does not name its device and topology can be quoted outside the configuration it describes - the
/// same failure mode `[test device]` exists to prevent for test runs.
struct TraceScope
{
    std::string deviceName;          ///< `VkPhysicalDeviceProperties::deviceName`.
    uint32_t graphicsFamily = 0;     ///< Queue family running graphics work.
    uint32_t computeFamily = 0;      ///< Queue family running async compute work.
    bool bDedicatedCompute = false;  ///< False when there is one family, so no async compute at all.
    uint32_t inFlightCount = 1;      ///< Executions that may be in flight at once.
    TraceTimestamps timestamps;      ///< What can be timed on these queues.
};

/// One compiled node, as the trace records it.
struct TraceNode
{
    std::size_t index = 0;                      ///< Index into `CompiledRenderGraph::GetNodes()`.
    std::string name;                           ///< Node name, as declared.
    QueueType queueType = QueueType::GRAPHICS;  ///< Queue the node declared.
    bool bAsync = false;                        ///< True when the node opted into the dedicated async compute queue.
    std::vector<ResourceHandle> reads;          ///< Resources the node reads, in declaration order.
    std::vector<ResourceHandle> writes;         ///< Resources the node writes, in declaration order.
};

/// Why one segment must run after another.
enum class TraceEdgeKind : uint8_t
{
    Transfer,    ///< A resource is handed over across a queue-family boundary.
    QueueOrder,  ///< Both segments submit to the same queue, so program order already orders them.
};

/// An ordering constraint between two segments, **with its cause**.
///
/// The cause is the point: a dependency without the resource that forces it is a fact nobody can act on, and it
/// is free to record where the barrier or handover is derived.
struct TraceEdge
{
    std::size_t from = 0;                            ///< Segment that must complete first.
    std::size_t to = 0;                              ///< Segment that must wait.
    ResourceHandle resource;                         ///< Resource forcing the order; empty for `QueueOrder`.
    TraceEdgeKind kind = TraceEdgeKind::QueueOrder;  ///< Why the order exists.
};

/// One barrier recorded inside a segment, in the forms a reader can interpret.
///
/// Stages and accesses are stored as **names**, not bitmasks: a reviewer or a model reading `EARLY_FRAGMENT` can
/// act on it, where `0x100` is a puzzle. The mask itself is not carried - the trace is for reading, and the
/// barrier is re-derivable from the uses.
struct TraceBarrier
{
    ResourceHandle resource;     ///< Resource the barrier addresses; empty for a device-wide memory barrier.
    const char* kind = "image";  ///< `image`, `buffer` or `memory`.
    std::string srcStage;        ///< Source stage mask, rendered by `NameOfPipelineStages`.
    std::string dstStage;        ///< Destination stage mask.
    std::string srcAccess;       ///< Source access mask, rendered by `NameOfAccesses`.
    std::string dstAccess;       ///< Destination access mask.
    uint32_t srcFamily = VK_QUEUE_FAMILY_IGNORED;  ///< Source family for a handover, `IGNORED` otherwise.
    uint32_t dstFamily = VK_QUEUE_FAMILY_IGNORED;  ///< Destination family for a handover, `IGNORED` otherwise.
};

/// A contiguous run of compiled nodes that executes on one queue, as recorded.
struct TraceSegment
{
    std::size_t index = 0;                      ///< Segment index, in execution order.
    QueueType queueType = QueueType::GRAPHICS;  ///< Resolved queue the segment runs on.
    uint32_t family = 0;                        ///< Vulkan queue family the queue key resolved to.
    std::size_t begin = 0;                      ///< First compiled node index (inclusive).
    std::size_t end = 0;                        ///< One past the last compiled node index.
    std::vector<TraceEdge> dependsOn;           ///< Edges forcing this segment to wait, with their causes.
    std::vector<TraceBarrier> barriers;         ///< Barriers recorded in this segment, in emission order.
    TraceInterval gpu;                          ///< Reserved time range; invalid until timestamps exist.
};

/// A timeline value signalled when a segment completes.
struct TraceSignal
{
    std::size_t segment = 0;                ///< Segment whose completion is signalled.
    QueueType queue = QueueType::GRAPHICS;  ///< Queue the value belongs to.
    uint64_t value = 0;                     ///< Timeline value signalled.
};

/// What a segment waits for, and why.
struct TraceWait
{
    std::size_t segment = 0;                ///< Segment that waits.
    QueueType queue = QueueType::GRAPHICS;  ///< Queue the waited value belongs to.
    uint64_t value = 0;                     ///< Timeline value waited for.
    const char* reason = "";                ///< What demanded the wait: `timeline`, `caller`, `slot`.
};

/// One execution of a compiled graph, as data.
///
/// Read-only by construction: it is derived from `CompiledRenderGraph`'s const accessors and never written back.
/// It is a **value**, not a view over the graph - the graph is rebuilt every frame, consumers keep history, and
/// out-of-process consumers need something they can serialise and diff.
///
/// Three layers live in one object, and only the first is populated today:
///
/// - **structural**: nodes, segments, transfers, dependency edges and barriers;
/// - **temporal**: `TraceSegment::gpu` and the host intervals - reserved, `valid == false`;
/// - **derived**: occupancy, bubbles and critical path are computed by consumers from the two above.
struct RenderGraphTrace
{
    /// Wire format version of the serialised form. A *format* version, not the library's version: a bump is a
    /// compatibility statement to tools, not to linkers.
    uint32_t schema = 1;
    uint64_t execution = 0;              ///< Execution counter; engine-lifetime, not per-frame.
    TraceScope scope;                    ///< Device and topology that produced this trace.
    std::vector<TraceNode> nodes;        ///< Every compiled node, in compiled order.
    std::vector<std::size_t> cpuNodes;   ///< Host-side nodes, which run before any GPU segment.
    std::vector<TraceSegment> segments;  ///< GPU segments, in execution order.
    std::vector<TraceSignal> signals;    ///< Timeline values signalled, filled at submit.
    std::vector<TraceWait> waits;        ///< Waits, filled at submit.
};

/// One resolved use of a node, as the trace needs it.
struct TraceUseView
{
    std::string_view handle;                   ///< Resource name.
    ResourceIOType io = ResourceIOType::READ;  ///< Read, write or both; `IsWrite` decides which list it lands in.
};

/// The parts of a compiled node the trace needs.
///
/// A view rather than `CompiledRenderGraphNode` so the trace does not depend on the compiled graph's full
/// definition - the record is about names, queues and uses, and nothing else.
struct TraceNodeView
{
    std::string_view name;                      ///< Node name.
    QueueType queueType = QueueType::GRAPHICS;  ///< Queue the node declared.
    bool bAsync = false;                        ///< True when the node opted into async compute.
    std::span<const TraceUseView> uses;         ///< Resolved uses, in declaration order.
};

/// Build the structural part of a trace from a plan and the compiled nodes it refers to.
///
/// Device-free: the plan carries segments and transfers, and the views carry names and queues, so a test can
/// build the same record a device would without creating one. Dependency edges are derived here with their
/// causes - a transfer for each cross-family handover, program order for consecutive segments on one queue.
/// @param plan Plan the executor will run.
/// @param nodes Compiled nodes the plan indexes, in compiled order.
/// @param scope Device and topology producing this trace; nothing is queried from it.
/// @return The structural trace, with every temporal field still invalid.
RenderGraphTrace TracePlan(const RenderGraphExecutionPlan& plan, std::span<const TraceNodeView> nodes,
                           const TraceScope& scope);

/// @param kind Kind to name.
/// @return The name used in serialised traces, e.g. `transfer`.
const char* NameOf(TraceEdgeKind kind);

/// @param type Queue type to name.
/// @return The name used in serialised traces, e.g. `compute`.
const char* NameOf(QueueType type);

/// Render a stage mask as names, joined by `|`, with `NONE` for an empty mask.
///
/// An unrecognised bit renders as `0x…` rather than disappearing, so a new stage is visible in a trace instead of
/// silently missing from it.
/// @param stages Stage mask to render.
/// @return A name such as `COLOR_ATTACHMENT_OUTPUT` or `EARLY_FRAGMENT|LATE_FRAGMENT`.
std::string NameOfPipelineStages(VkPipelineStageFlags2 stages);

/// Render an access mask as names, joined by `|`, with `NONE` for an empty mask.
/// @param accesses Access mask to render.
/// @return A name such as `SHADER_READ`, with unrecognised bits rendered as `0x…`.
std::string NameOfAccesses(VkAccessFlags2 accesses);

/// One field of the serialised object: its key, and what renders its value.
///
/// The table exists so the field list is **data**. The JSON emitter walks it, and a test asserts every key
/// appears in the output, so a struct member that is added without being emitted fails the suite instead of
/// quietly missing from the contract.
struct TraceField
{
    const char* name;                                ///< JSON key.
    std::string (*render)(const RenderGraphTrace&);  ///< Renders the value, already JSON-encoded.
};

/// @return Every field the serialised trace carries, in emission order.
std::span<const TraceField> TraceFields();

/// Serialise a trace as one JSON object on one line (newline-delimited JSON).
///
/// NDJSON rather than an array so a capture file stays streamable and `tail -1` is a valid document.
/// @param trace Trace to serialise.
/// @return The object, without a trailing newline.
std::string ToJson(const RenderGraphTrace& trace);

/// Render a trace as aligned text: lanes, node order, dependency edges and the barrier table.
///
/// This is what a person reads in a log and what the debug panel's text tab shows, so the two can never disagree.
/// @param trace Trace to render.
/// @return A multi-line block.
std::string ToText(const RenderGraphTrace& trace);

/// A segment, as a graph viewer wants it.
struct TraceGraphNode
{
    std::size_t segment = 0;  ///< Segment index.
    std::string label;        ///< Lane and node names, e.g. `gfx: shadow, gbuffer`.
};

/// An ordering constraint, as a graph viewer wants it.
struct TraceGraphEdge
{
    std::size_t from = 0;  ///< Producing segment.
    std::size_t to = 0;    ///< Consuming segment.
    std::string label;     ///< Cause, e.g. `bloom.mips (transfer)`.
};

/// The topology of one execution, independent of any renderer's syntax.
struct TraceGraph
{
    std::vector<TraceGraphNode> nodes;  ///< Segments.
    std::vector<TraceGraphEdge> edges;  ///< Dependency edges, labelled with their cause.
};

/// @param trace Trace to project.
/// @return Segments and edges, unlabelled by any diagram syntax.
TraceGraph ToGraph(const RenderGraphTrace& trace);

/// Render a graph as Graphviz DOT, which `dot -Tsvg` turns into an image with no browser involved.
/// @param graph Graph to render.
/// @return A DOT document.
std::string ToDot(const TraceGraph& graph);

/// Render a graph as a Mermaid flowchart, which GitHub renders inline in issues, pull requests and markdown.
/// @param graph Graph to render.
/// @return A Mermaid document.
std::string ToMermaid(const TraceGraph& graph);
}  // namespace Muyo::RenderGraph
