#include "LightningRenderer.h"

BaseLightningRenderer::BaseLightningRenderer(const std::string& name, int id)
    : BaseBucketRenderer(name, id), m_generic(name, id) {}

BaseLightningRenderer::~BaseLightningRenderer() {}

void BaseLightningRenderer::draw_debug_window() {
  generic_draw_debug_window();
}

void BaseLightningRenderer::render(DmaFollower& dma,
                                   SharedRenderState* render_state,
                                   ScopedProfilerNode& prof) {
  m_generic.render_in_mode(dma, render_state, prof, Generic2::Mode::LIGHTNING);
}

void BaseLightningRenderer::init_shaders(ShaderLibrary& shaders) {
  generic_init_shaders(shaders);
}
