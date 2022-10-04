
#include "common/dma/dma_chain_read.h"

#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/SkyBlendCommon.h"
#include "game/graphics/pipelines/vulkan_pipeline.h"

class SkyBlendGPU {
 public:
  SkyBlendGPU(std::unique_ptr<GraphicsDeviceVulkan>& device);
  ~SkyBlendGPU();
  void init_textures(TexturePool& tex_pool);
  SkyBlendStats do_sky_blends(DmaFollower& dma,
                              SharedRenderState* render_state,
                              ScopedProfilerNode& prof);

 private:
  std::unique_ptr<VertexBuffer> m_vertex_buffer;
  std::unique_ptr<TextureInfo> m_textures[2];
  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
  GraphicsPipelineLayout m_pipeline_layout;
  PipelineConfigInfo m_pipeline_config_info;

  int m_sizes[2] = {32, 64};

  struct Vertex {
    float x = 0;
    float y = 0;
    float intensity = 0;
  };

  Vertex m_vertex_data[6];

  struct TexInfo {
    GpuTexture* tex;
    u32 tbp;
  } m_tex_info[2];
};
