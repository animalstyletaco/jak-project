#pragma once

#include "common/dma/dma_chain_read.h"

#include "game/graphics/general_renderer/BucketRenderer.h"
#include "game/graphics/general_renderer/SkyBlendCommon.h"

class BaseSkyBlendCPU {
 public:
  BaseSkyBlendCPU();
  ~BaseSkyBlendCPU();

  SkyBlendStats do_sky_blends(DmaFollower& dma,
                              BaseSharedRenderState* render_state,
                              ScopedProfilerNode& prof);

 protected:
  void blend_sky_initial_fast(u8 intensity, u8* out, const u8* in, u32 size);
  void blend_sky_fast(u8 intensity, u8* out, const u8* in, u32 size);

  static constexpr int m_sizes[2] = {32, 64};
  std::vector<u8> m_texture_data[2];

  virtual void setup_gpu_texture(u32, bool, u32, u32, int, SkyBlendStats&) = 0;
};
