#pragma once

#include "common/common_types.h"
#include "common/custom_data/Tfrag3Data.h"
#include "common/util/Timer.h"

struct BaseLevelData {
  std::unique_ptr<tfrag3::Level> level;
  u64 load_id = UINT64_MAX;

  std::unordered_map<std::string, const tfrag3::MercModel*> merc_model_lookup;

  int frames_since_last_used = 0;
};

struct BaseMercRef {
  BaseMercRef(const tfrag3::MercModel* model) : model(model) {}

  const tfrag3::MercModel* model = nullptr;
  u64 load_id = 0;
  bool operator==(const BaseMercRef& other) const {
    return model == other.model && load_id == other.load_id;
  }
};

class BaseLoaderStage {
 public:
  BaseLoaderStage(const std::string& name) : m_name(name) {}
  virtual void reset() = 0;
  virtual ~BaseLoaderStage() = default;
  const std::string& name() const { return m_name; }

 protected:
  std::string m_name;
};
