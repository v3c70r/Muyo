#include "RenderGraphTrace.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace Muyo::RenderGraph
{
namespace
{
/// A stage bit and the name it renders as.
struct StageName
{
    VkPipelineStageFlags2 bit;
    const char* name;
};

/// Every stage the graph emits a barrier for. Kept as a table so an unknown bit falls through to hex rather than
/// disappearing - a stage that silently renders as nothing is worse than one that renders as `0x…`.
constexpr std::array<StageName, 20> K_STAGE_NAMES{{
    {VK_PIPELINE_STAGE_2_NONE, "NONE"},
    {VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, "TOP_OF_PIPE"},
    {VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, "DRAW_INDIRECT"},
    {VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT, "VERTEX_INPUT"},
    {VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, "VERTEX_SHADER"},
    {VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT, "TESSELLATION_CONTROL_SHADER"},
    {VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT, "TESSELLATION_EVALUATION_SHADER"},
    {VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT, "GEOMETRY_SHADER"},
    {VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT, "TASK_SHADER"},
    {VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT, "MESH_SHADER"},
    {VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, "FRAGMENT_SHADER"},
    {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, "EARLY_FRAGMENT"},
    {VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, "LATE_FRAGMENT"},
    {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, "COLOR_ATTACHMENT_OUTPUT"},
    {VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, "COMPUTE_SHADER"},
    {VK_PIPELINE_STAGE_2_TRANSFER_BIT, "TRANSFER"},
    {VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, "RAY_TRACING_SHADER"},
    {VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, "ACCELERATION_STRUCTURE_BUILD"},
    {VK_PIPELINE_STAGE_2_HOST_BIT, "HOST"},
    {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, "ALL_COMMANDS"},
}};

/// An access bit and the name it renders as.
struct AccessName
{
    VkAccessFlags2 bit;
    const char* name;
};

/// Every access the graph emits a barrier for, for the same reason as `K_STAGE_NAMES`.
constexpr std::array<AccessName, 16> K_ACCESS_NAMES{{
    {VK_ACCESS_2_NONE, "NONE"},
    {VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, "INDIRECT_COMMAND_READ"},
    {VK_ACCESS_2_INDEX_READ_BIT, "INDEX_READ"},
    {VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT, "VERTEX_ATTRIBUTE_READ"},
    {VK_ACCESS_2_UNIFORM_READ_BIT, "UNIFORM_READ"},
    {VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT, "INPUT_ATTACHMENT_READ"},
    {VK_ACCESS_2_SHADER_READ_BIT, "SHADER_READ"},
    {VK_ACCESS_2_SHADER_WRITE_BIT, "SHADER_WRITE"},
    {VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT, "COLOR_ATTACHMENT_READ"},
    {VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, "COLOR_ATTACHMENT_WRITE"},
    {VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT, "DEPTH_STENCIL_ATTACHMENT_READ"},
    {VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, "DEPTH_STENCIL_ATTACHMENT_WRITE"},
    {VK_ACCESS_2_TRANSFER_READ_BIT, "TRANSFER_READ"},
    {VK_ACCESS_2_TRANSFER_WRITE_BIT, "TRANSFER_WRITE"},
    {VK_ACCESS_2_HOST_WRITE_BIT, "HOST_WRITE"},
    {VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR, "ACCELERATION_STRUCTURE_READ"},
}};

/// Render one mask through a name table, appending any unrecognised bits as hex.
template <typename Mask, typename Name, std::size_t N>
std::string RenderMask(Mask mask, const std::array<Name, N>& names)
{
    if (mask == 0)
    {
        return "NONE";
    }

    std::string result;
    Mask remaining = mask;
    for (const Name& entry : names)
    {
        if (entry.bit == 0 || (mask & entry.bit) == 0)
        {
            continue;
        }
        if (!result.empty())
        {
            result += '|';
        }
        result += entry.name;
        remaining &= ~entry.bit;
    }

    if (remaining != 0)
    {
        // A bit with no name: print it rather than dropping it, so a stage the table has not learned yet shows up
        // in the trace instead of vanishing from it.
        char buffer[24];
        std::snprintf(buffer, sizeof(buffer), "0x%llx", static_cast<unsigned long long>(remaining));
        if (!result.empty())
        {
            result += '|';
        }
        result += buffer;
    }
    return result;
}

/// Append a JSON string literal, escaping what the format requires.
void AppendJsonString(std::string& out, std::string_view text)
{
    out += '"';
    for (const char c : text)
    {
        switch (c)
        {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(c));
                    out += buffer;
                }
                else
                {
                    out += c;
                }
                break;
        }
    }
    out += '"';
}

