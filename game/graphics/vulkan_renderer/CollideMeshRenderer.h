#pragma once
#include "game/graphics/vulkan_renderer/BucketRenderer.h"

class CollideMeshRenderer {
 public:
  CollideMeshRenderer();
  void render(SharedRenderState* render_state, ScopedProfilerNode& prof);
  ~CollideMeshRenderer();

 private:
  GLuint m_vao;
};
