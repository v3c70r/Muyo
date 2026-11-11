// The error-reporting contract (#53): declaration and configuration errors throw a standard exception a
// caller can catch; a failed Vulkan call aborts with its result and call site. The two halves have different
// mechanisms on purpose, and `docs/RenderGraph.md` "Failure index" / `docs/RenderGraph-Library-Design.md` §3
// state which is which.
//
// The abort case runs the same test case in a child process, because the process that aborts cannot assert on
// it. The parent requires death by SIGABRT *and* the `[FATAL]: Vulkan call failed` line, so a VK_ASSERT that
// threw instead of aborting fails on the signal (Catch2 catches the exception and the child exits non-zero),
// and an abort from somewhere else fails on the missing diagnostic.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>

#include "Debug.h"
#include "RenderGraph/RenderGraphBuilder.h"

#if defined(__unix__)
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
#if defined(_WIN32)
std::optional<std::string> EnvironmentValue(const char* name)
{
    char* value = nullptr;
    if (_dupenv_s(&value, nullptr, name) != 0 || value == nullptr)
    {
        return std::nullopt;
    }

    std::string result(value);
    free(value);
    return result;
}
#else
std::optional<std::string> EnvironmentValue(const char* name)
{
    if (const char* value = std::getenv(name))
    {
        return std::string(value);
    }
    return std::nullopt;
}
#endif

#if defined(__unix__)
/// Env vars that turn this test case into the child half of itself.
constexpr const char* sChildEnvVar = "MUYO_VK_ASSERT_ABORT_CHILD";
constexpr const char* sChildLogEnvVar = "MUYO_VK_ASSERT_ABORT_LOG";

/// The exact Catch2 spec for this case; Catch2 specs are exact matches without wildcards.
constexpr const char* sAbortCaseName = "Error reporting: a failed Vulkan call aborts with its result and call site";
#endif
}  // namespace

TEST_CASE("Error reporting: FatalError throws std::runtime_error with its message and location", "[ErrorReporting]")
{
    // Configuration failures (a missing layer, no enumerated device, an unsupported Vulkan version) are
    // reachable from a caller's environment, so they throw; a consumer that catches the error can report
    // which request was wrong. The location is part of the message because a log reader has no debugger.
    try
    {
        Muyo::FatalError("a configuration error the caller can fix");
    }
    catch (const std::runtime_error& error)
    {
        const std::string message = error.what();
        CHECK_THAT(message, Catch::Matchers::ContainsSubstring("a configuration error the caller can fix"));
        CHECK_THAT(message, Catch::Matchers::ContainsSubstring("ErrorReportingTests.cpp"));
        return;
    }
    FAIL("FatalError returned instead of throwing");
}

TEST_CASE("Error reporting: a declaration error throws std::runtime_error, never aborts",
          "[ErrorReporting][RenderGraph]")
{
    // Device-free: the missing-service check is the graph's own precondition and runs before any Vulkan call.
    // This pins the exception *type* as well as the throw, which the message-only checks elsewhere do not.
    CHECK_THROWS_AS(Muyo::RenderGraph::RenderGraphBuilder(Muyo::RenderGraph::RenderGraphServices{}),
                    std::runtime_error);

    // A successful result is a no-op, not a special case; the same function aborts only on failure.
    CHECK_NOTHROW(Muyo::VK_ASSERT(VK_SUCCESS));
}

TEST_CASE("Error reporting: a failed Vulkan call aborts with its result and call site", "[ErrorReporting]")
{
#if defined(__unix__)
    if (EnvironmentValue(sChildEnvVar).has_value())
    {
        // Child half: never returns on success of the test. Reset Catch2's SIGABRT handler so the abort is
        // observable as a signal, then send the diagnostic somewhere the parent can read it.
        std::signal(SIGABRT, SIG_DFL);
        const auto logPath = EnvironmentValue(sChildLogEnvVar);
        if (!logPath.has_value() || std::freopen(logPath->c_str(), "w", stderr) == nullptr)
        {
            std::_Exit(120);
        }
        Muyo::VK_ASSERT(VK_ERROR_DEVICE_LOST);
        std::_Exit(0);  // Only reached if VK_ASSERT stopped aborting on failure.
    }

    const std::filesystem::path logPath =
        std::filesystem::temp_directory_path() /
        ("muyo-vk-assert-" + std::to_string(static_cast<long long>(::getpid())) + ".log");
    REQUIRE(setenv(sChildEnvVar, "1", 1) == 0);
    REQUIRE(setenv(sChildLogEnvVar, logPath.c_str(), 1) == 0);

    const pid_t child = fork();
    if (child == 0)
    {
        execl("/proc/self/exe", "/proc/self/exe", sAbortCaseName, static_cast<char*>(nullptr));
        std::_Exit(127);  // only reached if exec failed
    }
    REQUIRE(child > 0);

    int childStatus = 0;
    REQUIRE(waitpid(child, &childStatus, 0) == child);
    REQUIRE(unsetenv(sChildEnvVar) == 0);
    REQUIRE(unsetenv(sChildLogEnvVar) == 0);

    INFO("child status: " << childStatus);
    REQUIRE(WIFSIGNALED(childStatus));
    CHECK(WTERMSIG(childStatus) == SIGABRT);

    std::ifstream log(logPath);
    REQUIRE(log.good());
    const std::string diagnostic((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
    CHECK_THAT(diagnostic, Catch::Matchers::ContainsSubstring("[FATAL]: Vulkan call failed with VK_ERROR_DEVICE_LOST"));
    CHECK_THAT(diagnostic, Catch::Matchers::ContainsSubstring("ErrorReportingTests.cpp"));
    std::error_code ignored;
    std::filesystem::remove(logPath, ignored);
#else
    WARN(
        "no process spawn on this platform: the VK_ASSERT abort cannot be observed from a test process, and "
        "an abort in-process would end the run");
#endif
}