/// Append a comma-separated array rendered by `renderOne`.
template <typename T, typename Fn>
void AppendJsonArray(std::string& out, const std::vector<T>& items, Fn renderOne)
{
    out += '[';
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (i != 0)
        {
            out += ',';
        }
        renderOne(out, items[i]);
    }
    out += ']';
}

/// Append a comma-separated array of strings.
void AppendJsonStringArray(std::string& out, const std::vector<ResourceHandle>& items)
{
    AppendJsonArray(out, items, [](std::string& sink, const ResourceHandle& item) { AppendJsonString(sink, item); });
}

std::string RenderSchema(const RenderGraphTrace& trace) { return std::to_string(trace.schema); }

std::string RenderExecution(const RenderGraphTrace& trace) { return std::to_string(trace.execution); }

std::string RenderCapabilities(const RenderGraphTrace&)
{
    // `timings` is the capability that tells a consumer whether the temporal fields mean anything. It is false
    // until timestamps are collected, so a reader never has to guess from a zero.
    return R"({"timings":false})";
}

std::string RenderScope(const RenderGraphTrace& trace)
{
    const TraceScope& scope = trace.scope;
    std::string out = R"({"device":)";
    AppendJsonString(out, scope.deviceName);
    out += ",\"graphics_family\":" + std::to_string(scope.graphicsFamily);
    out += ",\"compute_family\":" + std::to_string(scope.computeFamily);
    out += std::string(",\"dedicated_compute\":") + (scope.bDedicatedCompute ? "true" : "false");
    out += ",\"in_flight\":" + std::to_string(scope.inFlightCount);
    out += R"(,"timestamps":{"known":)" + std::string(scope.timestamps.bKnown ? "true" : "false");
    out += ",\"valid_bits_graphics\":" + std::to_string(scope.timestamps.validBitsGraphics);
    out += ",\"valid_bits_compute\":" + std::to_string(scope.timestamps.validBitsCompute);
    out += ",\"period_ns\":" + std::to_string(scope.timestamps.periodNs);
    out += std::string(",\"shared_domain\":") + (scope.timestamps.bSharedDomain ? "true" : "false");
    out += std::string(",\"calibrated\":") + (scope.timestamps.bCalibrated ? "true" : "false");
    out += "}}";
    return out;
}

std::string RenderNodes(const RenderGraphTrace& trace)
{
    std::string out;
    AppendJsonArray(out, trace.nodes,
                    [](std::string& sink, const TraceNode& node)
                    {
                        sink += R"({"index":)" + std::to_string(node.index);
                        sink += ",\"name\":";
                        AppendJsonString(sink, node.name);
                        sink += ",\"queue\":";
                        AppendJsonString(sink, NameOf(node.queueType));
                        sink += std::string(",\"async\":") + (node.bAsync ? "true" : "false");
                        sink += ",\"reads\":";
                        AppendJsonStringArray(sink, node.reads);
                        sink += ",\"writes\":";
                        AppendJsonStringArray(sink, node.writes);
                        sink += '}';
                    });
    return out;
}

std::string RenderCpuNodes(const RenderGraphTrace& trace)
{
    std::string out = "[";
    for (std::size_t i = 0; i < trace.cpuNodes.size(); ++i)
    {
        if (i != 0)
        {
            out += ',';
        }
        out += std::to_string(trace.cpuNodes[i]);
    }
    out += ']';
    return out;
}

