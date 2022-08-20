#pragma once

#include "common/common_types.h"
#include "common/custom_data/Tfrag3Data.h"
#include "common/util/Timer.h"

#include "game/graphics/texture/TexturePool.h"

struct LevelData {
  std::unique_ptr<tfrag3::Level> level;
  std::vector<unsigned> textures;
  u64 load_id = UINT64_MAX;

  struct TieVulkan {
    unsigned vertex_buffer;
    bool has_wind = false;
    unsigned wind_indices;
  };
  std::array<std::vector<TieVulkan>, tfrag3::TIE_GEOS> tie_data;
  std::array<std::vector<unsigned>, tfrag3::TIE_GEOS> tfrag_vertex_data;
  std::vector<unsigned> shrub_vertex_data;
  unsigned collide_vertices;

  unsigned merc_vertices;
  unsigned merc_indices;
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
  LoaderStage(const std::string& name) : m_name(name) {}
  virtual bool run(Timer& timer, LoaderInput& data) = 0;
  virtual void reset() = 0;
  virtual ~LoaderStage() = default;
  const std::string& name() const { return m_name; }

 protected:
  std::string m_name;
};
