#pragma once

#include "game/graphics/general_renderer/BucketRenderer.h"
#include "game/graphics/general_renderer/ocean/OceanMid.h"
#include "game/graphics/general_renderer/ocean/OceanTexture.h"

/*!
 * OceanMidAndFar is the handler for the first ocean bucket.
 * This bucket runs three renderers:
 * - ocean-texture (handled by the OceanTexture C++ class)
 * - ocean-far (handled by this class, it's very simple)
 * - ocean-mid (handled by the C++ OceanMid class)
 */

class BaseOceanMidAndFar : public BaseBucketRenderer {
 public:
  BaseOceanMidAndFar(const std::string& name,
                 int my_id);

 protected:
  void handle_ocean_far(DmaFollower& dma,
                        BaseSharedRenderState* render_state,
                        ScopedProfilerNode& prof);
  void handle_ocean_mid(DmaFollower& dma,
                        BaseSharedRenderState* render_state,
                        ScopedProfilerNode& prof);

  virtual void direct_renderer_reset_state() = 0;
  virtual void direct_renderer_render_gif(const u8* data,
    u32 size,
    BaseSharedRenderState* render_state,
    ScopedProfilerNode& prof) = 0;
  virtual void direct_renderer_flush_pending(BaseSharedRenderState* render_state,
    ScopedProfilerNode& prof) = 0;
  virtual void direct_renderer_set_mipmap(bool) = 0;

  virtual void texture_renderer_handle_ocean_texture(DmaFollower& dma,
                                                     BaseSharedRenderState* render_state,
                                                     ScopedProfilerNode& prof) = 0;

  virtual void ocean_mid_renderer_run(DmaFollower& dma,
                                      BaseSharedRenderState* render_state,
                                      ScopedProfilerNode& prof) = 0;
};

class BaseOceanMidAndFarJak1 : public virtual BaseOceanMidAndFar {
 public:
  BaseOceanMidAndFarJak1(const std::string& name, int my_id) : BaseOceanMidAndFar(name, my_id){};

 protected:
  void render(DmaFollower& dma,
              BaseSharedRenderState* render_state,
              ScopedProfilerNode& prof) override;
};

class BaseOceanMidAndFarJak2 : public virtual BaseOceanMidAndFar {
 public:
  BaseOceanMidAndFarJak2(const std::string& name, int my_id) : BaseOceanMidAndFar(name, my_id){};

 protected:
  void render(DmaFollower& dma,
              BaseSharedRenderState* render_state,
              ScopedProfilerNode& prof) override;

 protected:
  void handle_ocean_89(DmaFollower& dma,
                       BaseSharedRenderState* render_state,
                       ScopedProfilerNode& prof);
  void handle_ocean_79(DmaFollower& dma,
                       BaseSharedRenderState* render_state,
                       ScopedProfilerNode& prof);
};
