#pragma once
#include "RenderPass.h"
#include "RenderPassParameters.h"

namespace Muyo
{
class RenderPassGEMM : public RenderPass
{
    struct MatrixParams
    {
        VkDeviceAddress pAddrA;
        VkDeviceAddress pBddrB;
        VkDeviceAddress pDddrC;
        VkDeviceAddress pDddrO;
    };
    enum TestType
    {
        TT_SHARED = 0,
        TT_TILED,
        TT_COUNT,
    };

    struct TestCase
    {
        TestType testType;
        VkComponentTypeNV inputType;
        VkComponentTypeNV outputType;

        // MxNxK is the size of the full matrix multiply
        uint32_t M;
        uint32_t N;
        uint32_t K;

        // Each cooperative matrix multiply is lMxlNxlK
        uint32_t lM;
        uint32_t lN;
        uint32_t lK;

        // size of workgroup tile in destination matrix
        uint32_t TILE_M;
        uint32_t TILE_N;
        uint32_t TILE_K;

        bool BColMajor;
        uint32_t ARowLen;
        uint32_t ANumRows;
        uint32_t BRowLen;
        uint32_t BNumRows;
    };
public:
    void PrepareRenderPass() override;
    VkCommandBuffer GetCommandBuffer() const override
    {
        return m_cmdBuf;
    }
    void RecordCommandBuffer();
    ~RenderPassGEMM() override;
private:
    static constexpr int M = 256;
    static constexpr int N = 1024;
    static constexpr int K = 2048;

    static constexpr int TILE_M = 128;
    static constexpr int TILE_N = 128;
    static constexpr int TILE_K = 32;

    void CreatePipeline() override;
    void GetCoopMatProperties();
    VkCommandBuffer m_cmdBuf = VK_NULL_HANDLE;
    std::vector<VkCooperativeMatrixPropertiesNV> m_vCoopMatProperties;
};
}
