#include "Merc2BucketRenderer.h"

BaseMerc2BucketRenderer::BaseMerc2BucketRenderer(const std::string& name,
                                         int my_id)
    : BaseBucketRenderer(name, my_id) {}

void BaseMerc2BucketRenderer::render(DmaFollower& dma,
                                 BaseSharedRenderState* render_state,
                                 ScopedProfilerNode& prof) {
  // skip if disabled
  if (!m_enabled) {
    while (dma.current_tag_offset() != render_state->next_bucket) {
      dma.read_and_advance();
    }
    return;
  }

  merc2_render(dma, render_state, prof, &m_debug_stats);
}
