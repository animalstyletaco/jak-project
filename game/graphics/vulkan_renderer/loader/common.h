#pragma once

#include "common/common_types.h"
#include "common/custom_data/Tfrag3Data.h"
#include "common/util/Timer.h"

#include "game/graphics/general_renderer/loader/Loader.h"
#include "game/graphics/texture/TexturePoolVulkan.h"

struct LevelDataVulkan : BaseLevelData {
  std::vector<VulkanTexture> textures;

  struct TieVulkan {
    std::unique_ptr<VertexBuffer> vertex_buffer;
    std::unique_ptr<IndexBuffer> index_buffer;
    bool has_wind = false;
    std::unique_ptr<IndexBuffer> wind_indices;
  };
  std::array<std::vector<TieVulkan>, tfrag3::TIE_GEOS> tie_data;
  std::array<std::vector<VertexBuffer>, tfrag3::TIE_GEOS> tfrag_vertex_data;
  std::vector<VertexBuffer> shrub_vertex_data;
  std::unique_ptr<VertexBuffer> collide_vertices;

  std::unique_ptr<VertexBuffer> merc_vertices;
  std::unique_ptr<IndexBuffer> merc_indices;
};

struct MercRefVulkan : BaseMercRef {
  MercRefVulkan() = default;
  MercRefVulkan(const tfrag3::MercModel* model, u64 load_id, const LevelDataVulkan* level)
      : model(model), load_id(load_id), level(level) {
  }

  const tfrag3::MercModel* model = nullptr;
  u64 load_id = 0;
  const LevelDataVulkan* level = nullptr;
};

struct LoaderInputVulkan {
  LevelDataVulkan* lev_data;
  TexturePoolVulkan* tex_pool;
  std::unordered_map<std::string, std::vector<MercRefVulkan>>* mercs;
};

class LoaderStageVulkan : public BaseLoaderStage {
 public:
  LoaderStageVulkan(std::unique_ptr<GraphicsDeviceVulkan>& device, const std::string& name) : BaseLoaderStage(name), m_device(device) {}
  virtual bool run(Timer& timer, LoaderInputVulkan& data) = 0;
  virtual ~LoaderStageVulkan() = default;

 protected:
  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
};
