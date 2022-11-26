#include "SkyBlendGPU.h"

#include "common/log/log.h"

#include "game/graphics/general_renderer/AdgifHandler.h"

BaseSkyBlendGPU::BaseSkyBlendGPU() {
  // we only draw squares
  m_vertex_data[0].x = 0;
  m_vertex_data[0].y = 0;

  m_vertex_data[1].x = 1;
  m_vertex_data[1].y = 0;

  m_vertex_data[2].x = 0;
  m_vertex_data[2].y = 1;

  m_vertex_data[3].x = 1;
  m_vertex_data[3].y = 0;

  m_vertex_data[4].x = 0;
  m_vertex_data[4].y = 1;

  m_vertex_data[5].x = 1;
  m_vertex_data[5].y = 1;
}

BaseSkyBlendGPU::~BaseSkyBlendGPU() {
}


