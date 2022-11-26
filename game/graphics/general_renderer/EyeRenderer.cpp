#include "EyeRenderer.h"

#include "common/util/FileUtil.h"

#include "game/graphics/general_renderer/AdgifHandler.h"

#include "third-party/imgui/imgui.h"

/////////////////////////
// Bucket Renderer
/////////////////////////
BaseEyeRenderer::BaseEyeRenderer(const std::string& name, int id) : BaseBucketRenderer(name, id) {}

BaseEyeRenderer::~BaseEyeRenderer() {

}

void BaseEyeRenderer::render(DmaFollower& dma,
                         BaseSharedRenderState* render_state,
                         ScopedProfilerNode& prof) {
  m_debug.clear();

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

  handle_eye_dma2(dma, render_state, prof);

  while (dma.current_tag_offset() != render_state->next_bucket) {
    auto data = dma.read_and_advance();
    m_debug += fmt::format("dma: {}\n", data.size_bytes);
  }
}

void BaseEyeRenderer::draw_debug_window() {
  ImGui::Checkbox("Use GPU", &m_use_gpu);
  ImGui::Text("Time: %.3f ms\n", m_average_time_ms);
  ImGui::Text("Debug:\n%s", m_debug.c_str());
  if (!m_use_gpu) {
    ImGui::Checkbox("bilinear", &m_use_bilinear);
  }
  ImGui::Checkbox("alpha hack", &m_alpha_hack);
}

//////////////////////
// DMA Decode
//////////////////////

BaseEyeRenderer::ScissorInfo BaseEyeRenderer::decode_scissor(const DmaTransfer& dma) {
  ASSERT(dma.vif0() == 0);
  ASSERT(dma.vifcode1().kind == VifCode::Kind::DIRECT);
  ASSERT(dma.size_bytes == 32);

  GifTag gifTag(dma.data);
  ASSERT(gifTag.nloop() == 1);
  ASSERT(gifTag.eop());
  ASSERT(!gifTag.pre());
  ASSERT(gifTag.flg() == GifTag::Format::PACKED);
  ASSERT(gifTag.nreg() == 1);

  u8 reg_addr;
  memcpy(&reg_addr, dma.data + 24, 1);
  ASSERT((GsRegisterAddress)reg_addr == GsRegisterAddress::SCISSOR_1);
  BaseEyeRenderer::ScissorInfo result;
  u64 val;
  memcpy(&val, dma.data + 16, 8);
  GsScissor reg(val);
  result.x0 = reg.x0();
  result.x1 = reg.x1();
  result.y0 = reg.y0();
  result.y1 = reg.y1();
  return result;
}

BaseEyeRenderer::SpriteInfo BaseEyeRenderer::decode_sprite(const DmaTransfer& dma) {
  /*
   (new 'static 'dma-gif-packet
        :dma-vif (new 'static 'dma-packet
                      :dma (new 'static 'dma-tag :qwc #x6 :id (dma-tag-id cnt))
                      :vif1 (new 'static 'vif-tag :imm #x6 :cmd (vif-cmd direct) :msk #x1)
                      )
        :gif0 (new 'static 'gif-tag64
                   :nloop #x1
                   :eop #x1
                   :pre #x1
                   :prim (new 'static 'gs-prim :prim (gs-prim-type sprite) :tme #x1 :fst #x1)
                   :nreg #x5
                   )
        :gif1 (new 'static 'gif-tag-regs
                   :regs0 (gif-reg-id rgbaq)
                   :regs1 (gif-reg-id uv)
                   :regs2 (gif-reg-id xyz2)
                   :regs3 (gif-reg-id uv)
                   :regs4 (gif-reg-id xyz2)
                   )
        )
   */

  ASSERT(dma.vif0() == 0);
  ASSERT(dma.vifcode1().kind == VifCode::Kind::DIRECT);
  ASSERT(dma.size_bytes == 6 * 16);

  // note: not checking everything here.
  GifTag gifTag(dma.data);
  ASSERT(gifTag.nloop() == 1);
  ASSERT(gifTag.eop());
  ASSERT(gifTag.pre());
  ASSERT(gifTag.flg() == GifTag::Format::PACKED);
  ASSERT(gifTag.nreg() == 5);

  BaseEyeRenderer::SpriteInfo result;

  // rgba
  ASSERT(dma.data[16] == 128);               // r
  ASSERT(dma.data[16 + 4] == 128);           // r
  ASSERT(dma.data[16 + 8] == 128);           // r
  memcpy(&result.a, dma.data + 16 + 12, 1);  // a

  // uv0
  memcpy(&result.uv0[0], &dma.data[32], 8);

  // xyz0
  memcpy(&result.xyz0[0], &dma.data[48], 12);
  result.xyz0[2] >>= 4;

  // uv1
  memcpy(&result.uv1[0], &dma.data[64], 8);

  // xyz1
  memcpy(&result.xyz1[0], &dma.data[80], 12);
  result.xyz1[2] >>= 4;

  return result;
}

