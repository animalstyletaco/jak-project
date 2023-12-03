#include "BucketRenderer.h"

#include "third-party/fmt/core.h"
#include "third-party/imgui/imgui.h"

std::string BaseBucketRenderer::name_and_id() const {
  return fmt::format("[{:2d}] {}", (int)m_my_id, m_name);
}

BaseEmptyBucketRenderer::BaseEmptyBucketRenderer(const std::string& name, int my_id)
    : BaseBucketRenderer(name, my_id) {}

void BaseEmptyBucketRenderer::render(DmaFollower& dma,
                                     BaseSharedRenderState* render_state,
                                     ScopedProfilerNode& /*prof*/) {
  if (render_state->GetVersion() == GameVersion::Jak1) {
    // an empty bucket should have 4 things:
    // a NEXT in the bucket buffer
    // a CALL that calls the default register buffer chain
    // a CNT then RET to get out of the default register buffer chain
    // a NEXT to get to the next bucket.

    // NEXT
    auto first_tag = dma.current_tag();
    dma.read_and_advance();
    ASSERT(first_tag.kind == DmaTag::Kind::NEXT && first_tag.qwc == 0);

    // CALL
    auto call_tag = dma.current_tag();
    dma.read_and_advance();
    ASSERT_MSG(call_tag.kind == DmaTag::Kind::CALL && call_tag.qwc == 0,
               fmt::format("Bucket renderer {} ({}) was supposed to be empty, but wasn't\n",
                           m_my_id, m_name));

    // in the default reg buffer:
    ASSERT(dma.current_tag_offset() == render_state->default_regs_buffer);
    dma.read_and_advance();
    ASSERT(dma.current_tag().kind == DmaTag::Kind::RET);
    dma.read_and_advance();

    // NEXT to next buffer
    auto to_next_buffer = dma.current_tag();
    ASSERT(to_next_buffer.kind == DmaTag::Kind::NEXT);
    ASSERT(to_next_buffer.qwc == 0);
    dma.read_and_advance();

    // and we should now be in the next bucket!
    ASSERT(dma.current_tag_offset() == render_state->next_bucket);
  } else {
    auto first_tag = dma.current_tag();
    dma.read_and_advance();
    if (first_tag.kind != DmaTag::Kind::CNT || first_tag.qwc != 0) {
      fmt::print("Bucket renderer {} ({}) was supposed to be empty, but wasn't\n", m_my_id, m_name);
      ASSERT(false);
    }
  }
}

BaseSkipRenderer::BaseSkipRenderer(const std::string& name, int my_id)
    : BaseBucketRenderer(name, my_id) {}

void BaseSkipRenderer::render(DmaFollower& dma,
                              BaseSharedRenderState* render_state,
                              ScopedProfilerNode& /*prof*/) {
  while (dma.current_tag_offset() != render_state->next_bucket) {
    dma.read_and_advance();
  }
}

void BaseSharedRenderState::reset() {
  has_pc_data = false;
  for (auto& x : occlusion_vis) {
    x.valid = false;
  }
  load_status_debug.clear();
}

BaseRenderMux::BaseRenderMux(const std::string& name, int my_id) : BaseBucketRenderer(name, my_id) {
  for (auto& n : m_name_strs) {
    m_name_str_ptrs.push_back(n.data());
  }
}

void BaseRenderMux::draw_debug_window() {
  ImGui::ListBox("Pick", &m_render_idx, m_name_str_ptrs.data(), m_name_strs.size());
  ImGui::Separator();
}
