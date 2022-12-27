#include "Tfrag3.h"

#include "third-party/imgui/imgui.h"

BaseTfrag3::~BaseTfrag3() {
}

/*!
 * Render all trees with settings for the given tree.
 * This is intended to be used only for debugging when we can't easily get commands for all trees
 * working.
 */
void BaseTfrag3::render_all_trees(int geom,
                              const TfragRenderSettings& settings,
                              BaseSharedRenderState* render_state,
                              ScopedProfilerNode& prof) {
  TfragRenderSettings settings_copy = settings;
  for (size_t i = 0; i < get_total_cached_trees_count(geom); i++) {
    if (get_cached_tree(geom, i).kind != tfrag3::TFragmentTreeKind::INVALID) {
      settings_copy.tree_idx = i;
      render_tree(geom, settings_copy, render_state, prof);
    }
  }
}

void BaseTfrag3::render_matching_trees(int geom,
                                   const std::vector<tfrag3::TFragmentTreeKind>& trees,
                                   const TfragRenderSettings& settings,
                                   BaseSharedRenderState* render_state,
                                   ScopedProfilerNode& prof) {
  TfragRenderSettings settings_copy = settings;
  for (size_t i = 0; i < get_total_cached_trees_count(geom); i++) {
    auto& tree = get_cached_tree(geom, i);
    tree.reset_stats();
    if (!tree.allowed) {
      continue;
    }
    if (std::find(trees.begin(), trees.end(), tree.kind) != trees.end() || tree.forced) {
      tree.rendered_this_frame = true;
      settings_copy.tree_idx = i;
      render_tree(geom, settings_copy, render_state, prof);
      if (tree.cull_debug) {
        render_tree_cull_debug(settings_copy, render_state, prof);
      }
    }
  }
}

void BaseTfrag3::draw_debug_window() {
  for (int i = 0; i < (int)get_total_cached_trees_count(lod()); i++) {
    auto& tree = get_cached_tree(lod(), i);
    if (tree.kind == tfrag3::TFragmentTreeKind::INVALID) {
      continue;
    }
    ImGui::PushID(i);
    ImGui::Text("[%d] %10s", i, tfrag3::tfrag_tree_names[(int)tree.kind]);
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

float BaseTfrag3::frac(float in) {
  return in - (int)in;
}

void BaseTfrag3::debug_vis_draw(int first_root,
                    int tree,
                    int num,
                    int depth,
                    const std::vector<tfrag3::VisNode>& nodes,
                    std::vector<BaseTfrag3::DebugVertex>& verts_out) {
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