void BaseEyeRenderer::handle_eye_dma2(DmaFollower& dma,
                                  BaseSharedRenderState* render_state,
                                  ScopedProfilerNode&) {
  Timer timer;
  m_debug.clear();

  // first should be the gs setup for render to texture
  auto offset_setup = dma.read_and_advance();
  ASSERT(offset_setup.size_bytes == 128);
  ASSERT(offset_setup.vifcode0().kind == VifCode::Kind::FLUSHA);
  ASSERT(offset_setup.vifcode1().kind == VifCode::Kind::DIRECT);

  // next should be alpha setup
  auto alpha_setup = dma.read_and_advance();
  ASSERT(alpha_setup.size_bytes == 32);
  ASSERT(alpha_setup.vifcode0().kind == VifCode::Kind::NOP);
  ASSERT(alpha_setup.vifcode1().kind == VifCode::Kind::DIRECT);

  // from the add to bucket
  ASSERT(dma.current_tag().kind == DmaTag::Kind::NEXT);
  ASSERT(dma.current_tag().qwc == 0);
  ASSERT(dma.current_tag_vif0() == 0);
  ASSERT(dma.current_tag_vif1() == 0);
  dma.read_and_advance();

  run_dma_draws_in_gpu(dma, render_state);

  float time_ms = timer.getMs();
  m_average_time_ms = m_average_time_ms * 0.95 + time_ms * 0.05;
}

//////////////////////
// CPU Drawing
//////////////////////

u32 BaseEyeRenderer::bilinear_sample_eye(const u8* tex, float tx, float ty, int texw) {
  int tx0 = tx;
  int ty0 = ty;
  int tx1 = tx0 + 1;
  int ty1 = ty0 + 1;
  tx1 = std::min(tx1, texw - 1);
  ty1 = std::min(ty1, texw - 1);

  u8 tex0[4];
  u8 tex1[4];
  u8 tex2[4];
  u8 tex3[4];
  memcpy(tex0, tex + (4 * (tx0 + ty0 * texw)), 4);
  memcpy(tex1, tex + (4 * (tx1 + ty0 * texw)), 4);
  memcpy(tex2, tex + (4 * (tx0 + ty1 * texw)), 4);
  memcpy(tex3, tex + (4 * (tx1 + ty1 * texw)), 4);

  u8 result[4] = {0, 0, 0, 0};
  float x0w = float(tx1) - tx;
  float y0w = float(ty1) - ty;
  float weights[4] = {x0w * y0w, (1.f - x0w) * y0w, x0w * (1.f - y0w), (1.f - x0w) * (1.f - y0w)};

  for (int i = 0; i < 4; i++) {
    float total = 0;
    total += weights[0] * tex0[i];
    total += weights[1] * tex1[i];
    total += weights[2] * tex2[i];
    total += weights[3] * tex3[i];
    result[i] = total;
  }

  // clamp
  u32 tex_out;
  memcpy(&tex_out, result, 4);
  return tex_out;
}

int BaseEyeRenderer::add_draw_to_buffer(int idx, const BaseEyeRenderer::EyeDraw& draw, float* data, int pair, int lr) {
  int x_off = lr * SINGLE_EYE_SIZE * 16;
  int y_off = pair * SINGLE_EYE_SIZE * 16;
  data[idx++] = draw.sprite.xyz0[0] - x_off;
  data[idx++] = draw.sprite.xyz0[1] - y_off;
  data[idx++] = 0;
  data[idx++] = 0;

  data[idx++] = draw.sprite.xyz1[0] - x_off;
  data[idx++] = draw.sprite.xyz0[1] - y_off;
  data[idx++] = 1;
  data[idx++] = 0;

  data[idx++] = draw.sprite.xyz0[0] - x_off;
  data[idx++] = draw.sprite.xyz1[1] - y_off;
  data[idx++] = 0;
  data[idx++] = 1;

  data[idx++] = draw.sprite.xyz1[0] - x_off;
  data[idx++] = draw.sprite.xyz1[1] - y_off;
  data[idx++] = 1;
  data[idx++] = 1;
  return idx;
}

//////////////////////
// DMA Decode
//////////////////////

std::string BaseEyeRenderer::SpriteInfo::print() const {
  std::string result;
  result +=
      fmt::format("a: {:x} uv: ({}, {}), ({}, {}) xyz: ({}, {}, {}), ({}, {}, {})", a, uv0[0],
                  uv0[1], uv1[0], uv1[1], xyz0[0], xyz0[1], xyz0[2], xyz1[0], xyz1[1], xyz1[2]);
  return result;
}

std::string BaseEyeRenderer::ScissorInfo::print() const {
  return fmt::format("x : [{}, {}], y : [{}, {}]", x0, x1, y0, y1);
}

std::string BaseEyeRenderer::EyeDraw::print() const {
  return fmt::format("{}\n{}\n", sprite.print(), scissor.print());
}
