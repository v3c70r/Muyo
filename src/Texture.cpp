#include "Texture.h"
#include "../thirdparty/stb/stb_image.h"

Texture::Texture() : m_textureSampler(VK_NULL_HANDLE) {}

Texture::~Texture()
{
    if (GetRenderDevice()->GetDevice() != VK_NULL_HANDLE)
    {
        vkDestroySampler(GetRenderDevice()->GetDevice(), m_textureSampler, nullptr);
        vkDestroyImageView(GetRenderDevice()->GetDevice(), m_view, nullptr);
        GetMemoryAllocator()->FreeImage(m_image, m_allocation);
    }
}

void Texture::LoadPixels(void *pixels, int width, int height)
{
    // Upload pixels to staging buffer
    const size_t BUFFER_SIZE = width * height * 4;
    VmaAllocation stagingAllocation;
    VkBuffer stagingBuffer;
    GetMemoryAllocator()->AllocateBuffer(
        BUFFER_SIZE, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_ONLY, stagingBuffer, stagingAllocation, "Texture");

    void *pMappedMemory = nullptr;
    GetMemoryAllocator()->MapBuffer(stagingAllocation, &pMappedMemory);
    memcpy(pMappedMemory, pixels, BUFFER_SIZE);
    GetMemoryAllocator()->UnmapBuffer(stagingAllocation);

    createImage(width, height, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY);

    // UNDEFINED -> DST_OPTIMAL
    sTransitionImageLayout(m_image,
                           VK_IMAGE_LAYOUT_UNDEFINED,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Copy source to image
    mCopyBufferToImage(stagingBuffer, m_image, static_cast<uint32_t>(width),
                       static_cast<uint32_t>(height));

    // DST_OPTIMAL -> SHADER_READ_ONLY
    sTransitionImageLayout(m_image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    GetMemoryAllocator()->FreeBuffer(stagingBuffer, stagingAllocation);
    mInitImageView();
    mInitSampler();
}

void Texture::LoadImage(const std::string path)
{

    int width, height, channels;
    stbi_uc *pixels =
        stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    assert(pixels);
    LoadPixels((void *)pixels, width, height);
}
