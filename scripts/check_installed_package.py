#!/usr/bin/env python3
"""Verify an installed `muyo_rg` prefix is the stated package (#298).

`render_graph_docs.py --check` compares `CMakeLists.txt`'s `MUYO_RG_PUBLIC_HEADERS` with the canonical
`PUBLIC_HEADERS` list in the source tree. This script checks the *installed* tree instead, which is what
a consumer sees: a `foreach()` that silently skips a header, an extra `install(DIRECTORY ...)`, or the
internal-detail directory coming back would change the package without changing that source-side check.

It also states the layout the probe relies on: the package files, the three static libraries and the
generated `<muyo_rg/version.h>`. Run it after `cmake --install`:

    python3 scripts/check_installed_package.py --prefix "$RUNNER_TEMP/muyo-install"

Exit status 0 means the installed public surface is exactly `PUBLIC_HEADERS`, no internal graph
headers are installed, and the package files and libraries are present.
"""

from __future__ import annotations

import argparse
import glob
import os

from render_graph_docs import PUBLIC_HEADERS


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--prefix", required=True, help="the prefix `cmake --install` wrote to")
    args = parser.parse_args()

    prefix = os.path.abspath(args.prefix)
    errors: list[str] = []

    public_dir = os.path.join(prefix, "include", "RenderGraph")
    installed: set[str] = set()
    if not os.path.isdir(public_dir):
        errors.append(f"{public_dir} is missing; the public headers are not installed")
    else:
        entries = sorted(os.listdir(public_dir))
        installed = {name for name in entries if name.endswith(".h")}
        non_headers = [name for name in entries if not name.endswith(".h")]
        if non_headers:
            errors.append("include/RenderGraph contains non-header entries: " + ", ".join(non_headers))

    expected = set(PUBLIC_HEADERS)
    missing = sorted(expected - installed)
    extra = sorted(installed - expected)
    if missing:
        errors.append("include/RenderGraph omits: " + ", ".join(missing))
    if extra:
        errors.append("include/RenderGraph installs unclassified headers: " + ", ".join(extra))

    detail = os.path.join(prefix, "include", "Muyo", "detail", "RenderGraph")
    if os.path.exists(detail):
        errors.append(
            "include/Muyo/detail/RenderGraph exists; the internal graph headers are not part of the "
            "package (#297, #300)"
        )

    version_header = os.path.join(prefix, "include", "muyo_rg", "version.h")
    if not os.path.isfile(version_header):
        errors.append("include/muyo_rg/version.h is missing; MuyoVersion.h cannot compile without it (#87)")

    # The package files can live under lib/ or lib/<arch>/ depending on GNUInstallDirs, so locate them
    # rather than assuming one layout.
    package_dirs = sorted(glob.glob(os.path.join(prefix, "lib*", "**", "cmake", "muyo_rg"), recursive=True))
    if len(package_dirs) != 1:
        errors.append(
            "expected exactly one lib*/cmake/muyo_rg directory, found: "
            + (", ".join(package_dirs) if package_dirs else "none")
        )
    else:
        package_dir = package_dirs[0]
        for name in ("muyo_rgConfig.cmake", "muyo_rgConfigVersion.cmake", "muyo_rgTargets.cmake"):
            if not os.path.isfile(os.path.join(package_dir, name)):
                errors.append(f"{os.path.relpath(os.path.join(package_dir, name), prefix)} is missing")
        library_dir = os.path.dirname(os.path.dirname(package_dir))
        for name in ("libmuyo_rg.a", "libmuyo_device.a", "libSPIRV-Reflect.a"):
            if not os.path.isfile(os.path.join(library_dir, name)):
                errors.append(f"{os.path.relpath(os.path.join(library_dir, name), prefix)} is missing")

    if errors:
        for error in errors:
            print(f"check_installed_package: {error}")
        print("check_installed_package: the installed package differs from the stated surface")
        return 1

    print(
        f"check_installed_package: OK - {len(expected)} public headers, no internal graph headers, "
        f"package files and libraries present in {prefix}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
