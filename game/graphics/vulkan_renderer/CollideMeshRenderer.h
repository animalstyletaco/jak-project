#pragma once
#include "game/graphics/vulkan_renderer/BucketRenderer.h"

class CollideMeshRenderer {
 public:
  CollideMeshRenderer();
  void render(SharedRenderState* render_state, ScopedProfilerNode& prof, UniformBuffer& buffer);
  ~CollideMeshRenderer();

 private:
  void InitializeInputVertexAttribute();    
};
