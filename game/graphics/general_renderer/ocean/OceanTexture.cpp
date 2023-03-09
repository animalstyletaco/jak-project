#include "OceanTexture.h"

#include "game/graphics/vulkan_renderer/AdgifHandler.h"
#include "game/graphics/general_renderer/ocean/CommonOceanRenderer.h"

#include "third-party/imgui/imgui.h"

BaseOceanTexture::BaseOceanTexture(bool generate_mipmaps)
    : m_generate_mipmaps(generate_mipmaps) {
  m_dbuf_x = m_dbuf_a;
  m_dbuf_y = m_dbuf_b;

  m_tbuf_x = m_tbuf_a;
  m_tbuf_y = m_tbuf_b;

  init_pc();

  // initialize the mipmap drawing
}

BaseOceanTexture::~BaseOceanTexture() {
  destroy_pc();
}

void BaseOceanTexture::handle_ocean_texture_jak1(DmaFollower& dma,
                                        BaseSharedRenderState* render_state,
                                        ScopedProfilerNode& prof) {

  // if we're doing mipmaps, render to temp.
  // otherwise, render directly to target.
  setup_framebuffer_context(0);
  // render to the first texture
  {
    // (set-display-gs-state arg0 ocean-tex-page-0 128 128 0 0)
    auto data = dma.read_and_advance();
    (void)data;
  }

  // set up VIF
  {
    // (new 'static 'vif-tag :cmd (vif-cmd base))
    // (new 'static 'vif-tag :imm #xc0 :cmd (vif-cmd offset))
    auto data = dma.read_and_advance();
    ASSERT(data.size_bytes == 0);
    ASSERT(data.vifcode0().kind == VifCode::Kind::BASE);
    ASSERT(data.vifcode1().kind == VifCode::Kind::OFFSET);
    ASSERT(data.vifcode0().immediate == 0);
    ASSERT(data.vifcode1().immediate == 0xc0);
  }

  // load texture constants
  {
    // (ocean-texture-add-constants arg0)
    auto data = dma.read_and_advance();
    ASSERT(data.size_bytes == sizeof(OceanTextureConstants));
    ASSERT(data.vifcode0().kind == VifCode::Kind::STCYCL);
    ASSERT(data.vifcode0().immediate == 0x404);
    ASSERT(data.vifcode1().kind == VifCode::Kind::UNPACK_V4_32);
    ASSERT(data.vifcode1().num == data.size_bytes / 16);
    ASSERT(data.vifcode1().immediate == TexVu1Data::CONSTANTS);
    memcpy(&m_texture_constants, data.data, sizeof(OceanTextureConstants));
  }

  // set up GS for envmap texture drawing
  {
    // (ocean-texture-add-envmap arg0)
    auto data = dma.read_and_advance();
    ASSERT(data.size_bytes == sizeof(AdGifData) + 16);  // 16 for the giftag.
    ASSERT(data.vifcode0().kind == VifCode::Kind::NOP);
    ASSERT(data.vifcode1().kind == VifCode::Kind::DIRECT);
    memcpy(&m_envmap_adgif, data.data + 16, sizeof(AdGifData));
    // HACK
    setup_renderer();
  }

  // vertices are uploaded double buffered
  m_texture_vertices_loading = m_texture_vertices_a;
  m_texture_vertices_drawing = m_texture_vertices_b;

  // add first group of vertices
  {
    // (ocean-texture-add-verts arg0 sv-16)
    auto data = dma.read_and_advance();
    ASSERT(data.size_bytes == sizeof(m_texture_vertices_a));
    ASSERT(data.vifcode0().kind == VifCode::Kind::STCYCL);
    ASSERT(data.vifcode0().immediate == 0x404);
    ASSERT(data.vifcode1().kind == VifCode::Kind::UNPACK_V4_32);
    ASSERT(data.vifcode1().num == data.size_bytes / 16);
    VifCodeUnpack up(data.vifcode1());
    ASSERT(up.addr_qw == 0);
    ASSERT(up.use_tops_flag == true);
    memcpy(m_texture_vertices_loading, data.data, sizeof(m_texture_vertices_a));
  }

  // first call
  {
    auto data = dma.read_and_advance();
    ASSERT(data.size_bytes == 0);
    ASSERT(data.vifcode0().kind == VifCode::Kind::MSCALF);
    ASSERT(data.vifcode0().immediate == TexVu1Prog::START);
    ASSERT(data.vifcode1().kind == VifCode::Kind::STMOD);  // not sure why...
    run_L1_PC();
  }

  // loop over vertex groups
  for (int i = 0; i < NUM_FRAG_LOOPS; i++) {
    auto verts = dma.read_and_advance();
    ASSERT(verts.size_bytes == sizeof(m_texture_vertices_a));
    ASSERT(verts.vifcode0().kind == VifCode::Kind::STCYCL);
    ASSERT(verts.vifcode0().immediate == 0x404);
    ASSERT(verts.vifcode1().kind == VifCode::Kind::UNPACK_V4_32);
    ASSERT(verts.vifcode1().num == verts.size_bytes / 16);
    VifCodeUnpack up(verts.vifcode1());
    ASSERT(up.addr_qw == 0);
    ASSERT(up.use_tops_flag == true);
    memcpy(m_texture_vertices_loading, verts.data, sizeof(m_texture_vertices_a));

    auto call = dma.read_and_advance();
    ASSERT(call.size_bytes == 0);
    ASSERT(call.vifcode0().kind == VifCode::Kind::MSCALF);
    ASSERT(call.vifcode0().immediate == TexVu1Prog::REST);
    ASSERT(call.vifcode1().kind == VifCode::Kind::STMOD);  // not sure why...
    run_L2_PC();
  }

  // last upload does something weird...
  {
    // (ocean-texture-add-verts-last arg0 (the-as (inline-array vector) sv-48) sv-64)
    auto data0 = dma.read_and_advance();
    ASSERT(data0.size_bytes == 128 * 16);
    ASSERT(data0.vifcode0().kind == VifCode::Kind::STCYCL);
    ASSERT(data0.vifcode0().immediate == 0x404);
    ASSERT(data0.vifcode1().kind == VifCode::Kind::UNPACK_V4_32);
    ASSERT(data0.vifcode1().num == data0.size_bytes / 16);
    VifCodeUnpack up0(data0.vifcode1());
    ASSERT(up0.addr_qw == 0);
    ASSERT(up0.use_tops_flag == true);
    memcpy(m_texture_vertices_loading, data0.data, 128 * 16);

    auto data1 = dma.read_and_advance();
    ASSERT(data1.size_bytes == 64 * 16);
    ASSERT(data1.vifcode0().kind == VifCode::Kind::STCYCL);
    ASSERT(data1.vifcode0().immediate == 0x404);
    ASSERT(data1.vifcode1().kind == VifCode::Kind::UNPACK_V4_32);
    ASSERT(data1.vifcode1().num == data1.size_bytes / 16);
    VifCodeUnpack up1(data1.vifcode1());
    ASSERT(up1.addr_qw == 128);
    ASSERT(up1.use_tops_flag == true);
    memcpy(m_texture_vertices_loading + 128, data1.data, 64 * 16);
  }

  // last rest call
  {
    auto data = dma.read_and_advance();
    ASSERT(data.size_bytes == 0);
    ASSERT(data.vifcode0().kind == VifCode::Kind::MSCALF);
    ASSERT(data.vifcode0().immediate == TexVu1Prog::REST);
    ASSERT(data.vifcode1().kind == VifCode::Kind::STMOD);  // not sure why...
    run_L2_PC();
  }

  // last call
  {
    auto data = dma.read_and_advance();
    ASSERT(data.size_bytes == 0);
    ASSERT(data.vifcode0().kind == VifCode::Kind::MSCALF);
    ASSERT(data.vifcode0().immediate == TexVu1Prog::DONE);
    ASSERT(data.vifcode1().kind == VifCode::Kind::STMOD);  // not sure why...
    // this program does nothing.
  }

  flush(render_state, prof);
  if (m_generate_mipmaps) {
    // if we did mipmaps, the above code rendered to temp, and now we need to generate mipmaps
    // in the real output
    make_texture_with_mipmaps(render_state, prof);
  }

  // give to gpu!
  move_existing_to_vram(ocean_common::OCEAN_TEX_TBP_JAK1);
}

