// Out-of-tree consumer of the installed `muyo_rg` package (#298).
//
// The CI `package` job builds and runs this against an installed prefix with only `CMAKE_PREFIX_PATH`
// set. It must compile from the public headers alone and run without a GPU, so it constructs public
// graph values and never creates a device. The exit status is the job's verdict; the printed line is
// what the job asserts and what a reader sees in the log.
//
// Since #292 it also implements `IShaderSourceProvider` and puts it in `RenderGraphServices`. That is
// the guarantee "a client can supply SPIR-V bytes / a shader search path" exercised through the
// *installed* public surface: the provider is host code, the words are in memory, and no `.spv` file or
// device is involved.
#include <RenderGraph/MuyoVersion.h>
#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderGraphResourceDesc.h>
#include <RenderGraph/ShaderSourceProvider.h>

#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace rg = Muyo::RenderGraph;

namespace
{
/// A shader source with no files behind it: the probe's shader words live in this translation unit.
class MemoryShaderSource final : public rg::IShaderSourceProvider
{
public:
    std::optional<std::vector<uint32_t>> LoadShader(const std::string& shaderName) const override
    {
        if (shaderName != "probe.comp") return std::nullopt;
        // The SPIR-V magic plus a version word. The probe has no device, so it never reflects this or
        // creates a module; the point is that the bytes travel through the public interface.
        return std::vector<uint32_t>{0x07230203u, 0x00010000u};
    }
};
}  // namespace

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

    // #292: a host-supplied shader source through the installed public surface. Constructing the service
    // with the provider, asking it for words, and configuring a search path is the whole consumer API;
    // no device is created, so this runs on the package job's GPU-less runner.
    MemoryShaderSource shaderSource;
    const rg::RenderGraphServices services{.shaderSource = &shaderSource};
    const std::optional<std::vector<uint32_t>> words = services.shaderSource->LoadShader("probe.comp");
    if (services.shaderSource != &shaderSource || !words.has_value() || words->size() != 2 ||
        words->front() != 0x07230203u || services.shaderSource->LoadShader("absent.comp").has_value())
    {
        std::fprintf(stderr, "package-probe: a supplied shader source did not round-trip\n");
        return 1;
    }
    const rg::FileShaderSourceProvider fileSource("probe-shaders");
    if (fileSource.GetSearchPath() != std::filesystem::path("probe-shaders"))
    {
        std::fprintf(stderr, "package-probe: the shader search path did not round-trip\n");
        return 1;
    }
    std::printf("muyo_rg shader source: %zu words from memory, search path %s\n", words->size(),
                fileSource.GetSearchPath().string().c_str());
    return 0;
}
