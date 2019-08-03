#include "Texture.h"
#define STB_IMAGE_IMPLEMENTATION
#include "../thirdparty/stb/stb_image.h"

Texture::Texture()
    : mDevice(VK_NULL_HANDLE),
      mTextureImage(VK_NULL_HANDLE),
      mDeviceMemory(VK_NULL_HANDLE),
      mImageView(VK_NULL_HANDLE),
      mTextureSampler(VK_NULL_HANDLE)
{
}

Texture::~Texture()
{
    if (mDevice != VK_NULL_HANDLE)
    {
        vkDestroySampler(mDevice, mTextureSampler, nullptr);
        vkDestroyImageView(mDevice, mImageView, nullptr);
        vkDestroyImage(mDevice, mTextureImage, nullptr);
        vkFreeMemory(mDevice, mDeviceMemory, nullptr);
    }
}

void Texture::LoadPixels(void *pixels, int width, int height,
                         const VkDevice &device,
                         const VkPhysicalDevice &physicalDevice,
                         const VkCommandPool &pool, const VkQueue &queue)
{
    mDevice = device;
    // Upload pixels to staging buffer
    Buffer stagingBuffer(device, physicalDevice, width * height * 4,
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    stagingBuffer.setData((void *)pixels);
    stbi_image_free(pixels);

    createImage(device, physicalDevice, width, height, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mTextureImage,
                mDeviceMemory);

    // UNDEFINED -> DST_OPTIMAL
    sTransitionImageLayout(device, pool, queue, mTextureImage,
                           VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Copy source to image
    mCopyBufferToImage(device, pool, queue, stagingBuffer.buffer(),
                       mTextureImage, static_cast<uint32_t>(width),
                       static_cast<uint32_t>(height));

    // DST_OPTIMAL -> SHADER_READ_ONLY
    sTransitionImageLayout(device, pool, queue, mTextureImage,
                           VK_FORMAT_R8G8B8A8_UNORM,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    mInitImageView();
    mInitSampler();
}

void Texture::LoadImage(const std::string path, const VkDevice &device,
                        const VkPhysicalDevice &physicalDevice,
                        const VkCommandPool &pool, const VkQueue &queue)
{
    mDevice = device;

    int width, height, channels;
    stbi_uc *pixels =
        stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    assert(pixels);
    LoadPixels((void *)pixels, width, height, device, physicalDevice, pool,
               queue);
}
