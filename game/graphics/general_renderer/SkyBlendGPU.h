#pragma once
#include "common/dma/dma_chain_read.h"

#include "game/graphics/general_renderer/BucketRenderer.h"
#include "game/graphics/general_renderer/SkyBlendCommon.h"

class BaseSkyBlendGPU {
 public:
  BaseSkyBlendGPU();
  virtual ~BaseSkyBlendGPU();

 protected:
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
