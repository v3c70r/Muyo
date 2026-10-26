#!/usr/bin/env python3
"""Minimal SPIR-V checks for the descriptor-heap probes.

The SDK's spirv-dis on this machine does not know capability 5128
(DescriptorHeapEXT), so this parser checks the words directly.
"""

import argparse
import struct
import sys

OP_EXTENSION = 10
OP_DECORATE = 71
OP_DECORATE_ID = 332
DECORATION_BINDING = 33
DECORATION_DESCRIPTOR_SET = 34
DECORATION_ARRAY_STRIDE_ID_EXT = 5124
OP_BUFFER_POINTER_EXT = 5119
OP_CONSTANT_SIZE_OF_EXT = 5129


def decode_string(words):
    raw = b"".join(struct.pack("<I", word) for word in words)
    return raw.split(b"\0", 1)[0].decode("utf-8", "replace")


def parse(path):
    with open(path, "rb") as handle:
        data = handle.read()
    if len(data) < 20 or len(data) % 4:
        raise SystemExit(f"{path}: not a SPIR-V module")
    words = struct.unpack(f"<{len(data) // 4}I", data)
    if words[0] != 0x07230203:
        raise SystemExit(f"{path}: bad SPIR-V magic")

    extensions = []
    opcodes = set()
    decorations = set()
    binding_decorations = 0
    descriptor_set_decorations = 0

    index = 5
    while index < len(words):
        instruction = words[index]
        word_count = instruction >> 16
        opcode = instruction & 0xFFFF
        if word_count == 0 or index + word_count > len(words):
            raise SystemExit(f"{path}: malformed instruction at word {index}")
        operands = words[index + 1 : index + word_count]
        opcodes.add(opcode)
        if opcode == OP_EXTENSION and operands:
            extensions.append(decode_string(operands))
        elif opcode in (OP_DECORATE, OP_DECORATE_ID) and len(operands) >= 2:
            decorations.add(operands[1])
            if opcode == OP_DECORATE and operands[1] == DECORATION_BINDING:
                binding_decorations += 1
            elif opcode == OP_DECORATE and operands[1] == DECORATION_DESCRIPTOR_SET:
                descriptor_set_decorations += 1
        index += word_count

    return extensions, opcodes, decorations, binding_decorations, descriptor_set_decorations


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("module")
    parser.add_argument("--require-extension", action="append", default=[])
    parser.add_argument("--forbid-extension", action="append", default=[])
    parser.add_argument("--require-binding", action="store_true")
    parser.add_argument("--forbid-binding", action="store_true")
    parser.add_argument("--require-opcode", action="append", type=int, default=[])
    parser.add_argument("--require-decoration", action="append", type=int, default=[])
    args = parser.parse_args()

    extensions, opcodes, decorations, bindings, descriptor_sets = parse(args.module)
    failures = []
    for extension in args.require_extension:
        if extension not in extensions:
            failures.append(f"missing extension {extension}")
    for extension in args.forbid_extension:
        if extension in extensions:
            failures.append(f"unexpected extension {extension}")
    if args.require_binding and bindings == 0:
        failures.append("expected a Binding decoration")
    if args.forbid_binding and bindings != 0:
        failures.append(f"unexpected {bindings} Binding decoration(s)")
    for opcode in args.require_opcode:
        if opcode not in opcodes:
            failures.append(f"missing opcode {opcode}")
    for decoration in args.require_decoration:
        if decoration not in decorations:
            failures.append(f"missing decoration {decoration}")

    print(
        f"{args.module}: extensions={','.join(extensions) or '-'} "
        f"bindings={bindings} descriptor_sets={descriptor_sets}"
    )
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
