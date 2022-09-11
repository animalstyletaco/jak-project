
#include "common/dma/dma_chain_read.h"

#include "game/graphics/vulkan_renderer/BucketRenderer.h"
#include "game/graphics/vulkan_renderer/SkyBlendCommon.h"
#include "game/graphics/pipelines/vulkan.h"

class SkyBlendGPU {
 public:
  SkyBlendGPU(VkDevice device);
  ~SkyBlendGPU();
  void init_textures(TexturePool& tex_pool);
  SkyBlendStats do_sky_blends(DmaFollower& dma,
                              SharedRenderState* render_state,
                              ScopedProfilerNode& prof);

 private:
  VkBuffer m_vertex_buffer = VK_NULL_HANDLE;
  VkDeviceMemory m_vertex_memory = VK_NULL_HANDLE;
  VkDeviceSize m_vertex_device_size = 0;

  VkImage m_textures[2] = {VK_NULL_HANDLE};
  VkImageView m_texture_views[2] = {VK_NULL_HANDLE};
  VkDeviceMemory m_texture_memories[2] = {VK_NULL_HANDLE};
  VkDeviceSize m_texture_size[2] = {0};
  VkDevice m_device = VK_NULL_HANDLE;

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
