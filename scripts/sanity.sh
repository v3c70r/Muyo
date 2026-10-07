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
#   scripts/sanity.sh tidy         clang-tidy on the changed files (report only)
#   scripts/sanity.sh static       Clang static analyzer over a dedicated build (slow)
#   scripts/sanity.sh sanitize     build + run the tests under ASan/UBSan
#   scripts/sanity.sh warnings     -Wextra / -fanalyzer fallout (slow, noisy)
#   scripts/sanity.sh all          everything above
#
# Environment:
#   BASE=<ref>   comparison ref for the format check (default: origin/master)

set -uo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT" || exit 1
BASE="${BASE:-origin/master}"
JOBS="$(nproc 2>/dev/null || echo 4)"

SRC_GLOBS=('src/*.cpp' 'src/*.h' 'src/**/*.cpp' 'src/**/*.h')

bold() { printf '\n\033[1m== %s ==\033[0m\n' "$*"; }
ok() { printf '\033[32m%s\033[0m\n' "$*"; }
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
    bold "tidy: changed files (report only, .clang-tidy is not baselined yet)"
    local tool
    tool="$(command -v clang-tidy-21 || command -v clang-tidy || true)"
    if [ -z "$tool" ]; then
        bad "clang-tidy not found; install clang-tidy (issue #24)"
        return
    fi
    if [ ! -f build/compile_commands.json ]; then
        bad "build/compile_commands.json not found - clang-tidy needs a compilation database"
        echo "  configure first:  cmake -S . -B build"
        fail=1
        return
    fi

    local files
    files="$(git diff --name-only --diff-filter=ACMR "$BASE" -- "${SRC_GLOBS[@]}" | grep -E '\.(cpp|h)$' || true)"
    if [ -z "$files" ]; then
        ok "no changed C++ files"
        return
    fi

    local log=/tmp/muyo-tidy.log
    : >"$log"
    while IFS= read -r f; do
        [ -f "$f" ] || continue
        echo "### $f" >>"$log"
        "$tool" -p build "$f" >>"$log" 2>&1
    done <<<"$files"

    echo "diagnostics by check (user code):"
    local pattern='\[[a-z0-9]+(-[a-z0-9]+)*(,[a-z0-9-]+)*\]$'
    grep -oE "$pattern" "$log" | sed 's/[][]//g' | tr ',' '\n' \
        | grep -v warnings-as-errors | sort | uniq -c | sort -rn | head -20
    # Count the check-tagged lines themselves: portable, and independent of the source path.
    printf '\ntotal: %s   full log: %s\n' "$(grep -cE "$pattern" "$log" || true)" "$log"
    echo "report-only for now: do not fail the build on these until #24 baselines them"
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
    cmake -S . -B build-static >/dev/null || {
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

cmd_sanitize() {
    bold "sanitize: ASan + UBSan build and tests"
    cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Sanitize >/dev/null || {
        bad "configure failed"
        fail=1
        return
    }
    cmake --build build-san --target tests -j"$JOBS" || {
        bad "build failed"
        fail=1
        return
    }
    if (cd build-san && LSAN_OPTIONS="suppressions=$ROOT/san.supp:print_suppressions=1" ./tests); then
        ok "sanitize OK"
    else
        bad "tests failed under sanitizers"
        fail=1
    fi
}

cmd_warnings() {
    bold "warnings: -Wextra and gcc -fanalyzer fallout"
    echo "these are informational today (issue #25); the goal is a baseline, not zero"
    local log=/tmp/muyo-warnings.log
    cmake -S . -B build-warn -DCMAKE_CXX_FLAGS="-std=c++20 -Wall -Wextra -Wno-missing-braces -fanalyzer -pthread -g" >/dev/null || {
        bad "configure failed"
        fail=1
        return
    }
    cmake --build build-warn -j"$JOBS" 2>&1 | tee "$log" | grep -E "warning:" | sed 's/^.*warning: /warning: /' | sort | uniq -c | sort -rn | head -30
    printf '\nfull log: %s\n' "$log"
    printf 'total warnings: %s\n' "$(grep -c 'warning:' "$log" || true)"
}

targets=("$@")
[ ${#targets[@]} -eq 0 ] && targets=(docs format)
if printf '%s\n' "${targets[@]}" | grep -qx all; then
    targets=(docs format static sanitize warnings)
fi

for t in "${targets[@]}"; do
    case "$t" in
        docs) cmd_docs ;;
        format) cmd_format ;;
        tidy) cmd_tidy ;;
        static) cmd_static ;;
        sanitize) cmd_sanitize ;;
        warnings) cmd_warnings ;;
        *)
            bad "unknown target: $t"
            fail=1
            ;;
    esac
done

exit "$fail"
