#pragma once

#include <string>

#include "game/graphics/general_renderer/BucketRenderer.h"

constexpr int EYE_BASE_BLOCK = 8160;
constexpr int NUM_EYE_PAIRS = 20;
constexpr int SINGLE_EYE_SIZE = 32;

class BaseEyeRenderer : public BaseBucketRenderer {
 public:
  BaseEyeRenderer(const std::string& name, int id);
  ~BaseEyeRenderer();
  void render(DmaFollower& dma, BaseSharedRenderState* render_state, ScopedProfilerNode& prof) override;
  void draw_debug_window() override;

  void handle_eye_dma2(DmaFollower& dma, BaseSharedRenderState* render_state, ScopedProfilerNode& prof);

  struct SpriteInfo {
    u8 a;
    u32 uv0[2];
    u32 uv1[2];
    u32 xyz0[3];
    u32 xyz1[3];

    std::string print() const;
  };

  struct ScissorInfo {
    int x0, x1;
    int y0, y1;
    std::string print() const;
  };

  struct EyeDraw {
    SpriteInfo sprite;
    ScissorInfo scissor;
    std::string print() const;
  };

 protected:
  ScissorInfo decode_scissor(const DmaTransfer& dma);
  SpriteInfo decode_sprite(const DmaTransfer& dma);

  EyeDraw read_eye_draw(DmaFollower& dma) {
    auto scissor = decode_scissor(dma.read_and_advance());
    auto sprite = decode_sprite(dma.read_and_advance());
    return {sprite, scissor};
  }
  int add_draw_to_buffer_32(int idx,
                            const BaseEyeRenderer::EyeDraw& draw,
                            float* data,
                            int pair,
                            int lr);
  int add_draw_to_buffer_64(int idx,
                         const BaseEyeRenderer::EyeDraw& draw,
                         float* data,
                         int pair,
                         int lr);

  virtual void run_dma_draws_in_gpu(DmaFollower& dma, BaseSharedRenderState* render_state) = 0;

  std::string m_debug;
  float m_average_time_ms = 0;

  bool m_use_bilinear = true;
  bool m_alpha_hack = true;

  u32 m_temp_tex[SINGLE_EYE_SIZE * SINGLE_EYE_SIZE];

  bool m_use_gpu = true;

  struct CpuEyeTex {
    u64 gl_tex;
    GpuTexture* gpu_tex;
    u32 tbp;
  };
  CpuEyeTex m_cpu_eye_textures[NUM_EYE_PAIRS * 2];

  // xyst per vertex, 4 vertices per square, 3 draws per eye, 11 pairs of eyes, 2 eyes per pair.
  static constexpr int VTX_BUFFER_FLOATS = 4 * 4 * 3 * NUM_EYE_PAIRS * 2;
  float m_gpu_vertex_buffer[VTX_BUFFER_FLOATS];

  struct SingleEyeDraws {
    int lr;
    int pair;
    bool using_64 = false;

    int tex_slot() const { return pair * 2 + lr; }
    u32 clear_color;
    EyeDraw iris;

    EyeDraw pupil;

    EyeDraw lid;
  };
};
