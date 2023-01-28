#include <immintrin.h>

#include "background_common.h"

void background_common::interp_time_of_day_slow(const math::Vector<s32, 4> itimes[4],
                             const std::vector<tfrag3::TimeOfDayColor>& in,
                             math::Vector<u8, 4>* out) {
  // Timer interp_timer;
  math::Vector4f weights[8];
  for (int component = 0; component < 8; component++) {
    int quad_idx = component / 2;
    int word_off = (component % 2 * 2);
    for (int channel = 0; channel < 4; channel++) {
      int word = word_off + (channel / 2);
      int hw_off = channel % 2;

      u32 word_val = itimes[quad_idx][word];
      u32 hw_val = hw_off ? (word_val >> 16) : word_val;
      hw_val = hw_val & 0xff;
      weights[component][channel] = hw_val / 64.f;
    }
  }

  for (size_t color = 0; color < in.size(); color++) {
    math::Vector4f result = math::Vector4f::zero();
    for (int component = 0; component < 8; component++) {
      for (int channel = 0; channel < 4; channel++) {
        result[channel] += in[color].rgba[component][channel] * weights[component][channel];
      }
      // result += in[color].rgba[component].cast<float>() * weights[component];
    }
    result[0] = std::min(result[0], 255.f);
    result[1] = std::min(result[1], 255.f);
    result[2] = std::min(result[2], 255.f);
    result[3] = std::min(result[3], 128.f);  // note: different for alpha!
    out[color] = result.cast<u8>();
  }
}

// we want to absolutely minimize the number of time we have to "cross lanes" in AVX (meaning X
// component of one vector interacts with Y component of another).  We can make this a lot better by
// taking groups of 4 time of day colors (each containing 8x RGBAs) and rearranging them with this
// pattern.  We want to compute:
// [rgba][0][0] * weights[0] + [rgba][0][1] * weights[1] + [rgba][0][2]... + rgba[0][7] * weights[7]
// RGBA is already a vector of 4 components, but with AVX we have vectors with 32 bytes which fit
// 16 colors in them.

// This makes each vector have:
// colors0 = [rgba][0][0], [rgba][1][0], [rgba][2][0], [rgba][3][0]
// colors1 = [rgba][0][1], [rgba][1][1], [rgba][2][1], [rgba][3][1]
// ...
// so we can basically add up the columns (multiplying by weights in between)
// and we'll end up with [final0, final1, final2, final3, final4]

// the swizzle function below rearranges to get this pattern.
// it's not the most efficient way to do it, but it just runs during loading and not on every frame.

SwizzledTimeOfDay background_common::swizzle_time_of_day(const std::vector<tfrag3::TimeOfDayColor>& in) {
  SwizzledTimeOfDay out;
  out.data.resize((in.size() + 3) * 8 * 4);

  // we're rearranging per 4 colors (groups of 32 * 4 = 128)
  // color (lots of these)
  // component (8 of these)
  // channel (4 of these, rgba)

  for (u32 color_quad = 0; color_quad < (in.size() + 3) / 4; color_quad++) {
    u8* quad_out = out.data.data() + color_quad * 128;
    for (u32 component = 0; component < 8; component++) {
      for (u32 color = 0; color < 4; color++) {
        for (u32 channel = 0; channel < 4; channel++) {
          size_t in_idx = color_quad * 4 + color;
          if (in_idx < in.size()) {
            *quad_out = in.at(color_quad * 4 + color).rgba[component][channel];
          } else {
            *quad_out = 0;
          }
          quad_out++;
        }
      }
    }
  }
  out.color_count = (in.size() + 3) & (~3);
  return out;
}

