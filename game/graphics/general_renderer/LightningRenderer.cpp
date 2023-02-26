#include "LightningRenderer.h"

BaseLightningRenderer::BaseLightningRenderer(const std::string& name, int id)
    : BaseBucketRenderer(name, id) {}

BaseLightningRenderer::~BaseLightningRenderer() {}

void BaseLightningRenderer::draw_debug_window() {
  generic_draw_debug_window();
}

void BaseLightningRenderer::render(DmaFollower& dma,
                                   BaseSharedRenderState* render_state,
                                   ScopedProfilerNode& prof) {
  generic_render_in_mode(dma, render_state, prof, BaseGeneric2::Mode::LIGHTNING);
}
