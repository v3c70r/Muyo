#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <string>
#include <utility>
#include <vector>

#include "RenderGraph/TransientMemoryPlanner.h"

namespace Muyo::RenderGraph
{
namespace
{
/// A buffer-class request. Every buffer shares one class shape, so the only inputs that vary are the
/// requirements and the interval.
TransientMemoryRequest BufferRequest(std::string handle, VkDeviceSize size, VkDeviceSize alignment,
                                     std::size_t firstUse, std::size_t lastUse, uint32_t typeBits = 0xFFu)
{
    TransientMemoryRequest request;
    request.handle = std::move(handle);
    request.classKey = ClassOf(BufferResourceDesc{}, typeBits);
    request.size = size;
    request.alignment = alignment;
    request.memoryTypeBits = typeBits;
    request.firstUse = firstUse;
    request.lastUse = lastUse;
    return request;
}

/// An image-class request. The class comes from the create info, which is why the extent and format are
/// arguments here rather than a size: two images share a block only when every one of these matches.
TransientMemoryRequest ImageRequest(std::string handle, VkFormat format, VkExtent2D extent, VkDeviceSize size,
                                    std::size_t firstUse, std::size_t lastUse, uint32_t typeBits = 0xFFu)
{
    ImageResourceDesc desc{};
    desc.format = format;
    desc.extent = extent;

    TransientMemoryRequest request;
    request.handle = std::move(handle);
    request.classKey = ClassOf(desc, typeBits);
    request.size = size;
    request.alignment = 64;
    request.memoryTypeBits = typeBits;
    request.firstUse = firstUse;
    request.lastUse = lastUse;
    return request;
}
}  // namespace

TEST_CASE("TransientMemoryPlanner: transients whose uses do not overlap share one offset", "[TransientMemoryPlanner]")
{
    // The whole point of the planner, and the number a reviewer should ask for: two transients alive at
    // different times cost one buffer, not two.
    const std::vector<TransientMemoryRequest> requests = {
        BufferRequest("A", 1024, 256, 0, 1),
        BufferRequest("B", 1024, 256, 2, 3),
    };

    const TransientMemoryPlan plan = PlanTransientMemory(requests);

    CHECK(plan.SlotCount() == 1);
    CHECK(plan.reservedBytes == 1024);
    CHECK(plan.individualBytes == 2048);
    CHECK(plan.SharesMemory());
    CHECK(VerifyTransientMemoryPlan(plan, requests).empty());
}

TEST_CASE("TransientMemoryPlanner: transients whose uses overlap get their own offsets", "[TransientMemoryPlanner]")
{
    // Sharing here would be a correctness bug rather than a missed saving: B's first write would land in
    // memory A is still being read from.
    const std::vector<TransientMemoryRequest> requests = {
        BufferRequest("A", 1024, 256, 0, 3),
        BufferRequest("B", 1024, 256, 2, 4),
    };

    const TransientMemoryPlan plan = PlanTransientMemory(requests);

    CHECK(plan.SlotCount() == 2);
    CHECK(plan.reservedBytes == 2048);
    CHECK(plan.individualBytes == 2048);
    // Not sharing is a legitimate answer for a graph whose transients all overlap - the planner reports,
    // it does not fail.
    CHECK_FALSE(plan.SharesMemory());
    CHECK(VerifyTransientMemoryPlan(plan, requests).empty());
}

TEST_CASE("TransientMemoryPlanner: a freed offset is reused by a later transient", "[TransientMemoryPlanner]")
{
    // A[0,1] and B[2,3] fit in one offset; C[0,3] overlaps both and needs its own. The plan should find
    // that out rather than opening a third offset, which is what makes the packing a packing and not a
    // list.
    const std::vector<TransientMemoryRequest> requests = {
        BufferRequest("A", 1024, 256, 0, 1),
        BufferRequest("B", 1024, 256, 2, 3),
        BufferRequest("C", 1024, 256, 0, 3),
    };

    const TransientMemoryPlan plan = PlanTransientMemory(requests);

    CHECK(plan.SlotCount() == 2);
    CHECK(plan.reservedBytes == 2048);
    CHECK(plan.individualBytes == 3072);
    CHECK(VerifyTransientMemoryPlan(plan, requests).empty());
}

TEST_CASE("TransientMemoryPlanner: three disjoint transients share one offset", "[TransientMemoryPlanner]")
{
    const std::vector<TransientMemoryRequest> requests = {
        BufferRequest("A", 1024, 256, 0, 1),
        BufferRequest("B", 1024, 256, 2, 3),
        BufferRequest("C", 1024, 256, 4, 5),
    };

    const TransientMemoryPlan plan = PlanTransientMemory(requests);

    CHECK(plan.SlotCount() == 1);
    CHECK(plan.reservedBytes == 1024);
    CHECK(plan.individualBytes == 3072);
    CHECK(VerifyTransientMemoryPlan(plan, requests).empty());
}

TEST_CASE("TransientMemoryPlanner: an offset is aligned for every member, not only the widest",
          "[TransientMemoryPlanner]")
{
    // A and B share the first offset, which therefore has to satisfy A's 256 byte alignment and B's 64.
    // C runs across both of them and needs 128, so the second offset is 128 rather than starting at 100 -
    // the arithmetic a caller could get wrong by aligning only to the block and not to each offset.
    const std::vector<TransientMemoryRequest> requests = {
        BufferRequest("A", 100, 256, 0, 1),
        BufferRequest("B", 100, 64, 2, 3),
        BufferRequest("C", 100, 128, 0, 3),
    };

    const TransientMemoryPlan plan = PlanTransientMemory(requests);

    REQUIRE(plan.blocks.size() == 1);
    const TransientMemoryBlock& block = plan.blocks.front();
    REQUIRE(block.slots.size() == 2);
    CHECK(block.slots[0].offset == 0);
    CHECK(block.slots[0].alignment == 256);
    CHECK(block.slots[1].offset == 128);
    CHECK(block.alignment == 256);
    CHECK(block.size == 228);
    CHECK(VerifyTransientMemoryPlan(plan, requests).empty());
}

TEST_CASE("TransientMemoryPlanner: a buffer never shares a block with an image", "[TransientMemoryPlanner]")
{
    // Even with disjoint intervals: the classes are what make the plan legal, and a buffer and an image
    // are never in the same one.
    const std::vector<TransientMemoryRequest> requests = {
        BufferRequest("Buf", 4096, 256, 0, 1),
        ImageRequest("Img", VK_FORMAT_R8G8B8A8_UNORM, VkExtent2D{64, 64}, 4096, 2, 3),
    };

    const TransientMemoryPlan plan = PlanTransientMemory(requests);

    CHECK(plan.blocks.size() == 2);
    CHECK(plan.SlotCount() == 2);
    CHECK_FALSE(plan.SharesMemory());
    CHECK(VerifyTransientMemoryPlan(plan, requests).empty());
}

TEST_CASE("TransientMemoryPlanner: images with different create info are never aliased", "[TransientMemoryPlanner]")
{
    // Intervals that do not overlap, so a planner ignoring the create info would happily put these in one
    // offset. Different formats alias only with VK_IMAGE_CREATE_ALIAS_BIT and only within a format
    // compatibility class; requiring the create info to match removes the question.
    const std::vector<TransientMemoryRequest> requests = {
        ImageRequest("Color", VK_FORMAT_R8G8B8A8_UNORM, VkExtent2D{1024, 1024}, 4u * 1024u * 1024u, 0, 1),
        ImageRequest("Depth", VK_FORMAT_D32_SFLOAT, VkExtent2D{1024, 1024}, 4u * 1024u * 1024u, 2, 3),
    };

    const TransientMemoryPlan plan = PlanTransientMemory(requests);

    CHECK(plan.blocks.size() == 2);
    CHECK(plan.SlotCount() == 2);
    CHECK_FALSE(plan.SharesMemory());
    CHECK(VerifyTransientMemoryPlan(plan, requests).empty());
}

TEST_CASE("TransientMemoryPlanner: images with identical create info do share", "[TransientMemoryPlanner]")
{
    // The other side of the same rule. An implementation that put every image in its own block would pass
    // the test above and fail this one - and would leave the aliasing this issue exists for undone for the
    // case that matters most, a chain of full-screen targets.
    const std::vector<TransientMemoryRequest> requests = {
        ImageRequest("BloomA", VK_FORMAT_R16G16B16A16_SFLOAT, VkExtent2D{1920, 1080}, 8294400, 0, 1),
        ImageRequest("BloomB", VK_FORMAT_R16G16B16A16_SFLOAT, VkExtent2D{1920, 1080}, 8294400, 2, 3),
    };

    const TransientMemoryPlan plan = PlanTransientMemory(requests);

    REQUIRE(plan.blocks.size() == 1);
    CHECK(plan.SlotCount() == 1);
    CHECK(plan.reservedBytes == 8294400);
    CHECK(plan.individualBytes == 16588800);
    CHECK(plan.SharesMemory());
    CHECK(VerifyTransientMemoryPlan(plan, requests).empty());
}

TEST_CASE("TransientMemoryPlanner: resources with disjoint memory types do not share a block",
          "[TransientMemoryPlanner]")
{
    // A CPU-only buffer and a GPU-only one have disjoint memory type sets on a discrete device, and both
    // are ordinary. If the memory types were not part of the class, the planner would intersect them to
    // zero and either throw or hand the allocator a block it cannot create; instead they land in two
    // blocks, which is the correct answer and the reason the class carries them.
    const std::vector<TransientMemoryRequest> requests = {
        BufferRequest("Staging", 4096, 256, 0, 1, 0b10u),
        BufferRequest("GpuOnly", 4096, 256, 2, 3, 0b01u),
    };

    const TransientMemoryPlan plan = PlanTransientMemory(requests);

    CHECK(plan.blocks.size() == 2);
    CHECK_FALSE(plan.SharesMemory());
    CHECK(VerifyTransientMemoryPlan(plan, requests).empty());
}

TEST_CASE("TransientMemoryPlanner: a block's memory types are its members' intersection", "[TransientMemoryPlanner]")
{
    // The class normally carries the memory types, so two resources with *different* bits never reach one
    // block - that is the test above. This case builds the requests the way a caller that computed the
    // class itself could: one class, different masks inside it. The block then has to carry the
    // intersection rather than either member's mask, and an empty intersection has to be reported rather
    // than passed to the allocator as a zero mask.
    const TransientMemoryRequest a = BufferRequest("A", 1024, 256, 0, 1, 0b0111u);
    TransientMemoryRequest b = BufferRequest("B", 1024, 256, 2, 3, 0b0011u);
    b.classKey = a.classKey;  // a caller-built class: same shape, narrower mask inside it

    SECTION("The intersection is what the block carries")
    {
        const std::vector<TransientMemoryRequest> requests = {a, b};
        const TransientMemoryPlan plan = PlanTransientMemory(requests);

        REQUIRE(plan.blocks.size() == 1);
        CHECK(plan.blocks.front().memoryTypeBits == 0b0011u);
        CHECK(VerifyTransientMemoryPlan(plan, requests).empty());
    }
    SECTION("An empty intersection names the resources")
    {
        b.memoryTypeBits = 0b1000u;
        const std::vector<TransientMemoryRequest> requests = {a, b};
        CHECK_THROWS_WITH(PlanTransientMemory(requests),
                          Catch::Matchers::ContainsSubstring("'A'") && Catch::Matchers::ContainsSubstring("'B'"));
    }
}

TEST_CASE("TransientMemoryPlanner: a resource that allows no memory type is named", "[TransientMemoryPlanner]")
{
    // The intersection inside the planner is a safety net rather than the mechanism for separating
    // resources: the class carries the memory types, so two resources with *different* bits land in
    // different blocks by construction. What is left for the check to catch is a request that permits
    // nothing at all - a create info and usage combination the device offers no memory type for - and it
    // names what it found rather than handing the allocator a block with a zero mask.
    CHECK_THROWS_WITH(PlanTransientMemory({BufferRequest("NoType", 1024, 256, 0, 1, 0u)}),
                      Catch::Matchers::ContainsSubstring("'NoType'"));
}

TEST_CASE("TransientMemoryPlanner: transients on different queue-family domains never share an offset",
          "[TransientMemoryPlanner]")
{
    // The graph orders a hand-over within one queue family and not across families, so disjoint live
    // ranges are not enough: the executor only waits on a *shared handle*, and an aliased pair is two.
    // Same interval shape as the sharing case above, and it must still produce two offsets.
    std::vector<TransientMemoryRequest> requests = {
        BufferRequest("Graphics", 1024, 256, 0, 1),
        BufferRequest("AsyncCompute", 1024, 256, 2, 3),
    };
    requests[0].sharingDomain = 1;  // graphics family
    requests[1].sharingDomain = 2;  // dedicated compute family

    const TransientMemoryPlan plan = PlanTransientMemory(requests);

    CHECK(plan.SlotCount() == 2);
    CHECK(plan.reservedBytes == 2048);
    CHECK_FALSE(plan.SharesMemory());
    CHECK(VerifyTransientMemoryPlan(plan, requests).empty());
}

TEST_CASE("TransientMemoryPlanner: a request whose uses span queue families never shares an offset",
          "[TransientMemoryPlanner]")
{
    // Crossing families means the resource is live on both, so its bytes cannot be handed to anything -
    // not even another spanning resource, whose use could overlap it on either family. That is why
    // `canShareSlot` exists separately from the domain rather than being encoded as a unique domain.
    std::vector<TransientMemoryRequest> requests = {
        BufferRequest("Spanning", 1024, 256, 0, 1),
        BufferRequest("AlsoSpanning", 1024, 256, 2, 3),
        BufferRequest("Plain", 1024, 256, 4, 5),
    };
    requests[0].sharingDomain = 1;
    requests[0].canShareSlot = false;
    requests[1].sharingDomain = 1;
    requests[1].canShareSlot = false;

    const TransientMemoryPlan plan = PlanTransientMemory(requests);

    CHECK(plan.SlotCount() == 3);
    CHECK(VerifyTransientMemoryPlan(plan, requests).empty());
}

TEST_CASE("TransientMemoryPlanner: an empty graph plans nothing", "[TransientMemoryPlanner]")
{
    const std::vector<TransientMemoryRequest> requests;
    const TransientMemoryPlan plan = PlanTransientMemory(requests);

    CHECK(plan.blocks.empty());
    CHECK(plan.reservedBytes == 0);
    CHECK_FALSE(plan.SharesMemory());
    CHECK(VerifyTransientMemoryPlan(plan, requests).empty());
}

TEST_CASE("TransientMemoryPlanner: input it cannot satisfy is rejected by name", "[TransientMemoryPlanner]")
{
    SECTION("A zero alignment")
    {
        // Cannot be aligned to, and the arithmetic would divide by zero rather than fail.
        CHECK_THROWS_WITH(PlanTransientMemory({BufferRequest("A", 1024, 0, 0, 1)}),
                          Catch::Matchers::ContainsSubstring("'A'") && Catch::Matchers::ContainsSubstring("alignment"));
    }
    SECTION("An inverted interval")
    {
        // firstUse > lastUse means the live range came from somewhere that does not order nodes, and every
        // sharing decision downstream would be made on nonsense.
        CHECK_THROWS_WITH(PlanTransientMemory({BufferRequest("A", 1024, 256, 5, 2)}),
                          Catch::Matchers::ContainsSubstring("'A'") && Catch::Matchers::ContainsSubstring("inverted"));
    }
}

TEST_CASE("TransientMemoryPlanner: the verifier rejects a plan that broke its own promise", "[TransientMemoryPlanner]")
{
    // A verifier that always returns "fine" would make every VerifyTransientMemoryPlan call above a
    // formality, so it gets the same treatment as the planner: mutations that must be caught.
    const std::vector<TransientMemoryRequest> shared = {
        BufferRequest("A", 1024, 256, 0, 1),
        BufferRequest("B", 1024, 256, 2, 3),
    };
    const TransientMemoryPlan good = PlanTransientMemory(shared);
    REQUIRE(VerifyTransientMemoryPlan(good, shared).empty());

    SECTION("Two resources sharing an offset whose live ranges are made to overlap")
    {
        // The plan was legal for the intervals it was given. Change the intervals and the same plan is a
        // use-after-free - which is the check the graph relies on when it plans and then executes.
        const std::vector<TransientMemoryRequest> overlapping = {
            BufferRequest("A", 1024, 256, 0, 3),
            BufferRequest("B", 1024, 256, 2, 4),
        };
        const std::string problem = VerifyTransientMemoryPlan(good, overlapping);
        INFO("verifier said: " << problem);
        CHECK_THAT(problem, Catch::Matchers::ContainsSubstring("'A'") && Catch::Matchers::ContainsSubstring("'B'") &&
                                Catch::Matchers::ContainsSubstring("overlap"));
    }
    SECTION("A request that was never placed")
    {
        std::vector<TransientMemoryRequest> extra = shared;
        extra.push_back(BufferRequest("C", 512, 256, 0, 1));
        // The byte total is brought up to date too, so that the placement is what the verifier objects to
        // rather than the arithmetic - i.e. the check this section is about.
        TransientMemoryPlan plan = good;
        plan.individualBytes = 2560;
        const std::string problem = VerifyTransientMemoryPlan(plan, extra);
        INFO("verifier said: " << problem);
        CHECK_THAT(problem, Catch::Matchers::ContainsSubstring("'C'"));
    }
    SECTION("A slot moved onto the one before it")
    {
        const std::vector<TransientMemoryRequest> twoSlots = {
            BufferRequest("A", 1024, 256, 0, 3),
            BufferRequest("B", 1024, 256, 2, 4),
        };
        TransientMemoryPlan broken = PlanTransientMemory(twoSlots);
        REQUIRE(broken.blocks.size() == 1);
        REQUIRE(broken.blocks.front().slots.size() == 2);
        // The byte totals are left alone, so the overlap is the only thing wrong with it.
        broken.blocks.front().slots[1].offset = 0;
        const std::string problem = VerifyTransientMemoryPlan(broken, twoSlots);
        INFO("verifier said: " << problem);
        CHECK_THAT(problem, Catch::Matchers::ContainsSubstring("overlap"));
    }
    SECTION("Two resources sharing an offset across queue families")
    {
        std::vector<TransientMemoryRequest> crossFamily = shared;
        crossFamily[0].sharingDomain = 1;
        crossFamily[1].sharingDomain = 2;
        const std::string problem = VerifyTransientMemoryPlan(good, crossFamily);
        INFO("verifier said: " << problem);
        CHECK_THAT(problem, Catch::Matchers::ContainsSubstring("different queue families"));
    }
    SECTION("A spanning resource sharing an offset")
    {
        std::vector<TransientMemoryRequest> spanning = shared;
        spanning[0].canShareSlot = false;
        const std::string problem = VerifyTransientMemoryPlan(good, spanning);
        INFO("verifier said: " << problem);
        CHECK_THAT(problem, Catch::Matchers::ContainsSubstring("cannot share a slot"));
    }
}
}  // namespace Muyo::RenderGraph
