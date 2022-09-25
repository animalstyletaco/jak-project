#pragma once

#include "common/common_types.h"
#include "common/custom_data/Tfrag3Data.h"
#include "common/util/Timer.h"

#include "game/graphics/vulkan_renderer/TexturePoolVulkan.h"

struct LevelData {
  std::unique_ptr<tfrag3::Level> level;
  std::vector<TextureInfo> textures;
  u64 load_id = UINT64_MAX;

  struct TieVulkan {
    std::unique_ptr<VertexBuffer> vertex_buffer;
    bool has_wind = false;
    std::unique_ptr<IndexBuffer> wind_indices;
  };
  std::array<std::vector<TieVulkan>, tfrag3::TIE_GEOS> tie_data;
  std::array<std::vector<std::unique_ptr<VertexBuffer>>, tfrag3::TIE_GEOS> tfrag_vertex_data;
  std::vector<std::unique_ptr<VertexBuffer>> shrub_vertex_data;
  std::unique_ptr<VertexBuffer> collide_vertices;

  std::unique_ptr<VertexBuffer> merc_vertices;
  std::unique_ptr<IndexBuffer> merc_indices;
  std::unordered_map<std::string, const tfrag3::MercModel*> merc_model_lookup;

  int frames_since_last_used = 0;
};

struct MercRef {
  const tfrag3::MercModel* model = nullptr;
  u64 load_id = 0;
  const LevelData* level = nullptr;
  bool operator==(const MercRef& other) const {
    return model == other.model && load_id == other.load_id;
  }
};

struct LoaderInput {
  LevelData* lev_data;
  TexturePool* tex_pool;
  std::unordered_map<std::string, std::vector<MercRef>>* mercs;
};

class LoaderStage {
 public:
  LoaderStage(std::unique_ptr<GraphicsDeviceVulkan>& device, const std::string& name) : m_device(device), m_name(name) {}
  virtual bool run(Timer& timer, LoaderInput& data) = 0;
  virtual void reset() = 0;
  virtual ~LoaderStage() = default;
  const std::string& name() const { return m_name; }

 protected:
  std::unique_ptr<GraphicsDeviceVulkan>& m_device;
  std::string m_name;
};
