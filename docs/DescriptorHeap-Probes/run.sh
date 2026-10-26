#!/usr/bin/env bash
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT

slangc="${SLANGC:-slangc}"
spirv_val="${SPIRV_VAL:-spirv-val}"
python="${PYTHON:-python3}"

for probe in heap_function_local heap_global_negative heap_texture root_pointer; do
    "$slangc" "$here/$probe.slang" \
        -target spirv -profile glsl_450 \
        -capability spvDescriptorHeapEXT -capability spirv_1_4 \
        -entry main -o "$out/$probe.spv"
    "$spirv_val" --target-env vulkan1.4 "$out/$probe.spv"
done

# A real heap shader must not carry ordinary DescriptorSet/Binding decorations.
"$python" "$here/spirv_check.py" "$out/heap_function_local.spv" \
    --require-extension SPV_EXT_descriptor_heap \
    --require-extension SPV_KHR_untyped_pointers \
    --require-opcode 5119 \
    --require-opcode 5129 \
    --require-decoration 5124 \
    --forbid-binding
"$python" "$here/spirv_check.py" "$out/heap_texture.spv" \
    --require-extension SPV_EXT_descriptor_heap \
    --require-opcode 5129 \
    --require-decoration 5124 \
    --forbid-binding
"$python" "$here/spirv_check.py" "$out/root_pointer.spv" \
    --require-extension SPV_EXT_descriptor_heap \
    --require-extension SPV_KHR_physical_storage_buffer \
    --require-opcode 5119 \
    --require-opcode 5129 \
    --require-decoration 5124 \
    --forbid-binding

# Negative control: module-scope .Handle() is a parameter binding, not heap access.
"$python" "$here/spirv_check.py" "$out/heap_global_negative.spv" \
    --forbid-extension SPV_EXT_descriptor_heap \
    --require-binding

echo "descriptor-heap probes: OK"
