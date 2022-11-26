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

class BaseOceanMidAndFar : public BaseBucketRenderer, public CommonOceanTextureRendererInterface {
 public:
  BaseOceanMidAndFar(const std::string& name,
                 int my_id);
  void render(DmaFollower& dma, BaseSharedRenderState* render_state, ScopedProfilerNode& prof) override;
  void draw_debug_window() override;

 protected:
  void handle_ocean_far(DmaFollower& dma,
                        BaseSharedRenderState* render_state,
                        ScopedProfilerNode& prof);
  void handle_ocean_mid(DmaFollower& dma,
                        BaseSharedRenderState* render_state,
                        ScopedProfilerNode& prof);
};
