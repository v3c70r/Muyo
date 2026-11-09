#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Muyo::RenderGraph
{
/// Where the graph gets a shader's SPIR-V from (#292).
///
/// Every shader a node declares is loaded during `AddNode()`. With no provider in
/// `RenderGraphServices::shaderSource` the graph reads `shaders/<shaderName>.spv` from the process's
/// working directory, which is what every in-tree consumer does. A host with no `.spv` files - an
/// embedded runtime, a compiler that produces the words in memory, a test - implements this interface
/// and passes it in `RenderGraphServices::shaderSource`; the graph then asks it for the words instead
/// of opening a file.
///
/// The provider is **host-owned**. The graph neither copies nor destroys it: it must outlive every
/// builder that refers to it, and every `ShaderCache` call that is given it. `LoadShader()` returns
/// the words by value, so their lifetime is the caller's from the moment the call returns.
///
/// **Threading.** The builder is single-threaded, so a provider is called from one thread at a time
/// for a given builder. A provider shared by builders on different threads has to synchronise itself.
/// `LoadShader()` is `const` because it is a lookup; a provider that memoizes its answer keeps that
/// cache in mutable state or builds it before the first call.
///
/// **Failure modes.** An implementation returns `std::nullopt` for a name it does not have, and may
/// also return an empty vector for that case. The graph turns either into its named
/// `Node 'N' declares shader 'S', which could not be loaded` error. An exception thrown from
/// `LoadShader()` propagates out of `AddNode()` (the default file provider throws
/// `std::runtime_error("Failed to open file")` for a missing file, as the graph did before the
/// provider existed). Non-empty words that are not valid SPIR-V are not rejected by this interface:
/// reflection or module creation fails, either by throwing or by making the load return `std::nullopt`.
class IShaderSourceProvider
{
public:
    /// Destroy the provider. The host owns it and must keep it alive while a builder refers to it.
    virtual ~IShaderSourceProvider() = default;

    /// Return the SPIR-V words for a shader name.
    ///
    /// @param shaderName The name a node declared, without the `.spv` suffix, exactly as it appears in
    ///        `RenderGraphNodeCreateInfo::shaderNames` or `rtShaderNames`.
    /// @return The shader's SPIR-V words (one `uint32_t` per word), or `std::nullopt` when no shader
    ///         of that name exists. An empty vector is accepted as the same answer.
    /// @throws std::runtime_error (or any other exception) to report a failure that is not a missing
    ///         entry; it propagates out of the `AddNode()` or `ShaderCache::LoadShader()` call.
    virtual std::optional<std::vector<uint32_t>> LoadShader(const std::string& shaderName) const = 0;
};

/// The default shader source: reads `<searchPath>/<shaderName>.spv` from the filesystem.
///
/// This reproduces the graph's historical behaviour when `searchPath` is left at its default of
/// `"shaders"`. Supplying one with a different path is how a host points the graph at its own shader
/// directory without implementing `IShaderSourceProvider`:
///
/// @code
/// const FileShaderSourceProvider shaderSource("build/shaders");
/// RenderGraphBuilder builder(RenderGraphServices{
///     .device = device, .resourceAllocator = &allocator, .shaderSource = &shaderSource});
/// @endcode
///
/// **Missing and empty files.** A file that cannot be opened throws
/// `std::runtime_error("Failed to open file")`; a file of zero bytes yields `std::nullopt`. Both are
/// the behaviour `ReadSpirvFile` and the graph had before this provider existed.
class FileShaderSourceProvider final : public IShaderSourceProvider
{
public:
    /// @param searchPath Directory the `.spv` files are read from. A relative path is resolved
    ///        against the process's working directory, not the source tree, at every load.
    explicit FileShaderSourceProvider(std::filesystem::path searchPath = "shaders");

    /// Destroy the provider; it owns only the path.
    ~FileShaderSourceProvider() override = default;

    /// Read `<searchPath>/<shaderName>.spv`.
    /// @param shaderName Shader name without the `.spv` suffix.
    /// @return Its words, or `std::nullopt` when the file is empty.
    /// @throws std::runtime_error when the file cannot be opened or read, or is not a whole number of
    ///         32-bit words.
    std::optional<std::vector<uint32_t>> LoadShader(const std::string& shaderName) const override;

    /// @return The directory this provider reads from.
    const std::filesystem::path& GetSearchPath() const { return m_searchPath; }

private:
    std::filesystem::path m_searchPath;  ///< Directory the `.spv` files are read from.
};
}  // namespace Muyo::RenderGraph
