// Out-of-tree consumer of the installed `muyo_rg` package (#298).
//
// The CI `package` job builds and runs this against an installed prefix with only `CMAKE_PREFIX_PATH`
// set. It must compile from the public headers alone and run without a GPU, so it constructs public
// graph values and never creates a device. The exit status is the job's verdict; the printed line is
// what the job asserts and what a reader sees in the log.
#include <RenderGraph/MuyoVersion.h>
#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderGraphResourceDesc.h>

#include <cstdio>
#include <variant>

namespace rg = Muyo::RenderGraph;

int main()
{
    rg::RenderGraphNodeCreateInfo node{};
    node.nodeName = "package-probe";
    node.queueType = rg::QueueType::COMPUTE;

    rg::ResourceDesc desc = rg::BufferResourceDesc{};
    auto& buffer = std::get<rg::BufferResourceDesc>(desc);
    buffer.count = 256;
    buffer.stride = 4;
    buffer.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    buffer.memoryProperties = VMA_MEMORY_USAGE_AUTO;

    std::printf("muyo_rg %s (VERSION=%u): node=%s, buffer=%llu bytes\n", rg::VERSION_STRING, rg::VERSION,
                node.nodeName.c_str(), static_cast<unsigned long long>(buffer.count * buffer.stride));

    // `find_package(muyo_rg 0.1 REQUIRED)` already pins the minor; this fails loudly if the header a
    // consumer just found reports a version the package does not claim, which a stale generated
    // `<muyo_rg/version.h>` would do.
    if (rg::VERSION_MAJOR != 0 || rg::VERSION_MINOR != 1)
    {
        std::fprintf(stderr, "package-probe: expected muyo_rg 0.1.x, found %s\n", rg::VERSION_STRING);
        return 1;
    }
    if (node.queueType != rg::QueueType::COMPUTE || buffer.usage != VK_BUFFER_USAGE_STORAGE_BUFFER_BIT ||
        buffer.count * buffer.stride != 1024)
    {
        std::fprintf(stderr, "package-probe: a public graph value did not round-trip\n");
        return 1;
    }
    return 0;
}