void BaseOceanTexture::handle_ocean_texture_jak2(DmaFollower& dma,
                                                 BaseSharedRenderState* render_state,
                                                 ScopedProfilerNode& prof) {
  // if we're doing mipmaps, render to temp.
  // otherwise, render directly to target.
  //setup_framebuffer_context();
  setup_framebuffer_context(0);
  // render to the first texture
  {
    // (set-display-gs-state arg0 21 128 128 0 0)
    auto data = dma.read_and_advance();
    (void)data;
  }

  // set up GS for envmap texture drawing
  {
    // (ocean-texture-add-envmap arg0)
    auto data = dma.read_and_advance();
    ASSERT(data.size_bytes == sizeof(AdGifData) + 16);  // 16 for the giftag.
    ASSERT(data.vifcode0().kind == VifCode::Kind::NOP);
    ASSERT(data.vifcode1().kind == VifCode::Kind::DIRECT);
    memcpy(&m_envmap_adgif, data.data + 16, sizeof(AdGifData));
    // HACK
    setup_renderer();
  }

  // set up VIF
  {
    // (new 'static 'vif-tag)
    // (new 'static 'vif-tag :imm #x4 :cmd (vif-cmd offset))
    auto data = dma.read_and_advance();
    ASSERT(data.size_bytes == 64);
    ASSERT(data.vifcode0().kind == VifCode::Kind::NOP);
    ASSERT(data.vifcode1().kind == VifCode::Kind::DIRECT);
    ASSERT(data.vifcode0().immediate == 0);
    ASSERT(data.vifcode1().immediate == 0x4);
  }

  // (dma-buffer-add-vu-function arg0 ocean-texture-vu1-block 1)
  dma.read_and_advance();

  {
    // (ocean-texture-add-constants obj arg0)
    auto data = dma.read_and_advance();
    ASSERT(data.size_bytes == sizeof(OceanTextureConstants));
    ASSERT(data.vifcode0().kind == VifCode::Kind::STCYCL);
    ASSERT(data.vifcode0().immediate == 0x404);
    ASSERT(data.vifcode1().kind == VifCode::Kind::UNPACK_V4_32);
    ASSERT(data.vifcode1().num == data.size_bytes / 16);
    ASSERT(data.vifcode1().immediate == TexVu1Data::CONSTANTS);
    memcpy(&m_texture_constants, data.data, sizeof(OceanTextureConstants));
  }

  // vertices are uploaded double buffered
  m_texture_vertices_loading = m_texture_vertices_a;
  m_texture_vertices_drawing = m_texture_vertices_b;

  // add first group of vertices
  {
    // (ocean-texture-add-verts arg0 sv-16)
    auto data = dma.read_and_advance();
    ASSERT(data.size_bytes == sizeof(m_texture_vertices_a));
    ASSERT(data.vifcode0().kind == VifCode::Kind::STCYCL);
    ASSERT(data.vifcode0().immediate == 0x404);
    ASSERT(data.vifcode1().kind == VifCode::Kind::UNPACK_V4_32);
    ASSERT(data.vifcode1().num == data.size_bytes / 16);
    VifCodeUnpack up(data.vifcode1());
    ASSERT(up.addr_qw == 0);
    ASSERT(up.use_tops_flag == true);
    memcpy(m_texture_vertices_loading, data.data, sizeof(m_texture_vertices_a));
  }

  // first call
  {
    auto data = dma.read_and_advance();
    ASSERT(data.size_bytes == 0);
    ASSERT(data.vifcode0().kind == VifCode::Kind::MSCALF);
    ASSERT(data.vifcode0().immediate == TexVu1Prog::START);
    ASSERT(data.vifcode1().kind == VifCode::Kind::STMOD);  // not sure why...
    run_L1_PC_jak2();
  }

  // loop over vertex groups
  for (int i = 0; i < NUM_FRAG_LOOPS; i++) {
    auto verts = dma.read_and_advance();
    ASSERT(verts.size_bytes == sizeof(m_texture_vertices_a));
    ASSERT(verts.vifcode0().kind == VifCode::Kind::STCYCL);
    ASSERT(verts.vifcode0().immediate == 0x404);
    ASSERT(verts.vifcode1().kind == VifCode::Kind::UNPACK_V4_32);
    ASSERT(verts.vifcode1().num == verts.size_bytes / 16);
    VifCodeUnpack up(verts.vifcode1());
    ASSERT(up.addr_qw == 0);
    ASSERT(up.use_tops_flag == true);
    memcpy(m_texture_vertices_loading, verts.data, sizeof(m_texture_vertices_a));

    auto call = dma.read_and_advance();
    ASSERT(call.size_bytes == 0);
    ASSERT(call.vifcode0().kind == VifCode::Kind::MSCALF);
    ASSERT(call.vifcode0().immediate == TexVu1Prog::REST);
    ASSERT(call.vifcode1().kind == VifCode::Kind::STMOD);  // not sure why...
    run_L2_PC_jak2();
  }

  // last upload does something weird...
  {
    // (ocean-texture-add-verts-last arg0 (the-as (inline-array vector) sv-48) sv-64)
    auto data0 = dma.read_and_advance();
    ASSERT(data0.size_bytes == 128 * 16);
    ASSERT(data0.vifcode0().kind == VifCode::Kind::STCYCL);
    ASSERT(data0.vifcode0().immediate == 0x404);
    ASSERT(data0.vifcode1().kind == VifCode::Kind::UNPACK_V4_32);
    ASSERT(data0.vifcode1().num == data0.size_bytes / 16);
    VifCodeUnpack up0(data0.vifcode1());
    ASSERT(up0.addr_qw == 0);
    ASSERT(up0.use_tops_flag == true);
    memcpy(m_texture_vertices_loading, data0.data, 128 * 16);

    auto data1 = dma.read_and_advance();
    ASSERT(data1.size_bytes == 64 * 16);
    ASSERT(data1.vifcode0().kind == VifCode::Kind::STCYCL);
    ASSERT(data1.vifcode0().immediate == 0x404);
    ASSERT(data1.vifcode1().kind == VifCode::Kind::UNPACK_V4_32);
    ASSERT(data1.vifcode1().num == data1.size_bytes / 16);
    VifCodeUnpack up1(data1.vifcode1());
    ASSERT(up1.addr_qw == 128);
    ASSERT(up1.use_tops_flag == true);
    memcpy(m_texture_vertices_loading + 128, data1.data, 64 * 16);
  }

  // last rest call
  {
    auto data = dma.read_and_advance();
    ASSERT(data.size_bytes == 0);
    ASSERT(data.vifcode0().kind == VifCode::Kind::MSCALF);
    ASSERT(data.vifcode0().immediate == TexVu1Prog::REST);
    ASSERT(data.vifcode1().kind == VifCode::Kind::STMOD);  // not sure why...
    run_L2_PC_jak2();
  }

  // last call
  {
    auto data = dma.read_and_advance();
    ASSERT(data.size_bytes == 0);
    ASSERT(data.vifcode0().kind == VifCode::Kind::MSCALF);
    ASSERT(data.vifcode0().immediate == TexVu1Prog::DONE);
    ASSERT(data.vifcode1().kind == VifCode::Kind::STMOD);  // not sure why...
    // this program does nothing.
  }

  flush(render_state, prof);
  if (m_generate_mipmaps) {
    // if we did mipmaps, the above code rendered to temp, and now we need to generate mipmaps
    // in the real output
    make_texture_with_mipmaps(render_state, prof);
  }

  // (reset-display-gs-state *display* arg0)
  // dma.read_and_advance();

  // give to gpu!
  move_existing_to_vram(ocean_common::OCEAN_TEX_TBP_JAK2);
}
