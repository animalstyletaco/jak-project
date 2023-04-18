#include "Warp.h"

BaseWarp::BaseWarp(const std::string& name, int id) : BaseBucketRenderer(name, id) {}

BaseWarp::~BaseWarp() {}

void BaseWarp::draw_debug_window() {
  generic_draw_debug_window();
}

void BaseWarp::render(DmaFollower& dma, BaseSharedRenderState* render_state, ScopedProfilerNode& prof) {
  generic_render_in_mode(dma, render_state, prof, BaseGeneric2::Mode::NORMAL);
}
