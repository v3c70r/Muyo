#!/usr/bin/env bash
#
# Local sanity checks for Muyo.
#
# There is no CI (see AGENTS.md, section 4): these run on the development machine,
# normally before opening a pull request. Nothing here is required to pass yet --
# the point is to make the state visible so it can be improved in passes (issues
# #23, #24, #25) instead of being discovered late.
#
# Usage:
#   scripts/sanity.sh              fast: docs coverage + formatting of changed lines
#   scripts/sanity.sh docs         documentation coverage + generated markdown
#   scripts/sanity.sh format       clang-format on the lines this branch changed
#   scripts/sanity.sh tidy         clang-tidy on the lines this branch changed (fails on a finding)
#   scripts/sanity.sh static       Clang static analyzer over a dedicated build (slow)
#   scripts/sanity.sh sanitize     build + run the tests under ASan/UBSan
#   scripts/sanity.sh thread       build + run the device-free cases under ThreadSanitizer (report only)
#   scripts/sanity.sh warnings     -Wall/-Wextra baseline, changed files only (fails on a regression)
#   scripts/sanity.sh analyzer     gcc -fanalyzer fallout (slow, report only)
#   scripts/sanity.sh all          the slow checks: docs, format, static, sanitize, warnings, analyzer
#
# Environment:
#   BASE=<ref>   comparison ref for the format and warnings checks (default: origin/master)
#
# The last line is always 'sanity: OK' or 'sanity: FAILED' and the exit status matches it - including
# when the script cannot resolve the repository root and has nothing else to report.

set -uo pipefail

ROOT="$(git rev-parse --show-toplevel)"
if [ -z "$ROOT" ] || ! cd "$ROOT"; then
    # The degenerate case (not in a git repository, or the root cannot be entered) used to exit before
    # any verdict existed, so the "last line is always the verdict" contract had an exception. Printing
    # it costs one line and removes the exception, which matters because the verdict is what a reader or
    # an agent greps - and a run with no verdict looks like a run that did not happen.
    printf '\033[31msanity: FAILED (cannot enter the repository root)\033[0m\n'
    exit 1
fi
BASE="${BASE:-origin/master}"
JOBS="$(nproc 2>/dev/null || echo 4)"

SRC_GLOBS=('src/*.cpp' 'src/*.h' 'src/**/*.cpp' 'src/**/*.h')

bold() { printf '\n\033[1m== %s ==\033[0m\n' "$*"; }
ok() { printf '\033[32m%s\033[0m\n' "$*"; }
warn() { printf '\033[33m%s\033[0m\n' "$*"; }
bad() { printf '\033[31m%s\033[0m\n' "$*"; }

fail=0

cmd_docs() {
    bold "docs: coverage + generated markdown"
    if python3 scripts/render_graph_docs.py --check; then
        ok "docs OK"
    else
        bad "docs FAILED"
        fail=1
    fi
}

cmd_format() {
    bold "format: lines changed vs $BASE"
    local diff
    # Compare the working tree against the base, not BASE...HEAD: the latter only sees committed
    # lines, so a check run before committing inspects none of the work being prepared.
    diff="$(git diff -U0 --no-color "$BASE" -- "${SRC_GLOBS[@]}")"
    if [ -z "$diff" ]; then
        ok "no changed C++ files"
        return
    fi

    local tool
    tool="$(command -v clang-format-diff || command -v clang-format-diff-18 || true)"
    if [ -z "$tool" ]; then
        bad "clang-format-diff not found; skipping"
        return
    fi

    # Only the lines this branch touched are checked, so an unformatted file that
    # predates the branch never blocks work. A whole-tree pass is issue #23.
    local out
    out="$(printf '%s' "$diff" | "$tool" -p1)"
    if [ -n "$out" ]; then
        bad "changed lines are not clang-format clean:"
        printf '%s\n' "$out" | head -60
        printf '\nTo apply:\n  git diff -U0 --no-color %s -- src | %s -p1 | git apply -p0\n' "$BASE" "$tool"
        fail=1
    else
        ok "format OK"
    fi
}

