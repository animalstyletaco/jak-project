#include "Shrub.h"

#include "common/log/log.h"

BaseShrub::BaseShrub(const std::string& name, int my_id) : BaseBucketRenderer(name, my_id) {
  m_color_result.resize(background_common::TIME_OF_DAY_COLOR_COUNT);
}

BaseShrub::~BaseShrub() {}

void BaseShrub::render(DmaFollower& dma,
                       BaseSharedRenderState* render_state,
                       ScopedProfilerNode& prof) {
  if (!m_enabled) {
    while (dma.current_tag_offset() != render_state->next_bucket) {
      dma.read_and_advance();
    }
    return;
  }

  auto data0 = dma.read_and_advance();
  ASSERT(data0.vif1() == 0 || data0.vifcode1().kind == VifCode::Kind::NOP);
  ASSERT(data0.vif0() == 0 || data0.vifcode0().kind == VifCode::Kind::NOP ||
         data0.vifcode0().kind == VifCode::Kind::MARK);
  ASSERT(data0.size_bytes == 0);

  if (dma.current_tag().kind == DmaTag::Kind::CALL) {
    // renderer didn't run, let's just get out of here.
    for (int i = 0; i < 4; i++) {
      dma.read_and_advance();
    }
    ASSERT(dma.current_tag_offset() == render_state->next_bucket);
    return;
  }
  if (dma.current_tag_offset() == render_state->next_bucket) {
    return;
  }

  auto pc_port_data = dma.read_and_advance();
  ASSERT(pc_port_data.size_bytes == sizeof(TfragPcPortData));
  memcpy(&m_pc_port_data, pc_port_data.data, sizeof(TfragPcPortData));
  m_pc_port_data.level_name[11] = '\0';

  while (dma.current_tag_offset() != render_state->next_bucket) {
    dma.read_and_advance();
  }

  TfragRenderSettings settings;
  settings.camera.hvdf_off = m_pc_port_data.camera.hvdf_off;
  settings.camera.fog = m_pc_port_data.camera.fog;

  settings.camera = m_pc_port_data.camera;
  settings.tree_idx = 0;

  for (int i = 0; i < 4; i++) {
    settings.camera.itimes[i] = m_pc_port_data.camera.itimes[i];
  }

  background_common::update_render_state_from_pc_settings(render_state, m_pc_port_data);

  for (int i = 0; i < 4; i++) {
    settings.camera.planes[i] = m_pc_port_data.camera.planes[i];
  }

  m_has_level = setup_for_level(m_pc_port_data.level_name, render_state);
  render_all_trees(settings, render_state, prof);
}

void BaseShrub::draw_debug_window() {}
