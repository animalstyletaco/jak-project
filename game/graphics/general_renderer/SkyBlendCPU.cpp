#include "SkyBlendCPU.h"

#include <immintrin.h>

#include "common/util/os.h"

#include "game/graphics/general_renderer/AdgifHandler.h"

BaseSkyBlendCPU::BaseSkyBlendCPU() {
  for (int i = 0; i < 2; i++) {
    m_texture_data[i].resize(4 * m_sizes[i] * m_sizes[i]);
  }
}

BaseSkyBlendCPU::~BaseSkyBlendCPU() {
}

void BaseSkyBlendCPU::blend_sky_initial_fast(u8 intensity, u8* out, const u8* in, u32 size) {
  if (get_cpu_info().has_avx2) {
#ifdef __AVX2__
    __m256i intensity_vec = _mm256_set1_epi16(intensity);
    for (u32 i = 0; i < size / 16; i++) {
      __m128i tex_data8 = _mm_loadu_si128((const __m128i*)(in + (i * 16)));
      __m256i tex_data16 = _mm256_cvtepu8_epi16(tex_data8);
      tex_data16 = _mm256_mullo_epi16(tex_data16, intensity_vec);
      tex_data16 = _mm256_srli_epi16(tex_data16, 7);
      auto hi = _mm256_extracti128_si256(tex_data16, 1);
      auto result = _mm_packus_epi16(_mm256_castsi256_si128(tex_data16), hi);
      _mm_storeu_si128((__m128i*)(out + (i * 16)), result);
    }
#else
    ASSERT(false);
#endif
  } else {
    __m128i intensity_vec = _mm_set1_epi16(intensity);
    for (u32 i = 0; i < size / 8; i++) {
      __m128i tex_data8 = _mm_loadu_si64((const __m128i*)(in + (i * 8)));
      __m128i tex_data16 = _mm_cvtepu8_epi16(tex_data8);
      tex_data16 = _mm_mullo_epi16(tex_data16, intensity_vec);
      tex_data16 = _mm_srli_epi16(tex_data16, 7);
      auto result = _mm_packus_epi16(tex_data16, tex_data16);
      _mm_storel_epi64((__m128i*)(out + (i * 8)), result);
    }
  }
}

void BaseSkyBlendCPU::blend_sky_fast(u8 intensity, u8* out, const u8* in, u32 size) {
  if (get_cpu_info().has_avx2) {
#ifdef __AVX2__
    __m256i intensity_vec = _mm256_set1_epi16(intensity);
    __m256i max_intensity = _mm256_set1_epi16(255);
    for (u32 i = 0; i < size / 16; i++) {
      __m128i tex_data8 = _mm_loadu_si128((const __m128i*)(in + (i * 16)));
      __m128i out_val = _mm_loadu_si128((const __m128i*)(out + (i * 16)));
      __m256i tex_data16 = _mm256_cvtepu8_epi16(tex_data8);
      tex_data16 = _mm256_mullo_epi16(tex_data16, intensity_vec);
      tex_data16 = _mm256_srli_epi16(tex_data16, 7);
      tex_data16 = _mm256_min_epi16(max_intensity, tex_data16);
      auto hi = _mm256_extracti128_si256(tex_data16, 1);
      auto result = _mm_packus_epi16(_mm256_castsi256_si128(tex_data16), hi);
      out_val = _mm_adds_epu8(out_val, result);
      _mm_storeu_si128((__m128i*)(out + (i * 16)), out_val);
    }
#else
    ASSERT(false);
#endif
  } else {
    __m128i intensity_vec = _mm_set1_epi16(intensity);
    __m128i max_intensity = _mm_set1_epi16(255);
    for (u32 i = 0; i < size / 8; i++) {
      __m128i tex_data8 = _mm_loadu_si64((const __m128i*)(in + (i * 8)));
      __m128i out_val = _mm_loadu_si64((const __m128i*)(out + (i * 8)));
      __m128i tex_data16 = _mm_cvtepu8_epi16(tex_data8);
      tex_data16 = _mm_mullo_epi16(tex_data16, intensity_vec);
      tex_data16 = _mm_srli_epi16(tex_data16, 7);
      tex_data16 = _mm_min_epi16(max_intensity, tex_data16);
      auto result = _mm_packus_epi16(tex_data16, tex_data16);
      out_val = _mm_adds_epu8(out_val, result);
      _mm_storel_epi64((__m128i*)(out + (i * 8)), out_val);
    }
  }
  /*

   */
}

SkyBlendStats BaseSkyBlendCPU::do_sky_blends(DmaFollower& dma,
                                         BaseSharedRenderState* render_state,
                                         ScopedProfilerNode& /*prof*/) {
  SkyBlendStats stats;

  while (dma.current_tag().qwc == 6) {
    // assuming that the vif and gif-tag is correct
    auto setup_data = dma.read_and_advance();

    // first is an adgif
    AdgifHelper adgif(setup_data.data + 16);
    ASSERT(adgif.is_normal_adgif());
    ASSERT(adgif.alpha().data == 0x8000000068);  // Cs + Cd

    // next is the actual draw
    auto draw_data = dma.read_and_advance();
    ASSERT(draw_data.size_bytes == 6 * 16);

    GifTag draw_or_blend_tag(draw_data.data);

    // the first draw overwrites the previous frame's draw by disabling alpha blend (ABE = 0)
    bool is_first_draw = !GsPrim(draw_or_blend_tag.prim()).abe();

    // here's we're relying on the format of the drawing to get the alpha/offset.
    u32 coord;
    u32 intensity;
    memcpy(&coord, draw_data.data + (5 * 16), 4);
    memcpy(&intensity, draw_data.data + 16, 4);

    // we didn't parse the render-to-texture setup earlier, so we need a way to tell sky from
    // clouds. we can look at the drawing coordinates to tell - the sky is smaller than the clouds.
    int buffer_idx = 0;
    if (coord == 0x200) {
      // sky
      buffer_idx = 0;
    } else if (coord == 0x400) {
      buffer_idx = 1;
    } else {
      ASSERT(false);  // bad data
    }

    // look up the source texture
    setup_gpu_texture(adgif.tex0().tbp0(), is_first_draw, coord, intensity, buffer_idx, stats);
  }

  return stats;
}