std::string RenderSegments(const RenderGraphTrace& trace)
{
    std::string out;
    AppendJsonArray(out, trace.segments,
                    [](std::string& sink, const TraceSegment& segment)
                    {
                        sink += R"({"index":)" + std::to_string(segment.index);
                        sink += ",\"queue\":";
                        AppendJsonString(sink, NameOf(segment.queueType));
                        sink += ",\"family\":" + std::to_string(segment.family);
                        sink += ",\"begin\":" + std::to_string(segment.begin);
                        sink += ",\"end\":" + std::to_string(segment.end);
                        sink += ",\"depends_on\":";
                        AppendJsonArray(sink, segment.dependsOn,
                                        [](std::string& edgeSink, const TraceEdge& edge)
                                        {
                                            edgeSink += R"({"from":)" + std::to_string(edge.from);
                                            edgeSink += ",\"to\":" + std::to_string(edge.to);
                                            edgeSink += ",\"resource\":";
                                            AppendJsonString(edgeSink, edge.resource);
                                            edgeSink += ",\"kind\":";
                                            AppendJsonString(edgeSink, NameOf(edge.kind));
                                            edgeSink += '}';
                                        });
                        sink += ",\"barriers\":";
                        AppendJsonArray(sink, segment.barriers,
                                        [](std::string& barrierSink, const TraceBarrier& barrier)
                                        {
                                            barrierSink += R"({"resource":)";
                                            AppendJsonString(barrierSink, barrier.resource);
                                            barrierSink += ",\"kind\":";
                                            AppendJsonString(barrierSink, NameOf(barrier.kind));
                                            barrierSink += ",\"src_stage\":";
                                            AppendJsonString(barrierSink, barrier.srcStage);
                                            barrierSink += ",\"dst_stage\":";
                                            AppendJsonString(barrierSink, barrier.dstStage);
                                            barrierSink += ",\"src_access\":";
                                            AppendJsonString(barrierSink, barrier.srcAccess);
                                            barrierSink += ",\"dst_access\":";
                                            AppendJsonString(barrierSink, barrier.dstAccess);
                                            barrierSink += ",\"src_family\":" + std::to_string(barrier.srcFamily);
                                            barrierSink += ",\"dst_family\":" + std::to_string(barrier.dstFamily);
                                            barrierSink += '}';
                                        });
                        // Reserved: present so the shape does not change when timings arrive, invalid so a reader
                        // cannot mistake "unmeasured" for "instantaneous".
                        sink += R"(,"gpu":{"valid":)" + std::string(segment.gpu.valid ? "true" : "false");
                        sink += ",\"begin_ns\":" + std::to_string(segment.gpu.beginNs);
                        sink += ",\"end_ns\":" + std::to_string(segment.gpu.endNs);
                        sink += "}}";
                    });
    return out;
}

std::string RenderSignals(const RenderGraphTrace& trace)
{
    std::string out;
    AppendJsonArray(out, trace.signals,
                    [](std::string& sink, const TraceSignal& signal)
                    {
                        sink += R"({"segment":)" + std::to_string(signal.segment);
                        sink += ",\"queue\":";
                        AppendJsonString(sink, NameOf(signal.queue));
                        sink += ",\"value\":" + std::to_string(signal.value);
                        sink += '}';
                    });
    return out;
}

std::string RenderWaits(const RenderGraphTrace& trace)
{
    std::string out;
    AppendJsonArray(out, trace.waits,
                    [](std::string& sink, const TraceWait& wait)
                    {
                        sink += R"({"segment":)" + std::to_string(wait.segment);
                        sink += ",\"queue\":";
                        AppendJsonString(sink, NameOf(wait.queue));
                        sink += ",\"value\":" + std::to_string(wait.value);
                        sink += ",\"reason\":";
                        AppendJsonString(sink, NameOf(wait.reason));
                        sink += '}';
                    });
    return out;
}