void background_common::interp_time_of_day_fast(const math::Vector<s32, 4> itimes[4],
                             const SwizzledTimeOfDay& swizzled_colors,
                             math::Vector<u8, 4>* out) {
  math::Vector<u16, 4> weights[8];
  for (int component = 0; component < 8; component++) {
    int quad_idx = component / 2;
    int word_off = (component % 2 * 2);
    for (int channel = 0; channel < 4; channel++) {
      int word = word_off + (channel / 2);
      int hw_off = channel % 2;

      u32 word_val = itimes[quad_idx][word];
      u32 hw_val = hw_off ? (word_val >> 16) : word_val;
      hw_val = hw_val & 0xff;
      weights[component][channel] = hw_val;
    }
  }

  // weight multipliers
  __m128i weights0 = _mm_setr_epi16(weights[0][0], weights[0][1], weights[0][2], weights[0][3],
                                    weights[0][0], weights[0][1], weights[0][2], weights[0][3]);
  __m128i weights1 = _mm_setr_epi16(weights[1][0], weights[1][1], weights[1][2], weights[1][3],
                                    weights[1][0], weights[1][1], weights[1][2], weights[1][3]);
  __m128i weights2 = _mm_setr_epi16(weights[2][0], weights[2][1], weights[2][2], weights[2][3],
                                    weights[2][0], weights[2][1], weights[2][2], weights[2][3]);
  __m128i weights3 = _mm_setr_epi16(weights[3][0], weights[3][1], weights[3][2], weights[3][3],
                                    weights[3][0], weights[3][1], weights[3][2], weights[3][3]);
  __m128i weights4 = _mm_setr_epi16(weights[4][0], weights[4][1], weights[4][2], weights[4][3],
                                    weights[4][0], weights[4][1], weights[4][2], weights[4][3]);
  __m128i weights5 = _mm_setr_epi16(weights[5][0], weights[5][1], weights[5][2], weights[5][3],
                                    weights[5][0], weights[5][1], weights[5][2], weights[5][3]);
  __m128i weights6 = _mm_setr_epi16(weights[6][0], weights[6][1], weights[6][2], weights[6][3],
                                    weights[6][0], weights[6][1], weights[6][2], weights[6][3]);
  __m128i weights7 = _mm_setr_epi16(weights[7][0], weights[7][1], weights[7][2], weights[7][3],
                                    weights[7][0], weights[7][1], weights[7][2], weights[7][3]);

  // saturation: note that alpha is saturated to 128 but the rest are 255.
  // TODO: maybe we should saturate to 255 for everybody (can do this using a single packus) and
  // change the shader to deal with this.
  __m128i sat = _mm_set_epi16(128, 255, 255, 255, 128, 255, 255, 255);

  for (u32 color_quad = 0; color_quad < swizzled_colors.color_count / 4; color_quad++) {
    // first, load colors. We put 16 bytes / register and don't touch the upper half because we
    // convert u8s to u16s.
    {
      const u8* base = swizzled_colors.data.data() + color_quad * 128;
      __m128i color0_p = _mm_loadu_si64((const __m128i*)(base + 0));
      __m128i color1_p = _mm_loadu_si64((const __m128i*)(base + 16));
      __m128i color2_p = _mm_loadu_si64((const __m128i*)(base + 32));
      __m128i color3_p = _mm_loadu_si64((const __m128i*)(base + 48));
      __m128i color4_p = _mm_loadu_si64((const __m128i*)(base + 64));
      __m128i color5_p = _mm_loadu_si64((const __m128i*)(base + 80));
      __m128i color6_p = _mm_loadu_si64((const __m128i*)(base + 96));
      __m128i color7_p = _mm_loadu_si64((const __m128i*)(base + 112));

      // unpack to 16-bits. each has 16x 16 bit colors.
      __m128i color0 = _mm_cvtepu8_epi16(color0_p);
      __m128i color1 = _mm_cvtepu8_epi16(color1_p);
      __m128i color2 = _mm_cvtepu8_epi16(color2_p);
      __m128i color3 = _mm_cvtepu8_epi16(color3_p);
      __m128i color4 = _mm_cvtepu8_epi16(color4_p);
      __m128i color5 = _mm_cvtepu8_epi16(color5_p);
      __m128i color6 = _mm_cvtepu8_epi16(color6_p);
      __m128i color7 = _mm_cvtepu8_epi16(color7_p);

      // multiply by weights
      color0 = _mm_mullo_epi16(color0, weights0);
      color1 = _mm_mullo_epi16(color1, weights1);
      color2 = _mm_mullo_epi16(color2, weights2);
      color3 = _mm_mullo_epi16(color3, weights3);
      color4 = _mm_mullo_epi16(color4, weights4);
      color5 = _mm_mullo_epi16(color5, weights5);
      color6 = _mm_mullo_epi16(color6, weights6);
      color7 = _mm_mullo_epi16(color7, weights7);

      // add. This order minimizes dependencies.
      color0 = _mm_add_epi16(color0, color1);
      color2 = _mm_add_epi16(color2, color3);
      color4 = _mm_add_epi16(color4, color5);
      color6 = _mm_add_epi16(color6, color7);

      color0 = _mm_add_epi16(color0, color2);
      color4 = _mm_add_epi16(color4, color6);

      color0 = _mm_add_epi16(color0, color4);

      // divide, because we multiplied our weights by 2^7.
      color0 = _mm_srli_epi16(color0, 6);

      // saturate
      color0 = _mm_min_epu16(sat, color0);

      // back to u8s.
      auto result = _mm_packus_epi16(color0, color0);

      // store result
      _mm_storel_epi64((__m128i*)(&out[color_quad * 4]), result);
    }

    {
      const u8* base = swizzled_colors.data.data() + color_quad * 128 + 8;
      __m128i color0_p = _mm_loadu_si64((const __m128i*)(base + 0));
      __m128i color1_p = _mm_loadu_si64((const __m128i*)(base + 16));
      __m128i color2_p = _mm_loadu_si64((const __m128i*)(base + 32));
      __m128i color3_p = _mm_loadu_si64((const __m128i*)(base + 48));
      __m128i color4_p = _mm_loadu_si64((const __m128i*)(base + 64));
      __m128i color5_p = _mm_loadu_si64((const __m128i*)(base + 80));
      __m128i color6_p = _mm_loadu_si64((const __m128i*)(base + 96));
      __m128i color7_p = _mm_loadu_si64((const __m128i*)(base + 112));

      // unpack to 16-bits. each has 16x 16 bit colors.
      __m128i color0 = _mm_cvtepu8_epi16(color0_p);
      __m128i color1 = _mm_cvtepu8_epi16(color1_p);
      __m128i color2 = _mm_cvtepu8_epi16(color2_p);
      __m128i color3 = _mm_cvtepu8_epi16(color3_p);
      __m128i color4 = _mm_cvtepu8_epi16(color4_p);
      __m128i color5 = _mm_cvtepu8_epi16(color5_p);
      __m128i color6 = _mm_cvtepu8_epi16(color6_p);
      __m128i color7 = _mm_cvtepu8_epi16(color7_p);

      // multiply by weights
      color0 = _mm_mullo_epi16(color0, weights0);
      color1 = _mm_mullo_epi16(color1, weights1);
      color2 = _mm_mullo_epi16(color2, weights2);
      color3 = _mm_mullo_epi16(color3, weights3);
      color4 = _mm_mullo_epi16(color4, weights4);
      color5 = _mm_mullo_epi16(color5, weights5);
      color6 = _mm_mullo_epi16(color6, weights6);
      color7 = _mm_mullo_epi16(color7, weights7);

      // add. This order minimizes dependencies.
      color0 = _mm_add_epi16(color0, color1);
      color2 = _mm_add_epi16(color2, color3);
      color4 = _mm_add_epi16(color4, color5);
      color6 = _mm_add_epi16(color6, color7);

      color0 = _mm_add_epi16(color0, color2);
      color4 = _mm_add_epi16(color4, color6);

      color0 = _mm_add_epi16(color0, color4);

      // divide, because we multiplied our weights by 2^7.
      color0 = _mm_srli_epi16(color0, 6);

      // saturate
      color0 = _mm_min_epu16(sat, color0);

      // back to u8s.
      auto result = _mm_packus_epi16(color0, color0);

      // store result
      _mm_storel_epi64((__m128i*)(&out[color_quad * 4 + 2]), result);
    }
  }
}

