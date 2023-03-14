#include "OceanMidAndFar.h"

#include "third-party/imgui/imgui.h"

BaseOceanMidAndFar::BaseOceanMidAndFar(const std::string& name, int my_id)
    : BaseBucketRenderer(name, my_id) {
}

namespace ocean_mid_and_far {
void advance_and_print_dma(DmaFollower& dma) {
  auto data = dma.read_and_advance();
  printf(
      "dma transfer:\n%ssize: %d\nvif0: %s, data: %d\nvif1: %s, data: %d, imm: "
      "%d\n\n",
      dma.current_tag().print().c_str(), data.size_bytes, data.vifcode0().print().c_str(),
      data.vif0(), data.vifcode1().print().c_str(), data.vifcode1().num, data.vifcode1().immediate);
}

bool is_end_tag(const DmaTag& tag, const VifCode& v0, const VifCode& v1) {
  return tag.qwc == 0 && tag.kind == DmaTag::Kind::NEXT && v0.kind == VifCode::Kind::NOP &&
         v1.kind == VifCode::Kind::NOP;
}
}  // namespace ocean_mid_and_far

void BaseOceanMidAndFar::render(DmaFollower& dma,
                                BaseSharedRenderState* render_state,
                                ScopedProfilerNode& prof) {
  if (render_state->version == GameVersion::Jak1) {
    render_jak1(dma, render_state, prof);
  } else if (render_state->version == GameVersion::Jak2) {
    render_jak2(dma, render_state, prof);
  } else {
    assert(false);
  }
}

void BaseOceanMidAndFar::render_jak1(DmaFollower& dma, BaseSharedRenderState* render_state, ScopedProfilerNode& prof){
  // skip if disabled
  if (!m_enabled) {
    while (dma.current_tag_offset() != render_state->next_bucket) {
      dma.read_and_advance();
    }
    return;
  }

  // jump to bucket
  auto data0 = dma.read_and_advance();
  ASSERT(data0.vif1() == 0);
  ASSERT(data0.vif0() == 0);
  ASSERT(data0.size_bytes == 0);

  // see if bucket is empty or not
  if (dma.current_tag().kind == DmaTag::Kind::CALL) {
    // renderer didn't run, let's just get out of here.
    for (int i = 0; i < 4; i++) {
      dma.read_and_advance();
    }
    ASSERT(dma.current_tag_offset() == render_state->next_bucket);
    return;
  }
  direct_renderer_reset_state();

  {
    auto p = prof.make_scoped_child("texture");
    texture_renderer_handle_ocean_texture_jak1(dma, render_state, p);
  }

  handle_ocean_far(dma, render_state, prof);
  direct_renderer_flush_pending(render_state, prof);

  direct_renderer_set_mipmap(true);
  handle_ocean_mid(dma, render_state, prof);

  auto final_next = dma.read_and_advance();
  ASSERT(final_next.vifcode0().kind == VifCode::Kind::NOP &&
         final_next.vifcode1().kind == VifCode::Kind::NOP && final_next.size_bytes == 0);
  for (int i = 0; i < 4; i++) {
    dma.read_and_advance();
  }
  ASSERT(dma.current_tag_offset() == render_state->next_bucket);

  direct_renderer_flush_pending(render_state, prof);
  direct_renderer_set_mipmap(false);
}