/// The field table. Order is the emission order, and it is the contract a test guards.
constexpr std::array<TraceField, 9> K_TRACE_FIELDS{{
    {"schema", RenderSchema},
    {"execution", RenderExecution},
    {"capabilities", RenderCapabilities},
    {"scope", RenderScope},
    {"nodes", RenderNodes},
    {"cpu_nodes", RenderCpuNodes},
    {"segments", RenderSegments},
    {"signals", RenderSignals},
    {"waits", RenderWaits},
}};

/// Short lane name, e.g. `gfx` or `compute`.
const char* LaneName(QueueType type)
{
    switch (type)
    {
        case QueueType::GRAPHICS:
            return "gfx";
        case QueueType::COMPUTE:
            return "compute";
        case QueueType::COPY:
            return "copy";
        case QueueType::RAY_TRACING:
            return "rt";
        case QueueType::CPU:
            return "cpu";
        case QueueType::COUNT:
            break;
    }
    return "?";
}

/// Node names of a segment, comma separated.
std::string SegmentNodeNames(const RenderGraphTrace& trace, const TraceSegment& segment)
{
    std::string names;
    for (std::size_t i = segment.begin; i < segment.end && i < trace.nodes.size(); ++i)
    {
        if (!names.empty())
        {
            names += ", ";
        }
        names += trace.nodes[i].name;
    }
    return names;
}

/// Escape a label for DOT's quoted strings.
std::string EscapeDot(std::string_view text)
{
    std::string out;
    for (const char c : text)
    {
        if (c == '"' || c == '\\')
        {
            out += '\\';
        }
        out += c;
    }
    return out;
}

/// Escape a label for Mermaid's quoted strings: it has no backslash escapes inside labels, so quotes become entities.
std::string EscapeMermaid(std::string_view text)
{
    std::string out;
    for (const char c : text)
    {
        if (c == '"')
        {
            out += "#quot;";
        }
        else
        {
            out += c;
        }
    }
    return out;
}
}  // namespace

const char* NameOf(TraceEdgeKind kind)
{
    switch (kind)
    {
        case TraceEdgeKind::Transfer:
            return "transfer";
        case TraceEdgeKind::QueueOrder:
            return "queue-order";
    }
    return "?";
}

const char* NameOf(TraceBarrierKind kind)
{
    switch (kind)
    {
        case TraceBarrierKind::Image:
            return "image";
        case TraceBarrierKind::Buffer:
            return "buffer";
        case TraceBarrierKind::Memory:
            return "memory";
    }
    return "?";
}

const char* NameOf(TraceWaitReason reason)
{
    switch (reason)
    {
        case TraceWaitReason::Timeline:
            return "timeline";
        case TraceWaitReason::Caller:
            return "caller";
    }
    return "?";
}

const char* NameOf(QueueType type)
{
    switch (type)
    {
        case QueueType::GRAPHICS:
            return "graphics";
        case QueueType::COMPUTE:
            return "compute";
        case QueueType::COPY:
            return "copy";
        case QueueType::RAY_TRACING:
            return "ray-tracing";
        case QueueType::CPU:
            return "cpu";
        case QueueType::COUNT:
            break;
    }
    return "?";
}

std::string NameOfPipelineStages(VkPipelineStageFlags2 stages) { return RenderMask(stages, K_STAGE_NAMES); }

std::string NameOfAccesses(VkAccessFlags2 accesses) { return RenderMask(accesses, K_ACCESS_NAMES); }

