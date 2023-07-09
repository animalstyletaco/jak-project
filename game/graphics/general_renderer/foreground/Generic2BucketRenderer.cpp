#include "Generic2BucketRenderer.h"

BaseGeneric2BucketRenderer::BaseGeneric2BucketRenderer(const std::string& name,
                                               int id, BaseGeneric2::Mode mode)
    : BaseBucketRenderer(name, id), m_mode(mode) {}

void BaseGeneric2BucketRenderer::render(DmaFollower& dma,
                                    BaseSharedRenderState* render_state,
                                    ScopedProfilerNode& prof) {
  // if the user has asked to disable the renderer, just advance the dma follower to the next
  // bucket and return immediately.
  if (!m_enabled) {
    while (dma.current_tag_offset() != render_state->next_bucket) {
      dma.read_and_advance();
    }
    return;
  }
  generic_render(dma, render_state, prof, m_mode);
}