void BaseOceanMidAndFar::render_jak2(DmaFollower& dma,
                                     BaseSharedRenderState* render_state,
                                     ScopedProfilerNode& prof) {  // jump to bucket
  auto data0 = dma.read_and_advance();
  ASSERT(data0.vif1() == 0 || data0.vifcode1().kind == VifCode::Kind::NOP);
  ASSERT(data0.vif0() == 0 || data0.vifcode0().kind == VifCode::Kind::MARK);
  ASSERT(data0.size_bytes == 0);

  // see if bucket is empty or not
  if (dma.current_tag_offset() == render_state->next_bucket) {
    // fmt::print("ocean-mid-far: early exit!\n");
    return;
  }
  direct_renderer_reset_state();

  // TODO handle ocean::89 and ocean::79
  // handle_ocean_89_jak2(dma, render_state, prof);

  {
    auto p = prof.make_scoped_child("texture");
    texture_renderer_handle_ocean_texture_jak2(dma, render_state, p);
  }

  // handle_ocean_79_jak2(dma, render_state, prof);
  handle_ocean_far(dma, render_state, prof);
  direct_renderer_flush_pending(render_state, prof);

  direct_renderer_set_mipmap(true);
  handle_ocean_mid(dma, render_state, prof);

  auto final_next = dma.read_and_advance();
  ASSERT(final_next.vifcode0().kind == VifCode::Kind::NOP &&
         final_next.vifcode1().kind == VifCode::Kind::NOP && final_next.size_bytes == 0);
  for (int i = 0; i < 2; i++) {
    dma.read_and_advance();
  }
  ASSERT(dma.current_tag_offset() == render_state->next_bucket);

  // auto transfers = 0;
  // // print the entire chain
  // printf("START OCEAN MID FAR DMA!!!!!!!\n");
  // while (dma.current_tag_offset() != render_state->next_bucket) {
  //   auto data = dma.read_and_advance();
  //   printf(
  //       "dma transfer %d:\n%ssize: %d\nvif0: %s, data: %d\nvif1: %s, data: %d, imm: "
  //       "%d\n\n",
  //       transfers, dma.current_tag().print().c_str(), data.size_bytes,
  //       data.vifcode0().print().c_str(), data.vif0(), data.vifcode1().print().c_str(),
  //       data.vifcode1().num, data.vifcode1().immediate);
  //   transfers++;
  // }
  // printf("transfers: %d\n\n", transfers);

  direct_renderer_flush_pending(render_state, prof);
  direct_renderer_set_mipmap(false);
}

void BaseOceanMidAndFar::handle_ocean_far(DmaFollower& dma,
                                      BaseSharedRenderState* render_state,
                                      ScopedProfilerNode& prof) {
  auto init_data = dma.read_and_advance();
  ASSERT(init_data.size_bytes == 160);
  u8 init_data_buffer[160];
  memcpy(init_data_buffer, init_data.data, 160);

  // this is a bit of a hack, but it patches the ta0 to 0 in
  // (set! (-> (the-as (pointer gs-texa) s4-0) 8) (new 'static 'gs-texa :ta0 #x80 :ta1 #x80))
  // TODO figure out if we actually have do something here.
  u8 val = 0;
  memcpy(init_data_buffer + 80, &val, 1);
  direct_renderer_render_gif(init_data_buffer, 160, render_state, prof);

  while (dma.current_tag().kind == DmaTag::Kind::CNT &&
         dma.current_tag_vifcode0().kind == VifCode::Kind::NOP) {
    auto data = dma.read_and_advance();
    ASSERT(data.vifcode0().kind == VifCode::Kind::NOP);
    ASSERT(data.vifcode1().kind == VifCode::Kind::DIRECT);
    ASSERT(data.size_bytes / 16 == data.vifcode1().immediate);
    direct_renderer_render_gif(data.data, data.size_bytes, render_state, prof);
  }
}

void BaseOceanMidAndFar::handle_ocean_mid(DmaFollower& dma,
                                      BaseSharedRenderState* render_state,
                                      ScopedProfilerNode& prof) {
  if (dma.current_tag_vifcode0().kind == VifCode::Kind::BASE) {
    ocean_mid_renderer_run(dma, render_state, prof);
  } else {
    // not drawing
    return;
  }

  while (!ocean_mid_and_far::is_end_tag(dma.current_tag(), dma.current_tag_vifcode0(), dma.current_tag_vifcode1())) {
    dma.read_and_advance();
  }
}

void BaseOceanMidAndFar::handle_ocean_89_jak2(DmaFollower& dma,
                                              BaseSharedRenderState* render_state,
                                              ScopedProfilerNode& prof) {}
void BaseOceanMidAndFar::handle_ocean_79_jak2(DmaFollower& dma,
                                              BaseSharedRenderState* render_state,
                                              ScopedProfilerNode& prof) {}