RenderGraphTrace TracePlan(const RenderGraphExecutionPlan& plan, std::span<const TraceNodeView> nodes,
                           const TraceScope& scope)
{
    RenderGraphTrace trace;
    trace.scope = scope;

    trace.nodes.reserve(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
        TraceNode node;
        node.index = i;
        node.name = std::string(nodes[i].name);
        node.queueType = nodes[i].queueType;
        node.bAsync = nodes[i].bAsync;
        for (const TraceUseView& use : nodes[i].uses)
        {
            if (IsWrite(use.io))
            {
                node.writes.emplace_back(use.handle);
            }
            else
            {
                node.reads.emplace_back(use.handle);
            }
        }
        trace.nodes.push_back(std::move(node));
    }

    trace.cpuNodes = plan.cpuNodes;

    // Families come from the scope rather than the plan: the plan knows queue keys, and which Vulkan family a key
    // resolves to is a property of the device the trace ran on.
    const RenderGraphQueueFamilies families{.graphics = scope.graphicsFamily, .compute = scope.computeFamily};
    trace.segments.reserve(plan.segments.size());
    for (std::size_t s = 0; s < plan.segments.size(); ++s)
    {
        TraceSegment segment;
        segment.index = s;
        segment.queueType = plan.segments[s].queueType;
        segment.family = GetQueueFamilyForQueueType(segment.queueType, families);
        segment.begin = plan.segments[s].begin;
        segment.end = plan.segments[s].end;
        trace.segments.push_back(std::move(segment));
    }

    // A handover is an explicit ordering constraint, and the resource is its cause.
    for (const RenderGraphQueueTransfer& transfer : plan.transfers)
    {
        if (transfer.consumer >= trace.segments.size() || transfer.producer >= trace.segments.size())
        {
            continue;
        }
        TraceEdge edge;
        edge.from = transfer.producer;
        edge.to = transfer.consumer;
        edge.resource = transfer.handle;
        edge.kind = TraceEdgeKind::Transfer;
        trace.segments[transfer.consumer].dependsOn.push_back(std::move(edge));
    }

    // Two consecutive segments on one queue are ordered by program order. Recording it makes the edge visible
    // instead of implied by the plan's ordering, which is what a reader needs to see when a CPU node has split a
    // run of GPU work - the split is only visible in the gap.
    for (std::size_t s = 1; s < trace.segments.size(); ++s)
    {
        if (trace.segments[s].queueType != trace.segments[s - 1].queueType)
        {
            continue;
        }
        TraceEdge edge;
        edge.from = s - 1;
        edge.to = s;
        edge.kind = TraceEdgeKind::QueueOrder;
        trace.segments[s].dependsOn.push_back(std::move(edge));
    }

    return trace;
}

std::span<const TraceField> TraceFields() { return K_TRACE_FIELDS; }

std::string ToJson(const RenderGraphTrace& trace)
{
    std::string out = "{";
    bool bFirst = true;
    for (const TraceField& field : TraceFields())
    {
        if (!bFirst)
        {
            out += ',';
        }
        bFirst = false;
        AppendJsonString(out, field.name);
        out += ':';
        out += field.render(trace);
    }
    out += '}';
    return out;
}

