#include <vulkan/vulkan.h>
#include <string>
#include "Buffer.h"
#define STB_IMAGE_IMPLEMENTATION
#include "thirdparty/stb_image.h"

class Texture
{
public:
    Texture(std::string path)
    {
        int width, height, texChannels = 0;
        mPixels = stbi_load(path.c_str(), &width, &height, &texChannels, STBI_rgb_alpha);
        mSize.width = width;
        mSize.height = height;
    }
    void LoadImage()
    {
    }
private:
    stbi_uc* mPixels;
    VkExtent2D mSize;
};
