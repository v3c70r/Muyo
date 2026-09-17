#!/usr/bin/env python3
"""Enforce documentation coverage of the RenderGraph public API.

Reads the Doxygen XML output and fails if any public entity (class/struct/enum,
their public members and enum values, and free functions/variables in the
RenderGraph namespaces) has neither a brief nor a detailed description.

Entities marked with Doxygen's ``\\internal`` are excluded by Doxygen itself
(``INTERNAL_DOCS = NO``), so they never appear here.

Usage: check_api_coverage.py <doxygen-xml-dir>
Exit code 0 when covered, 1 otherwise.
"""

from __future__ import annotations

import os
import sys
import xml.etree.ElementTree as ET

# Compound kinds that make up the public API surface.
API_COMPOUND_KINDS = {"class", "struct", "enum", "namespace"}

# sectiondef kinds that carry public API members.
PUBLIC_SECTION_PREFIXES = ("public-",)
PUBLIC_SECTION_KINDS = {"enumvalue", "func", "var", "typedef", "enum"}


def _text(element: ET.Element | None) -> str:
    if element is None:
        return ""
    return "".join(element.itertext()).strip()


def _is_documented(member: ET.Element) -> bool:
    return bool(_text(member.find("briefdescription"))) or bool(
        _text(member.find("detaileddescription"))
    )


def _public_section(kind: str) -> bool:
    return kind.startswith(PUBLIC_SECTION_PREFIXES) or kind in PUBLIC_SECTION_KINDS


def check(xml_dir: str) -> list[str]:
    index_path = os.path.join(xml_dir, "index.xml")
    if not os.path.isfile(index_path):
        raise SystemExit(f"check_api_coverage: no Doxygen index.xml in {xml_dir!r}")

    index = ET.parse(index_path).getroot()
    missing: list[str] = []

    for compound in index.findall("compound"):
        kind = compound.get("kind")
        if kind not in API_COMPOUND_KINDS:
            continue

        refid = compound.get("refid")
        path = os.path.join(xml_dir, f"{refid}.xml")
        if not os.path.isfile(path):
            continue

        compounddef = ET.parse(path).getroot().find("compounddef")
        if compounddef is None or compounddef.get("prot") not in (None, "public"):
            continue

        name = compounddef.findtext("compoundname") or compound.get("name")

        # The compound itself (not required for namespaces, which are just containers).
        if kind != "namespace" and not _is_documented(compounddef):
            missing.append(f"{name} ({kind})")

        for section in compounddef.findall("sectiondef"):
            section_kind = section.get("kind") or ""
            if not _public_section(section_kind):
                continue
            for member in section.findall("memberdef"):
                if member.get("prot") not in (None, "public"):
                    continue
                member_name = member.findtext("name") or "<anonymous>"
                if not _is_documented(member):
                    missing.append(f"{name}::{member_name}")

    return sorted(set(missing))


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__)
        return 2

    missing = check(argv[1])
    if not missing:
        print("RenderGraph API documentation coverage: OK")
        return 0

    print(f"RenderGraph API documentation is missing for {len(missing)} entit(ies):")
    for entry in missing:
        print(f"  - {entry}")
    print("\nAdd a Doxygen brief (e.g. '/// One line summary.') above each entry.")
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