cmd_tidy() {
    bold "tidy: changed lines (enforced; curated .clang-tidy, #24)"
    local tool differ
    # Pin 18: the curated config is measured with it and it matches .clang-format; a newer clang-tidy adds
    # checks and changes results, so a version bump is a deliberate, re-measured change.
    tool="$(command -v clang-tidy-18 || command -v clang-tidy || true)"
    differ="$(command -v clang-tidy-diff-18.py || command -v clang-tidy-diff || true)"
    if [ -z "$tool" ] || [ -z "$differ" ]; then
        # A gate that skips itself because a tool is missing passes for the wrong reason, so this fails.
        bad "clang-tidy 18 / clang-tidy-diff not found; install clang-tidy-18 (issue #24)"
        fail=1
        return
    fi
    if [ ! -f build/compile_commands.json ]; then
        bad "build/compile_commands.json not found - clang-tidy needs a compilation database"
        echo "  configure first:  cmake -S . -B build"
        fail=1
        return
    fi

    # `git diff` does not see untracked files, so a new translation unit is invisible here until it is
    # staged (`git add -N` suffices) and the build is reconfigured to give it a compile command. The
    # format gate has the same blind spot; saying so is cheaper than discovering it at review time. This
    # runs before the empty-diff return, so a change that consists only of a new file is not a silent pass.
    local untracked
    untracked="$(git ls-files --others --exclude-standard -- 'src/*.cpp' 'src/**/*.cpp' || true)"
    if [ -n "$untracked" ]; then
        warn "untracked translation units are not checked (stage them with 'git add -N'):"
        printf '  %s\n' "$untracked"
    fi

    local diff
    diff="$(git diff -U0 --no-color "$BASE" -- "${SRC_GLOBS[@]}")"
    if [ -z "$diff" ]; then
        if [ -n "$untracked" ]; then
            bad "no tracked C++ changes; the untracked file(s) above were not tidied"
            fail=1
            return
        fi
        ok "no changed C++ files"
        return
    fi

    # Enforced gate: clang-tidy runs over each changed file (clang-tidy interpolates a compile command for a
    # header from a TU that includes it) and `-line-filter` keeps only diagnostics on lines this change
    # touched. That is the tidy analogue of the format gate: pre-existing findings in a file do not block
    # work, and a new one on a changed line does. `clang-tidy-diff` returns non-zero only when a clang-tidy
    # invocation fails, so the diagnostic count is what decides the verdict here.
    local log=/tmp/muyo-tidy.log
    local rc=0 count
    # `-clang-tidy-binary` pins the same version the comment above pins: clang-tidy-diff would otherwise
    # call whatever `clang-tidy` is first on PATH.
    printf '%s' "$diff" | "$differ" -clang-tidy-binary "$tool" -p1 -path build -j "$JOBS" >"$log" 2>&1 || rc=$?
    count="$(grep -cE ':[0-9]+:[0-9]+: (warning|error):' "$log" || true)"

    if [ "$count" -eq 0 ] && [ "$rc" -eq 0 ]; then
        ok "tidy OK (no diagnostics on changed lines)"
        return
    fi

    if [ "$count" -gt 0 ]; then
        echo "diagnostics by check:"
        # Dots and capitals are load-bearing: every `clang-analyzer-*` name has them.
        grep -oE '\[[A-Za-z0-9._-]+(,[A-Za-z0-9._-]+)*\]$' "$log" \
            | sed 's/[][]//g' | tr ',' '\n' | sort | uniq -c | sort -rn | head -20
        bad "tidy FAILED: $count diagnostic(s) on changed lines:"
        grep -E ':[0-9]+:[0-9]+: (warning|error):' "$log" | head -40
        printf '\nfull log: %s\n' "$log"
    fi
    if [ "$rc" -ne 0 ]; then
        bad "clang-tidy-diff failed (exit $rc); tail of $log:"
        tail -20 "$log"
    fi
    fail=1
}

cmd_static() {
    bold "static: Clang static analyzer (scan-build)"
    local sb
    sb="$(command -v scan-build-20 || command -v scan-build || true)"
    if [ -z "$sb" ]; then
        bad "scan-build not found; skipping"
        return
    fi
    echo "using $sb -- this rebuilds the whole project, expect several minutes"
    cmake -S . -B build-static -DGLFW_BUILD_WAYLAND=OFF >/dev/null || {
        bad "configure failed"
        fail=1
        return
    }
    if "$sb" --status-bugs -o build-static-report cmake --build build-static -j"$JOBS"; then
        ok "static analysis: no defects reported"
    else
        bad "static analysis reported defects (see build-static-report/)"
        fail=1
    fi
    echo "report: scan-view-20 build-static-report/* or open build-static-report/index.html"
}

# Sanitizer runtime options, in one place because three things read them: this script, the CI workflow,
# and whoever reproduces a failure by hand. Each is here for a reason, not for thoroughness:
#
#  -detect_stack_use_after_return=1  finds use-after-return, which the default (heap-only) mode misses.
#  -strict_string_checks=1            catches reads past the end of a string argument.
#  -check_initialization_order=1      finds access to a global before its initialiser ran - the class that
#                                     bit this repository's static registration order more than once.
#  -print_stacktrace=1 (UBSan)        a runtime error without a stack is a location, not a bug report.
#  -fast_unwind_on_malloc=0 (LSan)    full allocations stacks instead of one frame; this is what makes a
#                                     leak attributable to a module instead of an address (#171).
sanitizer_env() {
    ASAN_OPTIONS="detect_stack_use_after_return=1:strict_string_checks=1:check_initialization_order=1"
    UBSAN_OPTIONS="print_stacktrace=1"
    LSAN_OPTIONS="suppressions=$ROOT/san.supp:print_suppressions=1:fast_unwind_on_malloc=0"
    export ASAN_OPTIONS UBSAN_OPTIONS LSAN_OPTIONS
}

