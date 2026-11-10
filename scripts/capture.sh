#!/usr/bin/env bash
#
# Capture a Muyo application with RenderDoc through GLFW's X11 backend.
#
# RenderDoc 1.46's capture launcher stops advertising loader-provided instance extensions, including
# VK_KHR_wayland_surface. GLFW therefore selects Wayland, finds no WSI extension, and the application
# aborts before Vulkan starts. Hiding WAYLAND_DISPLAY makes GLFW select Xwayland instead, which is the
# one combination that captures on a Wayland session (#220).
#
# renderdoccmd execs the program argument directly: it searches neither the cwd nor PATH, and a bare
# name fails in milliseconds with an error that points at the wrong cause. This script resolves the
# program to an absolute path first.
#
# Usage: scripts/capture.sh [-f] [-o output.rdc] <program> [program arguments...]
#
# Options:
#   -o output.rdc  Capture file to write (default: <program name>.rdc in the current directory).
#   -f             Replace an existing capture file.
#   -h             Show this help.
#
# Environment:
#   RENDERDOC_CMD  renderdoccmd executable (default: renderdoccmd from PATH).
#
# The program must be built with GLFW's X11 backend. A fresh configure builds both backends; an
# existing build tree created before #220 needs -DGLFW_BUILD_X11=ON (see AGENTS.md section 4).

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/capture.sh [-f] [-o output.rdc] <program> [program arguments...]

Capture a Muyo application with RenderDoc through GLFW's X11 backend (Xwayland).

Options:
  -o output.rdc  Capture file to write (default: <program name>.rdc in the current directory).
  -f             Replace an existing capture file.
  -h             Show this help.

Environment:
  RENDERDOC_CMD  renderdoccmd executable (default: renderdoccmd from PATH).
EOF
}

force=0
output=""
while getopts ":o:fh" opt; do
    case "$opt" in
        o) output=$OPTARG ;;
        f) force=1 ;;
        h)
            usage
            exit 0
            ;;
        :)
            echo "capture.sh: -$OPTARG needs an argument" >&2
            exit 2
            ;;
        \?)
            echo "capture.sh: unknown option -$OPTARG" >&2
            exit 2
            ;;
    esac
done
shift $((OPTIND - 1))

if [[ $# -lt 1 ]]; then
    usage >&2
    exit 2
fi

program=$1
shift

# renderdoccmd execs the program argument directly, so resolve it here: a relative path or a name on
# PATH still works, and the launcher always receives an absolute path.
if [[ "$program" != /* ]]; then
    if [[ "$program" == */* ]]; then
        program="$(realpath -- "$program")"
    elif command -v "$program" >/dev/null 2>&1; then
        program="$(command -v "$program")"
    else
        echo "capture.sh: '$program' is not absolute and not found on PATH;" \
             "pass the path to the built binary" >&2
        exit 2
    fi
fi
if [[ ! -x "$program" ]]; then
    echo "capture.sh: '$program' is not an executable file" >&2
    exit 2
fi

renderdoc_cmd=${RENDERDOC_CMD:-}
if [[ -z "$renderdoc_cmd" ]]; then
    renderdoc_cmd="$(command -v renderdoccmd || true)"
elif [[ "$renderdoc_cmd" != /* ]]; then
    if [[ "$renderdoc_cmd" == */* ]]; then
        renderdoc_cmd="$(realpath -- "$renderdoc_cmd")"
    else
        renderdoc_cmd="$(command -v "$renderdoc_cmd" || true)"
    fi
fi
if [[ -z "$renderdoc_cmd" ]]; then
    echo "capture.sh: renderdoccmd not found; set RENDERDOC_CMD or add it to PATH" >&2
    exit 2
fi
if [[ ! -x "$renderdoc_cmd" ]]; then
    echo "capture.sh: '$renderdoc_cmd' is not executable" >&2
    exit 2
fi

if [[ -z "${DISPLAY:-}" ]]; then
    echo "capture.sh: DISPLAY is unset; the X11 (Xwayland) backend cannot create a window" >&2
    exit 2
fi

if [[ -z "$output" ]]; then
    output="$PWD/$(basename -- "$program").rdc"
fi
# -m resolves a path whose directory does not exist yet; renderdoccmd creates the file.
output="$(realpath -m -- "$output")"

if [[ -e "$output" && $force -eq 0 ]]; then
    echo "capture.sh: '$output' already exists; remove it or pass -f" >&2
    exit 2
fi
if [[ $force -eq 1 ]]; then
    rm -f -- "$output"
fi

echo "capture.sh: $program -> $output"
echo "capture.sh: X11 backend (WAYLAND_DISPLAY unset), renderdoccmd: $renderdoc_cmd"

# Unset WAYLAND_DISPLAY for renderdoccmd *and* the child it launches, so GLFW selects X11.
env -u WAYLAND_DISPLAY "$renderdoc_cmd" capture -c "$output" "$program" "$@"

if [[ ! -s "$output" ]]; then
    echo "capture.sh: renderdoccmd exited successfully but wrote no capture at '$output'" >&2
    exit 1
fi
echo "capture.sh: wrote $output"
