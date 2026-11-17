#include <catch2/catch_test_macros.hpp>

#include <csignal>
#include <cstdlib>

#include "GraphicsTestEnv.h"

#if defined(__unix__)
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
/// Env var that turns this test case into the *child* half of itself.
///
/// The child is spawned with fork+exec rather than fork alone. Forking a process that already holds a
/// Vulkan instance, a device and whatever threads the loader and the validation layer own is only safe
/// if none of them holds a lock at that instant - and the child would immediately call back into that
/// same code. Between fork and exec only async-signal-safe calls are permitted, and `execl` qualifies,
/// so the child reaches a fresh process image without touching any of it.
constexpr const char* sChildEnvVar = "MUYO_VALIDATION_ABORT_CHILD";

/// The exact Catch2 spec for this case, used by the parent to run the child. Catch2 specs are exact
/// matches without wildcards, so this has to be the full name.
constexpr const char* sCaseName =
    "Debug callback: a validation error aborts in every build configuration";

/// Provokes a validation error the layer is guaranteed to report: an invalid handle passed to a call
/// that validates it. Needs no setup beyond the device the test environment already created, and has
/// no success path on any driver, so the callback is always reached.
void ProvokeValidationError()
{
    VkFence bogusFence = reinterpret_cast<VkFence>(static_cast<uintptr_t>(0x1234));
    vkWaitForFences(Muyo::GetRenderDevice()->GetDevice(), 1, &bogusFence, VK_TRUE, 0);
}
}  // namespace

TEST_CASE_METHOD(Muyo::GraphicsTestEnv, "Debug callback: a validation error aborts in every build configuration",
                 "[RenderDevice]")
{
    // Closes the gap that #45's own body names. The conversion of the callback's assert(0) had no test
    // because DebugCallback is file-static and the expected outcome is the process dying, which the
    // process that dies cannot assert on. The reviewer on #45 proved the mechanism with a scratch test
    // and showed the old behaviour was worse than the PR claimed: under NDEBUG,
    // assert(CreateDebugUtilsMessenger(...) == VK_SUCCESS) removes *the call itself*, so no messenger
    // existed at all, validation errors went to raw stdout consumed by nothing, and the driver received
    // a garbage handle. This is the committed version of that observation.
    //
    // What it pins in both configurations: a validation ERROR terminates the process by SIGABRT. It
    // fails if the callback returns instead - which is exactly what the old `assert(0)` did under
    // NDEBUG - and it fails if the layer is not loaded, since then nothing reports the error and the
    // child exits 0.
#if defined(__unix__)
    if (std::getenv(sChildEnvVar) != nullptr)
    {
        // Child half: never returns on success of the test.
        //
        // Catch2 installs a handler for SIGABRT to print its own crash report, which would convert the
        // abort into an exit status and hide the signal the parent asserts on. Reset it to the default
        // action so death-by-signal is observable.
        std::signal(SIGABRT, SIG_DFL);
        ProvokeValidationError();
        // Only reached if nothing reported the error: no layer present, or a callback that returned.
        std::_Exit(0);
    }

    // Spawned with fork+exec, not std::system: `system` returns the status of the *shell* it invokes,
    // so a grandchild dying by signal arrives as exit status 128+signal and WIFSIGNALED is false - the
    // first version of this test failed for that reason while the child was aborting correctly.
    REQUIRE(setenv(sChildEnvVar, "1", 1) == 0);
    const pid_t child = fork();
    if (child == 0)
    {
        execl("/proc/self/exe", "/proc/self/exe", sCaseName, static_cast<char*>(nullptr));
        std::_Exit(127);  // only reached if exec failed
    }
    REQUIRE(child > 0);

    int childStatus = 0;
    REQUIRE(waitpid(child, &childStatus, 0) == child);
    REQUIRE(unsetenv(sChildEnvVar) == 0);

    INFO("child status: " << childStatus);
    REQUIRE(WIFSIGNALED(childStatus));
    CHECK(WTERMSIG(childStatus) == SIGABRT);
#else
    WARN("no process spawn on this platform: the validation callback's abort cannot be observed from a test "
         "process, and an abort in-process would end the run");
#endif
}