cmd_sanitize() {
    bold "sanitize: ASan + UBSan build and tests (UB aborts; the options are printed below)"
    cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Sanitize -DGLFW_BUILD_WAYLAND=OFF >/dev/null || {
        bad "configure failed"
        fail=1
        return
    }
    # Shaders and assets are both read relative to the working directory. Omitting them made a fresh
    # `sanity.sh sanitize` fail with a shader-load error that looks like a code regression (AGENTS.md
    # section 4).
    cmake --build build-san --target tests Shaders -j"$JOBS" || {
        bad "build failed"
        fail=1
        return
    }
    ln -sfn "$ROOT/assets" build-san/assets

    sanitizer_env
    echo "ASAN_OPTIONS=$ASAN_OPTIONS"
    echo "UBSAN_OPTIONS=$UBSAN_OPTIONS"
    echo "LSAN_OPTIONS=$LSAN_OPTIONS"

    local log=/tmp/muyo-sanitize.err
    (cd build-san && ./tests >/tmp/muyo-sanitize.out 2>"$log")
    local status=$?
    tail -3 /tmp/muyo-sanitize.out

    # Two independent verdicts, because the exit status alone is not enough: a build whose UBSan was
    # configured to recover prints `runtime error:` and still exits 0, which is how UB stayed invisible
    # here until `-fno-sanitize-recover=undefined` was added. The grep catches that configuration as well
    # as this one.
    local diagnostics
    diagnostics="$(grep -cE 'ERROR: (Address|Leak)Sanitizer|runtime error:' "$log" || true)"
    if [ "$status" -ne 0 ]; then
        bad "tests failed under sanitizers (exit $status)"
        echo "first 20 diagnostic lines:"
        grep -E 'ERROR: (Address|Leak)Sanitizer|runtime error:' "$log" | head -20 || true
        fail=1
    elif [ "$diagnostics" != "0" ]; then
        bad "sanitizer reported $diagnostics line(s) but the run exited 0 - the build is recovering, see CMakeLists.txt"
        head -20 "$log"
        fail=1
    else
        ok "sanitize OK (exit 0, no sanitizer diagnostics)"
    fi
}

cmd_thread() {
    bold "thread: ThreadSanitizer over the device-free cases (report only, enforcement is #183)"
    # TSan and ASan cannot share a binary, so this is its own configuration rather than another
    # sanitizer flag. The emulator that makes test discovery work without root is set in CMakeLists.txt
    # (`CROSSCOMPILING_EMULATOR`), because `catch_discover_tests` runs the binary at build time.
    cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Thread -DGLFW_BUILD_WAYLAND=OFF >/dev/null || {
        bad "configure failed"
        fail=1
        return
    }
    cmake --build build-tsan --target tests Shaders -j"$JOBS" || {
        bad "build failed"
        fail=1
        return
    }
    ln -sfn "$ROOT/assets" build-tsan/assets
    # A report count of zero is only evidence if the binary was instrumented. Linking libtsan is the
    # runtime's own precondition, so this rules out the silent case where the configuration name and the
    # flags disagree and the run is green because nothing was instrumented.
    if ! ldd build-tsan/tests 2>/dev/null | grep -q libtsan; then
        bad "build-tsan/tests is not linked against libtsan - this run would prove nothing"
        fail=1
        return
    fi

    # Scope, stated in tags rather than in prose: these are the cases that execute no Vulkan device.
    # Device cases are excluded because a driver is uninstrumented - TSan can see the loader and our
    # code, not the driver's threads - so a clean run over them would be a claim about an unnamed pair
    # (issue #173 step 4). The case count is printed below so "clean" carries its scope.
    local tags='[CaptureTool],[TransientMemoryPlanner],[RenderGraphTrace],[PSODesc],[LaneLayout]'
    # halt_on_error=0 collects every report instead of stopping at the first, which is what a baseline
    # needs. The verdict below is the report count, not the exit status: TSan's default exit status is
    # still 66 once it has reported anything.
    TSAN_OPTIONS="halt_on_error=0:second_deadlock_stack=1"
    export TSAN_OPTIONS
    echo "TSAN_OPTIONS=$TSAN_OPTIONS"
    echo "scope tags: $tags"

    local out=/tmp/muyo-thread.out err=/tmp/muyo-thread.err
    (cd build-tsan && setarch "$(uname -m)" -R ./tests "$tags" >"$out" 2>"$err")
    local status=$?
    grep -E "All tests passed|test cases:" "$out" | tail -1

    local reports
    reports="$(grep -cE 'WARNING: ThreadSanitizer:' "$err" || true)"
    if [ "$reports" != "0" ]; then
        warn "thread: $reports report(s), run exited $status - REPORT ONLY until #183 enforces the baseline"
        grep -A6 -E 'WARNING: ThreadSanitizer:' "$err" | head -40
    elif [ "$status" -ne 0 ]; then
        bad "thread: the device-free cases failed under TSan (exit $status, no TSan report) "
        tail -20 "$err" | head -20
        fail=1
    else
        ok "thread: 0 reports, exit 0 (scope is the cases named above, not the whole suite)"
    fi
    printf 'full log: %s\n' "$err"
}

