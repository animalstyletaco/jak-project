#include "Tfrag3.h"

#include "third-party/imgui/imgui.h"
#include "game/graphics/vulkan_renderer/vulkan_utils.h"

Tfrag3::Tfrag3() {
  InitializeDebugInputVertexAttribute();
  InitializeInputVertexAttribute();

  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.anisotropyEnable = VK_TRUE;
  // samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  samplerInfo.minLod = 0.0f;
  // samplerInfo.maxLod = static_cast<float>(mipLevels);
  samplerInfo.mipLodBias = 0.0f;

  samplerInfo.minFilter = VK_FILTER_NEAREST;
  samplerInfo.magFilter = VK_FILTER_NEAREST;

  // regardless of how many we use some fixed max
  // we won't actually interp or upload to gpu the unused ones, but we need a fixed maximum so
  // indexing works properly.
  m_color_result.resize(TIME_OF_DAY_COLOR_COUNT);
}

Tfrag3::~Tfrag3() {
  discard_tree_cache();
}

void Tfrag3::update_load(const std::vector<tfrag3::TFragmentTreeKind>& tree_kinds,
                         const LevelData* loader_data) {
  const auto* lev_data = loader_data->level.get();
  discard_tree_cache();
  for (int geom = 0; geom < GEOM_MAX; ++geom) {
    m_cached_trees[geom].clear();
  }

  size_t time_of_day_count = 0;
  size_t vis_temp_len = 0;
  size_t max_draws = 0;
  size_t max_num_grps = 0;
  size_t max_inds = 0;

  std::vector<VkImage> textures[GEOM_MAX];
  std::vector<VkDeviceMemory> texture_memories[GEOM_MAX];

  for (int geom = 0; geom < GEOM_MAX; ++geom) {
    for (size_t tree_idx = 0; tree_idx < lev_data->tfrag_trees[geom].size(); tree_idx++) {
      const auto& tree = lev_data->tfrag_trees[geom][tree_idx];

      if (std::find(tree_kinds.begin(), tree_kinds.end(), tree.kind) != tree_kinds.end()) {
        auto& tree_cache = m_cached_trees[geom].emplace_back();
        tree_cache.kind = tree.kind;
        max_draws = std::max(tree.draws.size(), max_draws);
        size_t num_grps = 0;
        for (auto& draw : tree.draws) {
          num_grps += draw.vis_groups.size();
        }
        max_num_grps = std::max(max_num_grps, num_grps);
        max_inds = std::max(tree.unpacked.indices.size(), max_inds);
        time_of_day_count = std::max(tree.colors.size(), time_of_day_count);
        u32 verts = tree.packed_vertices.vertices.size();

        tree_cache.vertex_buffer = loader_data->tfrag_vertex_data[geom][tree_idx].get();
        tree_cache.vert_count = verts;
        tree_cache.draws = &tree.draws;  // todo - should we just copy this?
        tree_cache.colors = &tree.colors;
        tree_cache.vis = &tree.bvh;
        tree_cache.index_data = tree.unpacked.indices.data();
        tree_cache.tod_cache = swizzle_time_of_day(tree.colors);
        tree_cache.draw_mode = tree.use_strips ? GL_TRIANGLE_STRIP : GL_TRIANGLES;
        vis_temp_len = std::max(vis_temp_len, tree.bvh.vis_nodes.size());

        //CreateIndexBuffer(tree.unpacked.indices);
        //CreateImage(0, 0, VK_IMAGE_TYPE_1D, 1, VK_SAMPLE_COUNT_1_BIT,
        //            VK_FORMAT_A8B8G8R8_SINT_PACK32, VK_IMAGE_TILING_OPTIMAL,
        //            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        //            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textures[geom][tree_idx], texture_memories[geom][tree_idx]);
      }
    }
  }

  m_cache.vis_temp.resize(vis_temp_len);
  m_cache.multidraw_offset_per_stripdraw.resize(max_draws);
  m_cache.multidraw_count_buffer.resize(max_num_grps);
  m_cache.multidraw_index_offset_buffer.resize(max_num_grps);
  m_cache.draw_idx_temp.resize(max_draws);
  m_cache.index_temp.resize(max_inds);
  ASSERT(time_of_day_count <= TIME_OF_DAY_COLOR_COUNT);
}


