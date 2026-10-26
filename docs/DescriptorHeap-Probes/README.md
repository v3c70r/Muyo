# Descriptor-heap probes

Compile-only probes for the `VK_EXT_descriptor_heap` design. They are not part of the CMake shader
glob or the Doxygen API reference; B0 and the review use them as the reproducible evidence behind
[`../DescriptorHeap-Bindless-Design.md`](../DescriptorHeap-Bindless-Design.md) §16.

```bash
source <vulkansdk>/setup-env.sh
./run.sh
# descriptor-heap probes: OK
```

`run.sh` compiles each probe with Slang, validates the module for Vulkan 1.4, and checks the SPIR-V
words with `spirv_check.py`. The parser is deliberate: this SDK's `spirv-dis` does not know capability
5128 (`DescriptorHeapEXT`) and aborts on some valid modules.

## What each probe proves

| Probe | Expected |
| --- | --- |
| `heap_function_local.slang` | `SPV_EXT_descriptor_heap` + `SPV_KHR_untyped_pointers`; **no** `Binding` decorations |
| `heap_texture.slang` | image/sampler/combined access from function-local handles; no `Binding` decorations |
| `root_pointer.slang` | `PhysicalStorageBuffer` root pointer + heap buffers; no `Binding` decorations |
| `heap_global_negative.slang` | module-scope `.Handle()` is an ordinary parameter: `Binding` decorations, **no** heap extension |

## H1: `.Handle()` must be function-local

A module-scope declaration such as

```slang
StructuredBuffer<float> input = StructuredBuffer<float>.Handle(push.inputIndex);
```

compiles, but Slang treats it as a shader parameter. The SPIR-V carries `DescriptorSet`/`Binding`
decorations and no heap access. On a null-layout heap pipeline that is a binding against a layout that
does not exist, or a silent route through the mapping path. The same construct inside a function emits
the heap stack (`DescriptorHeapEXT`, `ResourceHeapEXT`, `OpBufferPointerEXT`,
`OpConstantSizeOfEXT`/`ArrayStrideIdEXT`).

`heap_global_negative.slang` is the control: it must contain `Binding` decorations and must not contain
`SPV_EXT_descriptor_heap`. B0's `.heap.spv` gate is the opposite: **no** `Binding` decoration in any
positive module.

## Driver-version caveat

`VK_EXT_descriptor_heap` and `shaderUntypedPointers` depend on the Mesa build behind llvmpipe. Mesa 26.2.5
exposed both in an earlier probe; a llvmpipe built with LLVM 20.1.2 exposed neither. CI's lavapipe is a
gate only when its own Mesa exposes them; record the driver, Mesa and loader versions with the result.
