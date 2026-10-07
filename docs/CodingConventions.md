# Coding conventions

Short, enforced-by-review rules. Add to this file rather than scattering the same note around the
code.

## Never initialize Vulkan structs positionally

Fill Vulkan structs **by name** — field-by-field assignment or C++20 designated initializers.
Do not use a positional aggregate initializer with more than the `sType`:

```cpp
// NO - the field order is easy to get wrong and the compiler cannot tell you.
VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, nullptr, srcStage, dstStage,
                         srcAccess, dstAccess};
```

```cpp
// YES - field-by-field.
VkMemoryBarrier2 barrier{};
barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
barrier.srcStageMask = srcStage;
barrier.dstStageMask = dstStage;
barrier.srcAccessMask = srcAccess;
barrier.dstAccessMask = dstAccess;
```

```cpp
// YES - C++20 designated initializers. Designators must follow declaration order, which the
// compiler checks, so a reordered or missing field is a compile error rather than a silent bug.
VkMemoryBarrier2 barrier{
    .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
    .srcStageMask = srcStage,
    .dstStageMask = dstStage,
    .srcAccessMask = srcAccess,
    .dstAccessMask = dstAccess,
};
```

`{}` and `{VK_STRUCTURE_TYPE_...}` (setting only `sType`, everything else zero) are fine. The rule
only concerns positional lists of several fields.

### Why

Member *order* changes between API generations, and it is a property of the headers you compile
against — never assume it. The sync2 barriers are the trap:

| Struct | Field order in the Vulkan headers this repository builds against |
| --- | --- |
| `VkMemoryBarrier` | `sType, pNext, srcAccessMask, dstAccessMask` |
| `VkMemoryBarrier2` | `sType, pNext, srcStageMask, srcAccessMask, dstStageMask, dstAccessMask` |

So the obvious conversion of the legacy initializer — inserting the two stage masks after `pNext` —
puts the stage bits into the access masks:

```cpp
// Legacy (correct for VkMemoryBarrier).
VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, srcAccess, dstAccess};

// "Migrated" positionally: srcAccessMask receives the *stage* bit, dstStageMask the *access* bit.
VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, nullptr, srcStage, dstStage,
                         srcAccess, dstAccess};
```

The failure is nasty to read, because a stage bit and an unrelated access bit can share a value:

```
vkCmdPipelineBarrier2(): pDependencyInfo->pMemoryBarriers[0].srcAccessMask
    (VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT) is not supported by stage mask
    (VK_PIPELINE_STAGE_TRANSFER_BIT).
```

That message is a *stage* bit (`VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR`,
`0x02000000`) reported as an access bit (`VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT`, also
`0x02000000`). Field assignment avoids the whole class of bug.