bool Tfrag3::setup_for_level(const std::vector<tfrag3::TFragmentTreeKind>& tree_kinds,
                             const std::string& level,
                             SharedRenderState* render_state) {
  // make sure we have the level data.
  Timer tfrag3_setup_timer;
  auto lev_data = render_state->loader->get_tfrag3_level(level);
  if (!lev_data || (m_has_level && lev_data->load_id != m_load_id)) {
    m_has_level = false;
    m_textures = nullptr;
    m_level_name = "";
    discard_tree_cache();
    return false;
  }
  m_load_id = lev_data->load_id;

  if (m_level_name != level) {
    update_load(tree_kinds, lev_data);
    m_has_level = true;
    m_textures = &lev_data->textures;
    m_level_name = level;
  } else {
    m_has_level = true;
  }

  if (tfrag3_setup_timer.getMs() > 5) {
    fmt::print("TFRAG setup: {:.1f}ms\n", tfrag3_setup_timer.getMs());
  }

  return m_has_level;
}

void Tfrag3::render_tree(int geom,
                         const TfragRenderSettings& settings,
                         SharedRenderState* render_state,
                         ScopedProfilerNode& prof,
                         std::unique_ptr<UniformBuffer>& uniform_buffer) {
  if (!m_has_level) {
    return;
  }
  auto& tree = m_cached_trees.at(geom).at(settings.tree_idx);
  ASSERT(tree.kind != tfrag3::TFragmentTreeKind::INVALID);

  if (m_color_result.size() < tree.colors->size()) {
    m_color_result.resize(tree.colors->size());
  }
  if (m_use_fast_time_of_day) {
    interp_time_of_day_fast(settings.time_of_day_weights, tree.tod_cache, m_color_result.data());
  } else {
    interp_time_of_day_slow(settings.time_of_day_weights, *tree.colors, m_color_result.data());
  }

  TextureInfo timeOfDayTexture { uniform_buffer->getDevice() };
  VkDeviceSize size = m_color_result.size() * sizeof(m_color_result[0]);

  VkExtent3D extents{tree.colors->size(), 1, 1};
  timeOfDayTexture.CreateImage(extents, 1, VK_IMAGE_TYPE_1D, VK_SAMPLE_COUNT_1_BIT,
                               VK_FORMAT_A8B8G8R8_SINT_PACK32, VK_IMAGE_TILING_OPTIMAL,
                               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  timeOfDayTexture.CreateImageView(VK_IMAGE_VIEW_TYPE_1D, VK_FORMAT_A8B8G8R8_SINT_PACK32,
                                   VK_IMAGE_ASPECT_COLOR_BIT, 1);

  timeOfDayTexture.map();
  timeOfDayTexture.writeToBuffer(m_color_result.data());
  timeOfDayTexture.unmap();

  first_tfrag_draw_setup(settings, render_state, timeOfDayTexture, uniform_buffer);

  cull_check_all_slow(settings.planes, tree.vis->vis_nodes, settings.occlusion_culling,
                      m_cache.vis_temp.data());

  u32 total_tris;
  if (render_state->no_multidraw) {
    u32 idx_buffer_size = make_index_list_from_vis_string(
        m_cache.draw_idx_temp.data(), m_cache.index_temp.data(), *tree.draws, m_cache.vis_temp,
        tree.index_data, &total_tris);
    //CreateIndexBuffer(m_cache.index_temp);
    //glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx_buffer_size * sizeof(u32), m_cache.index_temp.data(),
    //             GL_STREAM_DRAW);
  } else {
    total_tris = make_multidraws_from_vis_string(
        m_cache.multidraw_offset_per_stripdraw.data(), m_cache.multidraw_count_buffer.data(),
        m_cache.multidraw_index_offset_buffer.data(), *tree.draws, m_cache.vis_temp);
  }

  prof.add_tri(total_tris);

  for (size_t draw_idx = 0; draw_idx < tree.draws->size(); draw_idx++) {
    const auto& draw = tree.draws->operator[](draw_idx);
    const auto& multidraw_indices = m_cache.multidraw_offset_per_stripdraw[draw_idx];
    const auto& singledraw_indices = m_cache.draw_idx_temp[draw_idx];

    if (render_state->no_multidraw) {
      if (singledraw_indices.second == 0) {
        continue;
      }
    } else {
      if (multidraw_indices.second == 0) {
        continue;
      }
    }

    ASSERT(m_textures);
    auto double_draw = setup_tfrag_shader(render_state, draw.mode,
                                          m_textures->at(draw.tree_tex_id), uniform_buffer);
    tree.tris_this_frame += draw.num_triangles;
    tree.draws_this_frame++;

    prof.add_draw_call();
    if (render_state->no_multidraw) {
      //glDrawElements(tree.draw_mode, singledraw_indices.second, GL_UNSIGNED_INT,
      //               (void*)(singledraw_indices.first * sizeof(u32)));
    } else {
      //glMultiDrawElements(tree.draw_mode, &m_cache.multidraw_count_buffer[multidraw_indices.first],
      //                    GL_UNSIGNED_INT,
      //                    &m_cache.multidraw_index_offset_buffer[multidraw_indices.first],
      //                    multidraw_indices.second);
    }

    switch (double_draw.kind) {
      case DoubleDrawKind::NONE:
        break;
      case DoubleDrawKind::AFAIL_NO_DEPTH_WRITE:
        prof.add_draw_call();
        uniform_buffer->SetUniform1f("alpha_min", -10.f);
        uniform_buffer->SetUniform1f("alpha_max", double_draw.aref_second);
        //glDepthMask(GL_FALSE);
        if (render_state->no_multidraw) {
          //glDrawElements(tree.draw_mode, singledraw_indices.second, GL_UNSIGNED_INT,
          //               (void*)(singledraw_indices.first * sizeof(u32)));
        } else {
          //glMultiDrawElements(
          //    tree.draw_mode, &m_cache.multidraw_count_buffer[multidraw_indices.first],
          //    GL_UNSIGNED_INT, &m_cache.multidraw_index_offset_buffer[multidraw_indices.first],
          //    multidraw_indices.second);
        }
        break;
      default:
        ASSERT(false);
    }
  }
}

/*!
 * Render all trees with settings for the given tree.
 * This is intended to be used only for debugging when we can't easily get commands for all trees
 * working.
 */
void Tfrag3::render_all_trees(int geom,
                              const TfragRenderSettings& settings,
                              SharedRenderState* render_state,
                              ScopedProfilerNode& prof,
                              std::unique_ptr<UniformBuffer>& uniform_buffer) {
  TfragRenderSettings settings_copy = settings;
  for (size_t i = 0; i < m_cached_trees[geom].size(); i++) {
    if (m_cached_trees[geom][i].kind != tfrag3::TFragmentTreeKind::INVALID) {
      settings_copy.tree_idx = i;
      render_tree(geom, settings_copy, render_state, prof, uniform_buffer);
    }
  }
}

void Tfrag3::render_matching_trees(int geom,
                                   const std::vector<tfrag3::TFragmentTreeKind>& trees,
                                   const TfragRenderSettings& settings,
                                   SharedRenderState* render_state,
                                   ScopedProfilerNode& prof,
                                   std::unique_ptr<UniformBuffer>& uniform_buffer) {
  TfragRenderSettings settings_copy = settings;
  for (size_t i = 0; i < m_cached_trees[geom].size(); i++) {
    auto& tree = m_cached_trees[geom][i];
    tree.reset_stats();
    if (!tree.allowed) {
      continue;
    }
    if (std::find(trees.begin(), trees.end(), tree.kind) != trees.end() || tree.forced) {
      tree.rendered_this_frame = true;
      settings_copy.tree_idx = i;
      render_tree(geom, settings_copy, render_state, prof, uniform_buffer);
      if (tree.cull_debug) {
        render_tree_cull_debug(settings_copy, render_state, prof, uniform_buffer);
      }
    }
  }
}

void Tfrag3::draw_debug_window() {
  for (int i = 0; i < (int)m_cached_trees.at(lod()).size(); i++) {
    auto& tree = m_cached_trees.at(lod()).at(i);
    if (tree.kind == tfrag3::TFragmentTreeKind::INVALID) {
      continue;
    }
    ImGui::PushID(i);
    ImGui::Text("[%d] %10s", i, tfrag3::tfrag_tree_names[(int)m_cached_trees[lod()][i].kind]);
    ImGui::SameLine();
    ImGui::Checkbox("Allow?", &tree.allowed);
    ImGui::SameLine();
    ImGui::Checkbox("Force?", &tree.forced);
    ImGui::SameLine();
    ImGui::Checkbox("cull debug (slow)", &tree.cull_debug);
    ImGui::PopID();
    if (tree.rendered_this_frame) {
      ImGui::Text("  tris: %d draws: %d", tree.tris_this_frame, tree.draws_this_frame);
    }
  }
}

void Tfrag3::discard_tree_cache() {
  m_textures = nullptr;
  for (int geom = 0; geom < GEOM_MAX; ++geom) {
    for (auto& tree : m_cached_trees[geom]) {
      if (tree.kind != tfrag3::TFragmentTreeKind::INVALID) {
        //glDeleteTextures(1, &tree.time_of_day_texture);
      }
    }
    m_cached_trees[geom].clear();
  }
}

namespace {

float frac(float in) {
  return in - (int)in;
}

void debug_vis_draw(int first_root,
                    int tree,
                    int num,
                    int depth,
                    const std::vector<tfrag3::VisNode>& nodes,
                    std::vector<Tfrag3::DebugVertex>& verts_out) {
  for (int ki = 0; ki < num; ki++) {
    auto& node = nodes.at(ki + tree - first_root);
    ASSERT(node.child_id != 0xffff);
    math::Vector4f rgba{frac(0.4 * depth), frac(0.7 * depth), frac(0.2 * depth), 0.06};
    math::Vector3f center = node.bsphere.xyz();
    float rad = node.bsphere.w();
    math::Vector3f corners[8] = {center, center, center, center};
    corners[0].x() += rad;
    corners[1].x() += rad;
    corners[2].x() -= rad;
    corners[3].x() -= rad;

    corners[0].y() += rad;
    corners[1].y() -= rad;
    corners[2].y() += rad;
    corners[3].y() -= rad;

    for (int i = 0; i < 4; i++) {
      corners[i + 4] = corners[i];
      corners[i].z() += rad;
      corners[i + 4].z() -= rad;
    }

    if (true) {
      for (int i : {0, 4}) {
        verts_out.push_back({corners[0 + i], rgba});
        verts_out.push_back({corners[1 + i], rgba});
        verts_out.push_back({corners[2 + i], rgba});

        verts_out.push_back({corners[1 + i], rgba});  // 0
        verts_out.push_back({corners[3 + i], rgba});
        verts_out.push_back({corners[2 + i], rgba});
      }

      for (int i : {2, 6, 7, 2, 3, 7, 0, 4, 5, 0, 5, 1, 0, 6, 4, 0, 6, 2, 1, 3, 7, 1, 5, 7}) {
        verts_out.push_back({corners[i], rgba});
      }

      constexpr int border0[12] = {0, 4, 6, 2, 2, 6, 3, 7, 0, 1, 2, 3};
      constexpr int border1[12] = {1, 5, 7, 3, 0, 4, 1, 5, 4, 5, 6, 7};
      rgba.w() = 1.0;

      for (int i = 0; i < 12; i++) {
        auto p0 = corners[border0[i]];
        auto p1 = corners[border1[i]];
        auto diff = (p1 - p0).normalized();
        math::Vector3f px = diff.z() == 0 ? math::Vector3f{1, 0, 1} : math::Vector3f{0, 1, 1};
        auto off = diff.cross(px) * 2000;

        verts_out.push_back({p0 + off, rgba});
        verts_out.push_back({p0 - off, rgba});
        verts_out.push_back({p1 - off, rgba});

        verts_out.push_back({p0 + off, rgba});
        verts_out.push_back({p1 + off, rgba});
        verts_out.push_back({p1 - off, rgba});
      }
    }

    if (node.flags) {
      debug_vis_draw(first_root, node.child_id, node.num_kids, depth + 1, nodes, verts_out);
    }
  }
}

}  // namespace

void Tfrag3::render_tree_cull_debug(const TfragRenderSettings& settings,
                                    SharedRenderState* render_state,
                                    ScopedProfilerNode& prof,
                                    std::unique_ptr<UniformBuffer>& uniform_buffer) {
  // generate debug verts:
  m_debug_vert_data.clear();
  auto& tree = m_cached_trees.at(settings.tree_idx).at(lod());

  debug_vis_draw(tree.vis->first_root, tree.vis->first_root, tree.vis->num_roots, 1,
                 tree.vis->vis_nodes, m_debug_vert_data);

   uniform_buffer->Set4x4MatrixDataInVkDeviceMemory("camera", 1,
      GL_FALSE, (float*)settings.math_camera.data());
   uniform_buffer->SetUniform4f("hvdf_offset",
      settings.hvdf_offset[0], settings.hvdf_offset[1], settings.hvdf_offset[2],
      settings.hvdf_offset[3]);
   uniform_buffer->SetUniform1f("fog_constant", settings.fog.x());

  //FIXME: Add depth test for vulkan
  //glEnable(GL_DEPTH_TEST);
  //glDepthFunc(GL_GEQUAL);
  //glEnable(GL_BLEND);
  //glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);  // ?
  //glDepthMask(GL_FALSE);

  //glBindVertexArray(m_debug_vao);
  //glBindBuffer(GL_ARRAY_BUFFER, m_debug_verts);

  int remaining = m_debug_vert_data.size();
  int start = 0;

  while (remaining > 0) {
    int to_do = std::min(DEBUG_TRI_COUNT * 3, remaining);

    //SetShaderModule(render_state->shaders[ShaderId::TFRAG3_NO_TEX]);
    //void* data;
    //vkMapMemory(device, device_memory, 0, to_do * sizeof(DebugVertex), 0, &data);
    //::memcpy(data, m_debug_vert_data.data() + start, to_do * sizeof(DebugVertex));
    //vkUnmapMemory(device, device_memory, nullptr);

    //TODO: Add draw function here
    
    prof.add_draw_call();
    prof.add_tri(to_do / 3);

    remaining -= to_do;
    start += to_do;
  }
}

void Tfrag3::InitializeDebugInputVertexAttribute() {
 VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
 inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
 inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
 inputAssembly.primitiveRestartEnable = VK_TRUE;

 VkVertexInputBindingDescription bindingDescription{};
 bindingDescription.binding = 0; bindingDescription.stride = sizeof(DebugVertex);
 bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

 std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};
 attributeDescriptions[0].binding = 0;
 attributeDescriptions[0].location = 0;
 attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
 attributeDescriptions[0].offset = offsetof(DebugVertex, position);

 attributeDescriptions[1].binding = 0;
 attributeDescriptions[1].location = 1;
 attributeDescriptions[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
 attributeDescriptions[1].offset = offsetof(DebugVertex, rgba);

 VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
 vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

 vertexInputInfo.vertexBindingDescriptionCount = 1;
 vertexInputInfo.vertexAttributeDescriptionCount =
     static_cast<uint32_t>(attributeDescriptions.size());
 vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
 vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
}

void Tfrag3::InitializeInputVertexAttribute() {
  //            glBufferData(GL_ARRAY_BUFFER, verts * sizeof(tfrag3::PreloadedVertex),
  //            nullptr,
  //                         GL_STREAM_DRAW);
  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(tfrag3::PreloadedVertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(tfrag3::PreloadedVertex, x);

  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[1].offset = offsetof(tfrag3::PreloadedVertex, s);

  attributeDescriptions[2].binding = 0;
  attributeDescriptions[2].location = 2;
  attributeDescriptions[2].format = VK_FORMAT_R16_UINT;
  attributeDescriptions[2].offset = offsetof(tfrag3::PreloadedVertex, color_index);

  VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

  vertexInputInfo.vertexBindingDescriptionCount = 1;
  vertexInputInfo.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(attributeDescriptions.size());
  vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
  vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
}