cmd_warnings() {
    bold "warnings: -Wall/-Wextra per-file baseline, changed files only"
    # The project's own flags now include -Wextra, so this build uses the configuration under test
    # rather than a hand-written flag string that can drift from CMakeLists.txt (#25).
    # -DCMAKE_CXX_FLAGS= clears anything an earlier `warnings` run cached (it used to set an
    # -fanalyzer string here); this check must measure the flags CMakeLists.txt defines and nothing else.
    cmake -S . -B build-warn -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS= -DGLFW_BUILD_WAYLAND=OFF >/dev/null || {
        bad "configure failed"
        fail=1
        return
    }
    # --clean-first, because an incremental build re-emits warnings only for the translation units it
    # recompiles: a per-file count taken from one would read zero for every file that was already up to
    # date, and the comparison would be vacuous. The counts in the baseline are whole-build counts.
    local log=/tmp/muyo-warnings.log
    if ! cmake --build build-warn --target tests helloVulkan helloVulkanRG --clean-first -j"$JOBS" >"$log" 2>&1; then
        bad "build failed (see $log)"
        tail -20 "$log"
        fail=1
        return
    fi
    # Changed files may keep their recorded count and not one warning more. Files this branch did not
    # touch cannot block it, which is what makes a baseline usable while #169/#186 are still open.
    if python3 scripts/warning_baseline.py --log "$log" --baseline scripts/warning-baseline.txt --base "$BASE"; then
        ok "warnings OK (no new warning in a changed file)"
    else
        bad "warnings FAILED (see above; log: $log)"
        fail=1
    fi
}

cmd_analyzer() {
    bold "analyzer: gcc -fanalyzer fallout (report only)"
    # -fanalyzer is a path-sensitive analysis, not a warning level, and its findings still need triage
    # before they can be enforced. Kept separate from `warnings` so enabling one does not silently
    # enable the other (#25).
    local log=/tmp/muyo-analyzer.log
    cmake -S . -B build-analyzer -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fanalyzer" -DGLFW_BUILD_WAYLAND=OFF >/dev/null || {
        bad "configure failed"
        fail=1
        return
    }
    cmake --build build-analyzer -j"$JOBS" 2>&1 | tee "$log" | grep -E "warning:" | sed 's/^.*warning: /warning: /' | sort | uniq -c | sort -rn | head -30
    printf '\nfull log: %s\n' "$log"
    printf 'total warnings: %s\n' "$(grep -c 'warning:' "$log" || true)"
}

targets=("$@")
[ ${#targets[@]} -eq 0 ] && targets=(docs format)
if printf '%s\n' "${targets[@]}" | grep -qx all; then
    targets=(docs format tidy static sanitize warnings analyzer)
fi

for t in "${targets[@]}"; do
    case "$t" in
        docs) cmd_docs ;;
        format) cmd_format ;;
        tidy) cmd_tidy ;;
        static) cmd_static ;;
        sanitize) cmd_sanitize ;;
        thread) cmd_thread ;;
        warnings) cmd_warnings ;;
        analyzer) cmd_analyzer ;;
        *)
            bad "unknown target: $t"
            fail=1
            ;;
    esac
done

# One greppable verdict, because the individual checks are not: a failing format check prints
# "changed lines are not clang-format clean:", which contains neither OK nor FAILED, so reading the run
# by grepping the output turns a failure into apparent silence. Two sessions here recorded "format
# clean" from exactly that - `./scripts/sanity.sh | grep -E "OK|FAILED"` matched a docs line, and the
# pipe discarded the exit status, which was 1. The verdict is the exit status; this line repeats it
# where it can be seen.
if [ "$fail" -eq 0 ]; then
    printf '\033[32msanity: OK\033[0m\n'
else
    printf '\033[31msanity: FAILED\033[0m\n'
fi

exit "$fail"