std::string ToText(const RenderGraphTrace& trace)
{
    std::string out;
    out += "execution " + std::to_string(trace.execution) + "  device=\"" + trace.scope.deviceName + "\"";
    out += "  families gfx=" + std::to_string(trace.scope.graphicsFamily) +
           " compute=" + std::to_string(trace.scope.computeFamily);
    out += std::string("  dedicated-compute=") + (trace.scope.bDedicatedCompute ? "yes" : "no");
    out += "  in-flight=" + std::to_string(trace.scope.inFlightCount);
    out += std::string("  timings=") + (trace.scope.timestamps.bKnown ? "measured" : "unavailable");
    out += '\n';

    for (const std::size_t index : trace.cpuNodes)
    {
        const std::string name = index < trace.nodes.size() ? trace.nodes[index].name : std::string("<out of range>");
        out += std::string("  cpu     ") + name + "\n";
    }

    for (const TraceSegment& segment : trace.segments)
    {
        out += "  ";
        out += LaneName(segment.queueType);
        out += " s" + std::to_string(segment.index);
        out += "  nodes[" + std::to_string(segment.begin) + ".." + std::to_string(segment.end) + ")  ";
        out += SegmentNodeNames(trace, segment);

        for (const TraceEdge& edge : segment.dependsOn)
        {
            out += "  waits s" + std::to_string(edge.from) + " (";
            out += edge.resource.empty() ? std::string(NameOf(edge.kind)) : edge.resource + ", " + NameOf(edge.kind);
            out += ")";
        }
        out += '\n';

        if (segment.barriers.empty())
        {
            continue;
        }
        out += "    barriers\n";
        for (const TraceBarrier& barrier : segment.barriers)
        {
            out += "      " + (barrier.resource.empty() ? std::string("<device-wide>") : barrier.resource);
            out += "  " + std::string(NameOf(barrier.kind));
            out += "  " + barrier.srcStage + "/" + barrier.dstStage;
            out += "  " + barrier.srcAccess + "->" + barrier.dstAccess;
            if (barrier.srcFamily != VK_QUEUE_FAMILY_IGNORED || barrier.dstFamily != VK_QUEUE_FAMILY_IGNORED)
            {
                out += "  family " + std::to_string(barrier.srcFamily) + "->" + std::to_string(barrier.dstFamily);
            }
            out += '\n';
        }
    }

    for (const TraceSignal& signal : trace.signals)
    {
        out += std::string("  signal  ") + LaneName(signal.queue) + " v" + std::to_string(signal.value) + " at s" +
               std::to_string(signal.segment) + "\n";
    }
    for (const TraceWait& wait : trace.waits)
    {
        out += std::string("  wait    ") + LaneName(wait.queue) + " v" + std::to_string(wait.value) + " in s" +
               std::to_string(wait.segment) + " (" + NameOf(wait.reason) + ")\n";
    }
    return out;
}

TraceGraph ToGraph(const RenderGraphTrace& trace)
{
    TraceGraph graph;
    graph.nodes.reserve(trace.segments.size());
    for (const TraceSegment& segment : trace.segments)
    {
        TraceGraphNode node;
        node.segment = segment.index;
        node.label = std::string(LaneName(segment.queueType)) + ": " + SegmentNodeNames(trace, segment);
        graph.nodes.push_back(std::move(node));

        for (const TraceEdge& edge : segment.dependsOn)
        {
            TraceGraphEdge graphEdge;
            graphEdge.from = edge.from;
            graphEdge.to = edge.to;
            graphEdge.label =
                edge.resource.empty() ? std::string(NameOf(edge.kind)) : edge.resource + " (" + NameOf(edge.kind) + ")";
            graph.edges.push_back(std::move(graphEdge));
        }
    }
    return graph;
}

std::string ToDot(const TraceGraph& graph)
{
    std::string out =
        "digraph rendergraph {\n  rankdir=LR;\n  node [shape=box, style=rounded, fontname=\"monospace\"];\n";
    for (const TraceGraphNode& node : graph.nodes)
    {
        out += "  s" + std::to_string(node.segment) + " [label=\"" + EscapeDot(node.label) + "\"];\n";
    }
    for (const TraceGraphEdge& edge : graph.edges)
    {
        out += "  s" + std::to_string(edge.from) + " -> s" + std::to_string(edge.to);
        if (!edge.label.empty())
        {
            out += " [label=\"" + EscapeDot(edge.label) + "\"]";
        }
        out += ";\n";
    }
    out += "}\n";
    return out;
}

std::string ToMermaid(const TraceGraph& graph)
{
    std::string out = "flowchart LR\n";
    for (const TraceGraphNode& node : graph.nodes)
    {
        out += "  s" + std::to_string(node.segment) + "[\"" + EscapeMermaid(node.label) + "\"]\n";
    }
    for (const TraceGraphEdge& edge : graph.edges)
    {
        out += "  s" + std::to_string(edge.from) + " -->";
        if (!edge.label.empty())
        {
            out += "|\"" + EscapeMermaid(edge.label) + "\"|";
        }
        out += " s" + std::to_string(edge.to) + "\n";
    }
    return out;
}
}  // namespace Muyo::RenderGraph