bool background_common::sphere_in_view_ref(const math::Vector4f& sphere, const math::Vector4f* planes) {
  math::Vector4f acc =
      planes[0] * sphere.x() + planes[1] * sphere.y() + planes[2] * sphere.z() - planes[3];

  return acc.x() > -sphere.w() && acc.y() > -sphere.w() && acc.z() > -sphere.w() &&
         acc.w() > -sphere.w();
}

// this isn't super efficient, but we spend so little time here it's not worth it to go faster.
void background_common::cull_check_all_slow(const math::Vector4f* planes,
                         const std::vector<tfrag3::VisNode>& nodes,
                         const u8* level_occlusion_string,
                         u8* out) {
  if (level_occlusion_string) {
    for (size_t i = 0; i < nodes.size(); i++) {
      u16 my_id = nodes[i].my_id;
      bool not_occluded =
          my_id != 0xffff && level_occlusion_string[my_id / 8] & (1 << (7 - (my_id & 7)));
      out[i] = not_occluded && sphere_in_view_ref(nodes[i].bsphere, planes);
    }
  } else {
    for (size_t i = 0; i < nodes.size(); i++) {
      out[i] = sphere_in_view_ref(nodes[i].bsphere, planes);
    }
  }
}

void background_common::update_render_state_from_pc_settings(BaseSharedRenderState* state, const TfragPcPortData& data) {
  if (!state->has_pc_data) {
    for (int i = 0; i < 4; i++) {
      state->camera_planes[i] = data.planes[i];
      state->camera_matrix[i] = data.camera[i];
    }
    state->camera_pos = data.cam_trans;
    state->camera_hvdf_off = data.hvdf_off;
    state->camera_fog = data.fog;
    state->has_pc_data = true;
  }
}
