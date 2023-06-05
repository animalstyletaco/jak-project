#include "VulkanRenderer.h"

#include "common/log/log.h"
#include "common/util/FileUtil.h"

#include "game/graphics/general_renderer/TextureUploadHandler.h"
#include "game/graphics/general_renderer/VisDataHandler.h"
#include "game/graphics/vulkan_renderer/DepthCue.h"
#include "game/graphics/vulkan_renderer/DirectRenderer.h"
#include "game/graphics/vulkan_renderer/EyeRenderer.h"
#include "game/graphics/vulkan_renderer/ShadowRenderer.h"
#include "game/graphics/vulkan_renderer/SkyRenderer.h"
#include "game/graphics/vulkan_renderer/sprite/Sprite3.h"
#include "game/graphics/vulkan_renderer/background/Shrub.h"
#include "game/graphics/vulkan_renderer/background/TFragment.h"
#include "game/graphics/vulkan_renderer/background/Tie3.h"
#include "game/graphics/vulkan_renderer/foreground/Generic2.h"
#include "game/graphics/vulkan_renderer/foreground/Merc2.h"
#include "game/graphics/vulkan_renderer/ocean/OceanMidAndFar.h"
#include "game/graphics/vulkan_renderer/ocean/OceanNear.h"
#include "game/graphics/vulkan_renderer/ProgressRenderer.h"
#include "game/graphics/vulkan_renderer/LightningRenderer.h"

#include "third-party/imgui/imgui.h"
#include "third-party/imgui/imgui_impl_vulkan.h"

// for the vif callback
#include "game/kernel/common/kmachine.h"
#include "game/runtime.h"

namespace vulkan_renderer {
std::string g_current_render;

}

class VulkanTextureUploadHandler : public BaseTextureUploadHandler, public BucketVulkanRenderer {
 public:
  VulkanTextureUploadHandler(const std::string& name,
                            int my_id,
                            std::unique_ptr<GraphicsDeviceVulkan>& device,
                            VulkanInitializationInfo& vulkan_info)
      : BaseTextureUploadHandler(name, my_id), BucketVulkanRenderer(device, vulkan_info){};
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override {
    m_eye_renderer = render_state->eye_renderer;
    BaseTextureUploadHandler::render(dma, render_state, prof);
  };

 private:
  void texture_pool_handle_upload_now(const u8* tpage,
                                      int mode,
                                      const u8* memory_base,
                                      u32 s7_ptr) override {
    m_vulkan_info.texture_pool->handle_upload_now(tpage, mode, memory_base, s7_ptr);
  }
  void eye_renderer_handle_eye_dma2(DmaFollower& dma,
                                    BaseSharedRenderState* render_state,
                                    ScopedProfilerNode& prof) override {
    m_eye_renderer->handle_eye_dma2(dma, render_state, prof);
  }

  EyeVulkanRenderer* m_eye_renderer = nullptr;
};

class VisDataVulkanHandler : public BaseVisDataHandler, public BucketVulkanRenderer {
 public:
  VisDataVulkanHandler(const std::string& name,
                       int my_id,
                       std::unique_ptr<GraphicsDeviceVulkan>& device,
                       VulkanInitializationInfo& vulkan_info)
      : BaseVisDataHandler(name, my_id), BucketVulkanRenderer(device, vulkan_info){};
  void render(DmaFollower& dma, SharedVulkanRenderState* render_state, ScopedProfilerNode& prof) override {
    BaseVisDataHandler::render(dma, render_state, prof);
  };
};


using namespace vulkan_renderer;

VulkanRenderer::~VulkanRenderer() {
  freeCommandBuffers();
}

VulkanRenderer::VulkanRenderer(std::shared_ptr<VulkanTexturePool> texture_pool,
                               std::shared_ptr<VulkanLoader> loader,
                               GameVersion version,
                               std::unique_ptr<GraphicsDeviceVulkan>& device)
    : m_version(version), m_render_state(version, device), m_device(device), m_vulkan_info(device, version) {
  m_vulkan_info.texture_pool = texture_pool;
  m_vulkan_info.loader = loader;

  createCommandBuffers();

  m_extents = {640, 480};
  recreateSwapChain(false);

  //May be overkill for descriptor pool
  std::vector<VkDescriptorPoolSize> poolSizes = {
    {VK_DESCRIPTOR_TYPE_SAMPLER, 100},
    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 300000},
    {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100},
    {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100},
    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 10000},
    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10000},
    {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 10000}
  };

  uint32_t maxSets = 0;
  for (auto& poolSize : poolSizes) {
    maxSets += poolSize.descriptorCount;
  }
  m_vulkan_info.descriptor_pool = std::make_unique<DescriptorPool>(
      m_device, maxSets, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, poolSizes);
  m_collide_renderer = std::make_unique<CollideMeshVulkanRenderer>(m_device, m_vulkan_info);

  m_blackout_renderer = std::make_unique<FullScreenDrawVulkan>(m_device, m_vulkan_info);

  // initialize all renderers
  // initialize all renderers
  switch (m_version) {
    case GameVersion::Jak1:
      init_bucket_renderers_jak1();
      break;
    case GameVersion::Jak2:
      init_bucket_renderers_jak2();
      break;
    default:
      ASSERT(false);
  }
}

/*!
 * Construct bucket renderers.  We can specify different renderers for different buckets
 */
void VulkanRenderer::init_bucket_renderers_jak1() {
  using namespace jak1;
  m_bucket_renderers.resize((int)BucketId::MAX_BUCKETS);
  m_graphics_bucket_renderers.resize((int)BucketId::MAX_BUCKETS);
  m_bucket_categories.resize((int)BucketId::MAX_BUCKETS, BucketCategory::OTHER);

  std::vector<tfrag3::TFragmentTreeKind> normal_tfrags = {tfrag3::TFragmentTreeKind::NORMAL,
                                                          tfrag3::TFragmentTreeKind::LOWRES};
  std::vector<tfrag3::TFragmentTreeKind> dirt_tfrags = {tfrag3::TFragmentTreeKind::DIRT};
  std::vector<tfrag3::TFragmentTreeKind> ice_tfrags = {tfrag3::TFragmentTreeKind::ICE};
  auto sky_gpu_blender = std::make_shared<SkyBlendVulkanGPU>(m_device, m_vulkan_info);
  auto sky_cpu_blender = std::make_shared<SkyBlendCPU>(m_device, m_vulkan_info);

  //-------------
  // PRE TEXTURE
  //-------------
  // 0 : ??
  // 1 : ??
  // 2 : ??
  // 3 : SKY_DRAW
  init_bucket_renderer<SkyVulkanRenderer>("sky", BucketCategory::OTHER, BucketId::SKY_DRAW, m_device, m_vulkan_info);
  // 4 : OCEAN_MID_AND_FAR
  init_bucket_renderer<OceanVulkanMidAndFar>("ocean-mid-far", BucketCategory::OCEAN,
                                       BucketId::OCEAN_MID_AND_FAR, m_device, m_vulkan_info);

  //-----------------------
  // LEVEL 0 tfrag texture
  //-----------------------
  // 5 : TFRAG_TEX_LEVEL0
  init_bucket_renderer<VulkanTextureUploadHandler>("l0-tfrag-tex", BucketCategory::TEX,
                                             BucketId::TFRAG_TEX_LEVEL0, m_device, m_vulkan_info);
  // 6 : TFRAG_LEVEL0
  init_bucket_renderer<TFragmentVulkan>("l0-tfrag-tfrag", BucketCategory::TFRAG, BucketId::TFRAG_LEVEL0, m_device, m_vulkan_info,
                                  normal_tfrags, false, 0);
  // 7 : TFRAG_NEAR_LEVEL0
  // 8 : TIE_NEAR_LEVEL0
  // 9 : TIE_LEVEL0
  init_bucket_renderer<Tie3VulkanWithEnvmapJak1>("l0-tfrag-tie", BucketCategory::TIE,
                                           BucketId::TIE_LEVEL0, m_device, m_vulkan_info, 0);
  // 10 : MERC_TFRAG_TEX_LEVEL0
  init_bucket_renderer<MercVulkan2>("l0-tfrag-merc", BucketCategory::MERC,
                              BucketId::MERC_TFRAG_TEX_LEVEL0, m_device, m_vulkan_info);
  // 11 : GMERC_TFRAG_TEX_LEVEL0
  init_bucket_renderer<GenericVulkan2>("l0-tfrag-generic", BucketCategory::GENERIC,
                                 BucketId::GENERIC_TFRAG_TEX_LEVEL0, m_device, m_vulkan_info, 1500000, 10000, 10000, 800);

  //-----------------------
  // LEVEL 1 tfrag texture
  //-----------------------
  // 12 : TFRAG_TEX_LEVEL1
  init_bucket_renderer<VulkanTextureUploadHandler>("l1-tfrag-tex", BucketCategory::TEX,
                                             BucketId::TFRAG_TEX_LEVEL1, m_device, m_vulkan_info);
  // 13 : TFRAG_LEVEL1
  init_bucket_renderer<TFragmentVulkan>("l1-tfrag-tfrag", BucketCategory::TFRAG, BucketId::TFRAG_LEVEL1, m_device, m_vulkan_info,
                                  normal_tfrags, false, 1);
  // 14 : TFRAG_NEAR_LEVEL1
  // 15 : TIE_NEAR_LEVEL1
  // 16 : TIE_LEVEL1
  init_bucket_renderer<Tie3VulkanWithEnvmapJak1>("l1-tfrag-tie", BucketCategory::TIE,
                                           BucketId::TIE_LEVEL1, m_device, m_vulkan_info, 1);
  // 17 : MERC_TFRAG_TEX_LEVEL1
  init_bucket_renderer<MercVulkan2>("l1-tfrag-merc", BucketCategory::MERC,
                              BucketId::MERC_TFRAG_TEX_LEVEL1, m_device, m_vulkan_info);
  // 18 : GMERC_TFRAG_TEX_LEVEL1
  init_bucket_renderer<GenericVulkan2>("l1-tfrag-generic", BucketCategory::GENERIC,
                                 BucketId::GENERIC_TFRAG_TEX_LEVEL1, m_device, m_vulkan_info, 1500000, 10000, 10000, 800);

  //-----------------------
  // LEVEL 0 shrub texture
  //-----------------------
  // 19 : SHRUB_TEX_LEVEL0
  init_bucket_renderer<VulkanTextureUploadHandler>("l0-shrub-tex", BucketCategory::TEX,
                                             BucketId::SHRUB_TEX_LEVEL0, m_device, m_vulkan_info);
  // 20 : SHRUB_NORMAL_LEVEL0
  init_bucket_renderer<ShrubVulkan>("l0-shrub", BucketCategory::SHRUB, BucketId::SHRUB_NORMAL_LEVEL0, m_device, m_vulkan_info);
  // 21 : ???
  // 22 : SHRUB_BILLBOARD_LEVEL0
  // 23 : SHRUB_TRANS_LEVEL0
  // 24 : SHRUB_GENERIC_LEVEL0

  //-----------------------
  // LEVEL 1 shrub texture
  //-----------------------
  // 25 : SHRUB_TEX_LEVEL1
  init_bucket_renderer<VulkanTextureUploadHandler>("l1-shrub-tex", BucketCategory::TEX,
                                             BucketId::SHRUB_TEX_LEVEL1, m_device, m_vulkan_info);
  // 26 : SHRUB_NORMAL_LEVEL1
  init_bucket_renderer<ShrubVulkan>("l1-shrub", BucketCategory::SHRUB, BucketId::SHRUB_NORMAL_LEVEL1, m_device, m_vulkan_info);
  // 27 : ???
  // 28 : SHRUB_BILLBOARD_LEVEL1
  // 29 : SHRUB_TRANS_LEVEL1
  // 30 : SHRUB_GENERIC_LEVEL1
  init_bucket_renderer<GenericVulkan2>("mystery-generic", BucketCategory::GENERIC,
                                 BucketId::SHRUB_GENERIC_LEVEL1, m_device, m_vulkan_info);

  //-----------------------
  // LEVEL 0 alpha texture
  //-----------------------
  init_bucket_renderer<VulkanTextureUploadHandler>("l0-alpha-tex", BucketCategory::TEX,
                                             BucketId::ALPHA_TEX_LEVEL0, m_device, m_vulkan_info);  // 31
  init_bucket_renderer<SkyBlendVulkanHandler>("l0-alpha-sky-blend-and-tfrag-trans", BucketCategory::OTHER,
                                        BucketId::TFRAG_TRANS0_AND_SKY_BLEND_LEVEL0, m_device, m_vulkan_info, 0,
                                        sky_gpu_blender, sky_cpu_blender);  // 32
  // 33
  init_bucket_renderer<TFragmentVulkan>("l0-alpha-tfrag", BucketCategory::TFRAG,
                                  BucketId::TFRAG_DIRT_LEVEL0, m_device, m_vulkan_info, dirt_tfrags, false,
                                  0);  // 34
  // 35
  init_bucket_renderer<TFragmentVulkan>("l0-alpha-tfrag-ice", BucketCategory::TFRAG,
                                  BucketId::TFRAG_ICE_LEVEL0, m_device, m_vulkan_info, ice_tfrags, false, 0);
  // 37

  //-----------------------
  // LEVEL 1 alpha texture
  //-----------------------
  init_bucket_renderer<VulkanTextureUploadHandler>("l1-alpha-tex", BucketCategory::TEX,
                                             BucketId::ALPHA_TEX_LEVEL1, m_device, m_vulkan_info);  // 38
  init_bucket_renderer<SkyBlendVulkanHandler>("l1-alpha-sky-blend-and-tfrag-trans", BucketCategory::OTHER,
                                        BucketId::TFRAG_TRANS1_AND_SKY_BLEND_LEVEL1, m_device, m_vulkan_info, 1,
                                        sky_gpu_blender, sky_cpu_blender);  // 39
  // 40
  init_bucket_renderer<TFragmentVulkan>("l1-alpha-tfrag-dirt", BucketCategory::TFRAG,
                                  BucketId::TFRAG_DIRT_LEVEL1, m_device, m_vulkan_info, dirt_tfrags, false,
                                  1);  // 41
  // 42
  init_bucket_renderer<TFragmentVulkan>("l1-alpha-tfrag-ice", BucketCategory::TFRAG,
                                  BucketId::TFRAG_ICE_LEVEL1, m_device, m_vulkan_info, ice_tfrags, false, 1);
  // 44

  init_bucket_renderer<MercVulkan2>("common-alpha-merc", BucketCategory::MERC,
                              BucketId::MERC_AFTER_ALPHA, m_device, m_vulkan_info);

  init_bucket_renderer<GenericVulkan2>("common-alpha-generic", BucketCategory::GENERIC,
                                 BucketId::GENERIC_ALPHA, m_device, m_vulkan_info);                                  // 46
  init_bucket_renderer<ShadowVulkanRenderer>("shadow", BucketCategory::OTHER, BucketId::SHADOW, m_device, m_vulkan_info);  // 47

  //-----------------------
  // LEVEL 0 pris texture
  //-----------------------
  init_bucket_renderer<VulkanTextureUploadHandler>("l0-pris-tex", BucketCategory::TEX,
                                             BucketId::PRIS_TEX_LEVEL0, m_device, m_vulkan_info);  // 48
  init_bucket_renderer<MercVulkan2>("l0-pris-merc", BucketCategory::MERC,
                              BucketId::MERC_PRIS_LEVEL0, m_device, m_vulkan_info);  // 49
  init_bucket_renderer<GenericVulkan2>("l0-pris-generic", BucketCategory::GENERIC,
                                 BucketId::GENERIC_PRIS_LEVEL0, m_device, m_vulkan_info);  // 50

  //-----------------------
  // LEVEL 1 pris texture
  //-----------------------
  init_bucket_renderer<VulkanTextureUploadHandler>("l1-pris-tex", BucketCategory::TEX,
                                             BucketId::PRIS_TEX_LEVEL1, m_device, m_vulkan_info);  // 51
  init_bucket_renderer<MercVulkan2>("l1-pris-merc", BucketCategory::MERC,
                              BucketId::MERC_PRIS_LEVEL1, m_device, m_vulkan_info);  // 52
  init_bucket_renderer<GenericVulkan2>("l1-pris-generic", BucketCategory::GENERIC,
                                 BucketId::GENERIC_PRIS_LEVEL1, m_device, m_vulkan_info);  // 53

  // other renderers may output to the eye renderer
  m_render_state.eye_renderer = init_bucket_renderer<EyeVulkanRenderer>(
      "common-pris-eyes", BucketCategory::OTHER, BucketId::MERC_EYES_AFTER_PRIS, m_device, m_vulkan_info);  // 54

  // hack: set to merc2 for debugging
  init_bucket_renderer<MercVulkan2>("common-pris-merc", BucketCategory::MERC,
                              BucketId::MERC_AFTER_PRIS, m_device, m_vulkan_info);  // 55
  init_bucket_renderer<GenericVulkan2>("common-pris-generic", BucketCategory::GENERIC,
                                 BucketId::GENERIC_PRIS, m_device, m_vulkan_info);  // 56

  //-----------------------
  // LEVEL 0 water texture
  //-----------------------
  init_bucket_renderer<VulkanTextureUploadHandler>("l0-water-tex", BucketCategory::TEX,
                                             BucketId::WATER_TEX_LEVEL0, m_device, m_vulkan_info);  // 57
  init_bucket_renderer<MercVulkan2>("l0-water-merc", BucketCategory::MERC,
                              BucketId::MERC_WATER_LEVEL0, m_device, m_vulkan_info);  // 58
  init_bucket_renderer<GenericVulkan2>("l0-water-generic", BucketCategory::GENERIC,
                                 BucketId::GENERIC_WATER_LEVEL0, m_device, m_vulkan_info);  // 59

  //-----------------------
  // LEVEL 1 water texture
  //-----------------------
  init_bucket_renderer<VulkanTextureUploadHandler>("l1-water-tex", BucketCategory::TEX,
                                             BucketId::WATER_TEX_LEVEL1, m_device, m_vulkan_info);  // 60
  init_bucket_renderer<MercVulkan2>("l1-water-merc", BucketCategory::MERC,
                              BucketId::MERC_WATER_LEVEL1, m_device, m_vulkan_info);  // 61
  init_bucket_renderer<GenericVulkan2>("l1-water-generic", BucketCategory::GENERIC,
                                 BucketId::GENERIC_WATER_LEVEL1, m_device, m_vulkan_info);  // 62

  init_bucket_renderer<OceanNearVulkan>("ocean-near", BucketCategory::OCEAN, BucketId::OCEAN_NEAR, m_device, m_vulkan_info);  // 63

  //-----------------------
  // DEPTH CUE
  //-----------------------
  init_bucket_renderer<DepthCueVulkan>("depth-cue", BucketCategory::OTHER, BucketId::DEPTH_CUE, m_device, m_vulkan_info);  // 64

  //-----------------------
  // COMMON texture
  //-----------------------
  init_bucket_renderer<VulkanTextureUploadHandler>("common-tex", BucketCategory::TEX,
                                             BucketId::PRE_SPRITE_TEX, m_device, m_vulkan_info);  // 65

  // the first renderer added will be the default for sprite.
  init_bucket_renderer<SpriteVulkan3>("sprite-3", BucketCategory::SPRITE, BucketId::SPRITE, m_device,
                                      m_vulkan_info);

  init_bucket_renderer<DirectVulkanRenderer>("debug", BucketCategory::OTHER, BucketId::DEBUG, m_device, m_vulkan_info, 0x20000);
  init_bucket_renderer<DirectVulkanRenderer>("debug-no-zbuf", BucketCategory::OTHER,
                                       BucketId::DEBUG_NO_ZBUF, m_device, m_vulkan_info, 0x8000);
  // an extra custom bucket!
  init_bucket_renderer<DirectVulkanRenderer>("subtitle", BucketCategory::OTHER, BucketId::SUBTITLE, m_device, m_vulkan_info, 6000);

  // for now, for any unset renderers, just set them to an EmptyBucketRenderer.
  for (size_t i = 0; i < m_bucket_renderers.size(); i++) {
    if (!m_bucket_renderers[i]) {
      init_bucket_renderer<EmptyBucketVulkanRenderer>(fmt::format("bucket{}", i), BucketCategory::OTHER,
                                                (BucketId)i, m_device, m_vulkan_info);
    }

    m_graphics_bucket_renderers[i]->init_shaders(m_vulkan_info.shaders);
    m_graphics_bucket_renderers[i]->init_textures(*m_vulkan_info.texture_pool);
  }
  sky_cpu_blender->init_textures(*m_vulkan_info.texture_pool);
  sky_gpu_blender->init_textures(*m_vulkan_info.texture_pool);
  m_vulkan_info.loader->load_common(*m_vulkan_info.texture_pool, "GAME");
}

void VulkanRenderer::init_bucket_renderers_jak2() {
  using namespace jak2;
  m_bucket_renderers.resize((int)BucketId::MAX_BUCKETS);
  m_graphics_bucket_renderers.resize((int)BucketId::MAX_BUCKETS);
  m_bucket_categories.resize((int)BucketId::MAX_BUCKETS, BucketCategory::OTHER);
  // 0
  init_bucket_renderer<VisDataVulkanHandler>("vis", BucketCategory::OTHER, BucketId::BUCKET_2, m_device, m_vulkan_info);
  init_bucket_renderer<VulkanTextureUploadHandler>("tex-lcom-sky-pre", BucketCategory::TEX, BucketId::TEX_LCOM_SKY_PRE, m_device, m_vulkan_info);
  init_bucket_renderer<DirectVulkanRenderer>("sky-draw", BucketCategory::OTHER, BucketId::SKY_DRAW,
                                             m_device, m_vulkan_info, 1024);
  init_bucket_renderer<OceanVulkanMidAndFar>("ocean-mid-far", BucketCategory::OCEAN,
                                             BucketId::OCEAN_MID_FAR, m_device, m_vulkan_info);

  for (int i = 0; i < 6; ++i) {
#define GET_BUCKET_ID_FOR_LIST(bkt1, bkt2, idx) ((int)(bkt1) + ((int)(bkt2) - (int)(bkt1)) * (idx))
    init_bucket_renderer<VulkanTextureUploadHandler>(
        fmt::format("tex-l{}-tfrag", i), BucketCategory::TEX,
        GET_BUCKET_ID_FOR_LIST(BucketId::TEX_L0_TFRAG, BucketId::TEX_L1_TFRAG, i), m_device,
        m_vulkan_info);
    init_bucket_renderer<TFragmentVulkan>(
        fmt::format("tfrag-l{}-tfrag", i), BucketCategory::TFRAG,
        GET_BUCKET_ID_FOR_LIST(BucketId::TFRAG_L0_TFRAG, BucketId::TFRAG_L1_TFRAG, i), m_device,
        m_vulkan_info, std::vector{tfrag3::TFragmentTreeKind::NORMAL}, false, i);
    Tie3Vulkan* tie = init_bucket_renderer<Tie3Vulkan>(
        fmt::format("tie-l{}-tfrag", i), BucketCategory::TIE,
        GET_BUCKET_ID_FOR_LIST(BucketId::TIE_L0_TFRAG, BucketId::TIE_L1_TFRAG, i), m_device,
        m_vulkan_info, i);
    init_bucket_renderer<Tie3VulkanAnotherCategory>(
        fmt::format("etie-l{}-tfrag", i), BucketCategory::TIE,
        GET_BUCKET_ID_FOR_LIST(BucketId::ETIE_L0_TFRAG, BucketId::ETIE_L1_TFRAG, i), m_device,
        m_vulkan_info, tie, tfrag3::TieCategory::NORMAL_ENVMAP);
    init_bucket_renderer<MercVulkan2>(
        fmt::format("merc-l{}-tfrag", i), BucketCategory::MERC,
        GET_BUCKET_ID_FOR_LIST(BucketId::MERC_L0_TFRAG, BucketId::MERC_L1_TFRAG, i), m_device,
        m_vulkan_info);

    init_bucket_renderer<VulkanTextureUploadHandler>(
        fmt::format("tex-l{}-shrub", i), BucketCategory::TEX,
        GET_BUCKET_ID_FOR_LIST(BucketId::TEX_L0_SHRUB, BucketId::TEX_L1_SHRUB, i), m_device,
        m_vulkan_info);
    init_bucket_renderer<ShrubVulkan>(
        fmt::format("shrub-l{}-shrub", i), BucketCategory::SHRUB,
        GET_BUCKET_ID_FOR_LIST(BucketId::SHRUB_L0_SHRUB, BucketId::SHRUB_L1_SHRUB, i), m_device,
        m_vulkan_info);
    init_bucket_renderer<MercVulkan2>(
        fmt::format("merc-l{}-shrub", i), BucketCategory::MERC,
        GET_BUCKET_ID_FOR_LIST(BucketId::MERC_L0_SHRUB, BucketId::MERC_L1_SHRUB, i), m_device,
        m_vulkan_info);

    init_bucket_renderer<VulkanTextureUploadHandler>(
        fmt::format("tex-l{}-alpha", i), BucketCategory::TEX,
        GET_BUCKET_ID_FOR_LIST(BucketId::TEX_L0_ALPHA, BucketId::TEX_L1_ALPHA, i), m_device,
        m_vulkan_info);
    init_bucket_renderer<TFragmentVulkan>(
        fmt::format("tfrag-t-l{}-alpha", i), BucketCategory::TFRAG,
        GET_BUCKET_ID_FOR_LIST(BucketId::TFRAG_T_L0_ALPHA, BucketId::TFRAG_T_L1_ALPHA, i), m_device,
        m_vulkan_info, std::vector{tfrag3::TFragmentTreeKind::TRANS}, false, i);
    init_bucket_renderer<Tie3VulkanAnotherCategory>(
        fmt::format("tie-t-l{}-alpha", i), BucketCategory::TIE,
        GET_BUCKET_ID_FOR_LIST(BucketId::TIE_T_L0_ALPHA, BucketId::TIE_T_L1_ALPHA, i), m_device,
        m_vulkan_info, tie, tfrag3::TieCategory::TRANS);
    init_bucket_renderer<Tie3VulkanAnotherCategory>(
        fmt::format("etie-t-l{}-alpha", i), BucketCategory::TIE,
        GET_BUCKET_ID_FOR_LIST(BucketId::ETIE_T_L0_ALPHA, BucketId::ETIE_T_L1_ALPHA, i), m_device,
        m_vulkan_info, tie,
        tfrag3::TieCategory::TRANS_ENVMAP);
    init_bucket_renderer<MercVulkan2>(
        fmt::format("merc-l{}-alpha", i), BucketCategory::MERC,
        GET_BUCKET_ID_FOR_LIST(BucketId::MERC_L0_ALPHA, BucketId::MERC_L1_ALPHA, i), m_device, m_vulkan_info);

    init_bucket_renderer<VulkanTextureUploadHandler>(
        fmt::format("tex-l{}-pris", i), BucketCategory::TEX,
        GET_BUCKET_ID_FOR_LIST(BucketId::TEX_L0_PRIS, BucketId::TEX_L1_PRIS, i), m_device,
        m_vulkan_info);
    init_bucket_renderer<MercVulkan2>(
        fmt::format("merc-l{}-pris", i), BucketCategory::MERC,
        GET_BUCKET_ID_FOR_LIST(BucketId::MERC_L0_PRIS, BucketId::MERC_L1_PRIS, i), m_device,
        m_vulkan_info);

    init_bucket_renderer<VulkanTextureUploadHandler>(
        fmt::format("tex-l{}-pris2", i), BucketCategory::TEX,
        GET_BUCKET_ID_FOR_LIST(BucketId::TEX_L0_PRIS2, BucketId::TEX_L1_PRIS2, i), m_device,
        m_vulkan_info);
    init_bucket_renderer<MercVulkan2>(
        fmt::format("merc-l{}-pris2", i), BucketCategory::MERC,
        GET_BUCKET_ID_FOR_LIST(BucketId::MERC_L0_PRIS2, BucketId::MERC_L1_PRIS2, i), m_device,
        m_vulkan_info);

    init_bucket_renderer<VulkanTextureUploadHandler>(
        fmt::format("tex-l{}-water", i), BucketCategory::TEX,
        GET_BUCKET_ID_FOR_LIST(BucketId::TEX_L0_WATER, BucketId::TEX_L1_WATER, i), m_device,
        m_vulkan_info);
    init_bucket_renderer<MercVulkan2>(
        fmt::format("merc-l{}-water", i), BucketCategory::MERC,
        GET_BUCKET_ID_FOR_LIST(BucketId::MERC_L0_WATER, BucketId::MERC_L1_WATER, i), m_device,
        m_vulkan_info);
    init_bucket_renderer<TFragmentVulkan>(
        fmt::format("tfrag-w-l{}-alpha", i), BucketCategory::TFRAG,
        GET_BUCKET_ID_FOR_LIST(BucketId::TFRAG_W_L0_WATER, BucketId::TFRAG_W_L1_WATER, i), m_device,
        m_vulkan_info, std::vector{tfrag3::TFragmentTreeKind::WATER}, false, i);
    init_bucket_renderer<Tie3VulkanAnotherCategory>(
        fmt::format("tie-w-l{}-water", i), BucketCategory::TIE,
        GET_BUCKET_ID_FOR_LIST(BucketId::TIE_W_L0_WATER, BucketId::TIE_W_L1_WATER, i), m_device,
        m_vulkan_info, tie,
        tfrag3::TieCategory::WATER);
    init_bucket_renderer<Tie3VulkanAnotherCategory>(
        fmt::format("etie-w-l{}-water", i), BucketCategory::TIE,
        GET_BUCKET_ID_FOR_LIST(BucketId::ETIE_W_L0_WATER, BucketId::ETIE_W_L1_WATER, i), m_device,
        m_vulkan_info, tie,
        tfrag3::TieCategory::WATER_ENVMAP);
#undef GET_BUCKET_ID_FOR_LIST
  }
  // 180
  init_bucket_renderer<VulkanTextureUploadHandler>("tex-lcom-tfrag", BucketCategory::TEX, BucketId::TEX_LCOM_TFRAG, m_device, m_vulkan_info);
  init_bucket_renderer<MercVulkan2>("merc-lcom-tfrag", BucketCategory::MERC, BucketId::MERC_LCOM_TFRAG, m_device, m_vulkan_info);
  // 190
  init_bucket_renderer<VulkanTextureUploadHandler>("tex-lcom-shrub", BucketCategory::TEX, BucketId::TEX_LCOM_SHRUB, m_device, m_vulkan_info);
  init_bucket_renderer<MercVulkan2>("merc-lcom-shrub", BucketCategory::MERC, BucketId::MERC_LCOM_SHRUB, m_device, m_vulkan_info);
  init_bucket_renderer<VulkanTextureUploadHandler>("tex-l0-pris", BucketCategory::TEX,
                                                   BucketId::TEX_L0_PRIS, m_device, m_vulkan_info);
  init_bucket_renderer<MercVulkan2>("merc-l0-pris", BucketCategory::MERC, BucketId::MERC_L0_PRIS, m_device, m_vulkan_info);
  // 200
  init_bucket_renderer<VulkanTextureUploadHandler>("tex-l1-pris", BucketCategory::TEX,
                                             BucketId::TEX_L1_PRIS, m_device, m_vulkan_info);
  init_bucket_renderer<MercVulkan2>("merc-l1-pris", BucketCategory::MERC, BucketId::MERC_L1_PRIS, m_device, m_vulkan_info);
  init_bucket_renderer<VulkanTextureUploadHandler>("tex-l2-pris", BucketCategory::TEX,
                                                   BucketId::TEX_L2_PRIS, m_device, m_vulkan_info);
  init_bucket_renderer<MercVulkan2>("merc-l2-pris", BucketCategory::MERC, BucketId::MERC_L2_PRIS, m_device, m_vulkan_info);
  init_bucket_renderer<VulkanTextureUploadHandler>("tex-l3-pris", BucketCategory::TEX,
                                                   BucketId::TEX_L3_PRIS, m_device, m_vulkan_info);
  // 210
  init_bucket_renderer<VulkanTextureUploadHandler>("tex-l4-pris", BucketCategory::TEX,
                                             BucketId::TEX_L4_PRIS, m_device, m_vulkan_info);
  init_bucket_renderer<MercVulkan2>("merc-l4-pris", BucketCategory::MERC, BucketId::MERC_L4_PRIS, m_device, m_vulkan_info);
  init_bucket_renderer<VulkanTextureUploadHandler>("tex-l5-pris", BucketCategory::TEX,
                                             BucketId::TEX_L5_PRIS, m_device, m_vulkan_info);
  init_bucket_renderer<MercVulkan2>("merc-l5-pris", BucketCategory::MERC, BucketId::MERC_L5_PRIS, m_device, m_vulkan_info);
  // 220
  init_bucket_renderer<VulkanTextureUploadHandler>("tex-lcom-pris", BucketCategory::TEX,
                                             BucketId::TEX_LCOM_PRIS, m_device, m_vulkan_info);
  init_bucket_renderer<MercVulkan2>("merc-lcom-pris", BucketCategory::MERC, BucketId::MERC_LCOM_PRIS, m_device, m_vulkan_info);
  init_bucket_renderer<VulkanTextureUploadHandler>("tex-l0-pris2", BucketCategory::TEX,
                                             BucketId::TEX_L0_PRIS2, m_device, m_vulkan_info);
  init_bucket_renderer<MercVulkan2>("merc-l0-pris2", BucketCategory::MERC, BucketId::MERC_L0_PRIS2, m_device, m_vulkan_info);
  init_bucket_renderer<VulkanTextureUploadHandler>("tex-l1-pris2", BucketCategory::TEX,
                                             BucketId::TEX_L1_PRIS2, m_device, m_vulkan_info);
  init_bucket_renderer<MercVulkan2>("merc-l1-pris2", BucketCategory::MERC, BucketId::MERC_L1_PRIS2, m_device, m_vulkan_info);
  // 230
  init_bucket_renderer<VulkanTextureUploadHandler>("tex-l2-pris2", BucketCategory::TEX,
                                             BucketId::TEX_L2_PRIS2, m_device, m_vulkan_info);
  init_bucket_renderer<MercVulkan2>("merc-l2-pris2", BucketCategory::MERC, BucketId::MERC_L2_PRIS2, m_device, m_vulkan_info);
  init_bucket_renderer<VulkanTextureUploadHandler>("tex-l3-pris2", BucketCategory::TEX,
                                             BucketId::TEX_L3_PRIS2, m_device, m_vulkan_info);
  init_bucket_renderer<MercVulkan2>("merc-l3-pris2", BucketCategory::MERC, BucketId::MERC_L3_PRIS2, m_device, m_vulkan_info);
  // 240
  init_bucket_renderer<VulkanTextureUploadHandler>("tex-l4-pris2", BucketCategory::TEX,
                                             BucketId::TEX_L4_PRIS2, m_device, m_vulkan_info);
  init_bucket_renderer<MercVulkan2>("merc-l4-pris2", BucketCategory::MERC, BucketId::MERC_L4_PRIS2, m_device, m_vulkan_info);
  init_bucket_renderer<VulkanTextureUploadHandler>("tex-l5-pris2", BucketCategory::TEX,
                                             BucketId::TEX_L5_PRIS2, m_device, m_vulkan_info);
  init_bucket_renderer<MercVulkan2>("merc-l5-pris2", BucketCategory::MERC, BucketId::MERC_L5_PRIS2, m_device, m_vulkan_info);
  // 250
  init_bucket_renderer<VulkanTextureUploadHandler>("tex-l0-water", BucketCategory::TEX,
                                                   BucketId::TEX_L0_WATER, m_device, m_vulkan_info);
  init_bucket_renderer<MercVulkan2>("merc-l0-water", BucketCategory::MERC, BucketId::MERC_L0_WATER, m_device, m_vulkan_info);
  init_bucket_renderer<TFragmentVulkan>("tfrag-w-l0-alpha", BucketCategory::TFRAG, BucketId::TFRAG_W_L0_WATER, m_device,
      m_vulkan_info, std::vector{tfrag3::TFragmentTreeKind::WATER}, false, 0);
  // 260
  init_bucket_renderer<VulkanTextureUploadHandler>("tex-l1-water", BucketCategory::TEX,
                                             BucketId::TEX_L1_WATER, m_device, m_vulkan_info);
  init_bucket_renderer<MercVulkan2>("merc-l1-water", BucketCategory::MERC, BucketId::MERC_L1_WATER, m_device, m_vulkan_info);
  init_bucket_renderer<TFragmentVulkan>("tfrag-w-l1-alpha", BucketCategory::TFRAG,
                                  BucketId::TFRAG_W_L1_WATER, m_device, m_vulkan_info,
                                  std::vector{tfrag3::TFragmentTreeKind::WATER}, false, 1);
  // 270
  init_bucket_renderer<VulkanTextureUploadHandler>("tex-l2-water", BucketCategory::TEX,
                                             BucketId::TEX_L2_WATER, m_device, m_vulkan_info);
  init_bucket_renderer<MercVulkan2>("merc-l2-water", BucketCategory::MERC, BucketId::MERC_L2_WATER, m_device, m_vulkan_info);
  init_bucket_renderer<TFragmentVulkan>("tfrag-w-l2-alpha", BucketCategory::TFRAG,
                                  BucketId::TFRAG_W_L2_WATER, m_device, m_vulkan_info,
                                  std::vector{tfrag3::TFragmentTreeKind::WATER}, false, 2);
  init_bucket_renderer<VulkanTextureUploadHandler>("tex-l3-water", BucketCategory::TEX,
                                             BucketId::TEX_L3_WATER, m_device, m_vulkan_info);
  // 280
  init_bucket_renderer<MercVulkan2>("merc-l3-water", BucketCategory::MERC, BucketId::MERC_L3_WATER, m_device, m_vulkan_info);
  init_bucket_renderer<TFragmentVulkan>("tfrag-w-l3-water", BucketCategory::TFRAG,
                                  BucketId::TFRAG_W_L3_WATER, m_device, m_vulkan_info,
                                  std::vector{tfrag3::TFragmentTreeKind::WATER}, false, 3);
  init_bucket_renderer<VulkanTextureUploadHandler>("tex-l4-water", BucketCategory::TEX,
                                             BucketId::TEX_L4_WATER, m_device, m_vulkan_info);
  init_bucket_renderer<MercVulkan2>("merc-l4-water", BucketCategory::MERC, BucketId::MERC_L4_WATER, m_device, m_vulkan_info);
  // 290
  init_bucket_renderer<VulkanTextureUploadHandler>("tex-l5-water", BucketCategory::TEX,
                                             BucketId::TEX_L5_WATER, m_device, m_vulkan_info);
  // 300
  init_bucket_renderer<TFragmentVulkan>("tfrag-w-l5-water", BucketCategory::TFRAG,
                                  BucketId::TFRAG_W_L5_WATER, m_device, m_vulkan_info,
                                  std::vector{tfrag3::TFragmentTreeKind::WATER}, false, 5);
  init_bucket_renderer<VulkanTextureUploadHandler>("tex-lcom-water", BucketCategory::TEX, BucketId::TEX_LCOM_WATER, m_device, m_vulkan_info);
  init_bucket_renderer<MercVulkan2>("merc-lcom-water", BucketCategory::MERC, BucketId::MERC_LCOM_WATER, m_device, m_vulkan_info);
  init_bucket_renderer<VulkanTextureUploadHandler>("tex-lcom-sky-post", BucketCategory::TEX,
                                                   BucketId::TEX_LCOM_SKY_POST, m_device,
                                                   m_vulkan_info);
  // 310
  init_bucket_renderer<OceanNearVulkan>("ocean-near", BucketCategory::OCEAN, BucketId::OCEAN_NEAR, m_device, m_vulkan_info);
  init_bucket_renderer<VulkanTextureUploadHandler>("tex-all-sprite", BucketCategory::TEX, BucketId::TEX_ALL_SPRITE, m_device, m_vulkan_info);
  init_bucket_renderer<SpriteVulkan3>("particles", BucketCategory::SPRITE, BucketId::PARTICLES, m_device,
                                m_vulkan_info);
  init_bucket_renderer<LightningVulkanRenderer>("lightning", BucketCategory::OTHER, BucketId::EFFECTS, m_device, m_vulkan_info);
  init_bucket_renderer<VulkanTextureUploadHandler>("tex-all-warp", BucketCategory::TEX,
                                                   BucketId::TEX_ALL_WARP, m_device, m_vulkan_info);
  init_bucket_renderer<DirectVulkanRenderer>("debug-no-zbuf1", BucketCategory::OTHER,
                                             BucketId::DEBUG_NO_ZBUF1, m_device,
                                             m_vulkan_info, 0x8000);
  init_bucket_renderer<VulkanTextureUploadHandler>("tex-all-map", BucketCategory::TEX,
                                                   BucketId::TEX_ALL_MAP, m_device, m_vulkan_info);
  // 320
  init_bucket_renderer<ProgressVulkanRenderer>("progress", BucketCategory::OTHER, BucketId::PROGRESS, m_device, m_vulkan_info,
                                       0x8000);
  init_bucket_renderer<DirectVulkanRenderer>("screen-filter", BucketCategory::OTHER,
                                             BucketId::SCREEN_FILTER, m_device, m_vulkan_info,
                                             256);
  init_bucket_renderer<DirectVulkanRenderer>("bucket-322", BucketCategory::OTHER, BucketId::SUBTITLE, m_device, m_vulkan_info,
                                       0x8000);
  init_bucket_renderer<DirectVulkanRenderer>("debug2", BucketCategory::OTHER, BucketId::DEBUG2,
                                             m_device, m_vulkan_info, 0x8000);
  init_bucket_renderer<DirectVulkanRenderer>("debug-no-zbuf2", BucketCategory::OTHER,
                                             BucketId::DEBUG_NO_ZBUF2, m_device,
                                             m_vulkan_info, 0x8000);
  init_bucket_renderer<DirectVulkanRenderer>("debug3", BucketCategory::OTHER, BucketId::DEBUG3,
                                             m_device, m_vulkan_info, 0x8000);

  auto eye_renderer = std::make_unique<EyeVulkanRenderer>("eyes", 0, m_device, m_vulkan_info);
  m_render_state.eye_renderer = eye_renderer.get();
  m_jak2_eye_renderer = std::move(eye_renderer);

  // for now, for any unset renderers, just set them to an EmptyBucketRenderer.
  for (size_t i = 0; i < m_bucket_renderers.size(); i++) {
    if (!m_bucket_renderers[i]) {
      init_bucket_renderer<EmptyBucketVulkanRenderer>(fmt::format("bucket{}", i), BucketCategory::OTHER, i, m_device, m_vulkan_info);
    }

    m_graphics_bucket_renderers[i]->init_shaders(m_vulkan_info.shaders);
    m_graphics_bucket_renderers[i]->init_textures(*m_vulkan_info.texture_pool);
  }
  m_vulkan_info.loader->load_common(*m_vulkan_info.texture_pool, "GAME");
}


/*!
 * Main render function. This is called from the gfx loop with the chain passed from the game.
 */
void VulkanRenderer::render(DmaFollower dma, const RenderOptions& settings) {
  m_profiler.clear();
  m_render_state.reset();
  m_render_state.ee_main_memory = g_ee_main_mem;
  m_render_state.offset_of_s7 = offset_of_s7();

  {
    auto prof = m_profiler.root()->make_scoped_child("frame-setup");
    setup_frame(settings);
    if (settings.gpu_sync) {
      //glFinish();
      vkQueueWaitIdle(m_device->graphicsQueue());  // TODO: Verify that this is correct
    }
  }

  {
    auto prof = m_profiler.root()->make_scoped_child("loader");
    if (m_last_pmode_alp == 0 && settings.pmode_alp_register != 0 && m_enable_fast_blackout_loads) {
      // blackout, load everything and don't worry about frame rate
      m_vulkan_info.loader->update_blocking(*m_vulkan_info.texture_pool);

    } else {
      m_vulkan_info.loader->update(*m_vulkan_info.texture_pool);
    }
  }

  // render the buckets!
  {
    auto prof = m_profiler.root()->make_scoped_child("buckets");
    dispatch_buckets(dma, prof, settings.gpu_sync);
  }

  // apply effects done with PCRTC registers
  {
    auto prof = m_profiler.root()->make_scoped_child("pcrtc");
    do_pcrtc_effects(settings.pmode_alp_register, &m_render_state, prof);
    if (settings.gpu_sync) {
      //glFinish();
      vkQueueWaitIdle(m_device->graphicsQueue());  // TODO: Verify that this is correct
    }
  }

  if (settings.draw_render_debug_window) {
    auto prof = m_profiler.root()->make_scoped_child("render-window");
    draw_renderer_selection_window();
    // add a profile bar for the imgui stuff
    //vif_interrupt_callback(0);
    if (settings.gpu_sync) {
      vkQueueWaitIdle(m_device->graphicsQueue()); //TODO: Verify that this is correct
    }
  }

  m_last_pmode_alp = settings.pmode_alp_register;

  if (settings.draw_loader_window) {
    m_vulkan_info.loader->draw_debug_window();
  }

  m_profiler.finish();
  if (settings.draw_profiler_window) {
    m_profiler.draw();
  }

  //  if (m_profiler.root_time() > 0.018) {
  //    fmt::print("Slow frame: {:.2f} ms\n", m_profiler.root_time() * 1000);
  //    fmt::print("{}\n", m_profiler.to_string());
  //  }

  if (settings.draw_small_profiler_window) {
    SmallProfilerStats stats;
    stats.draw_calls = m_profiler.root()->stats().draw_calls;
    stats.triangles = m_profiler.root()->stats().triangles;
    for (int i = 0; i < (int)BucketCategory::MAX_CATEGORIES; i++) {
      stats.time_per_category[i] = m_category_times[i];
    }
    m_small_profiler.draw(m_render_state.load_status_debug, stats);
  }

  if (settings.draw_subtitle_editor_window) {
    m_subtitle_editor.draw_window();
  }

  if (settings.save_screenshot) {
    finish_screenshot(settings.screenshot_path, settings.game_res_w, settings.game_res_h, 0, 0);
  }
}

/*!
 * Draw the per-renderer debug window
 */
void VulkanRenderer::draw_renderer_selection_window() {
  ImGui::Begin("Renderer Debug");

  ImGui::Checkbox("Use old single-draw", &m_render_state.no_multidraw);
  ImGui::SliderFloat("Fog Adjust", &m_render_state.fog_intensity, 0, 10);
  ImGui::Checkbox("Sky CPU", &m_render_state.use_sky_cpu);
  ImGui::Checkbox("Occlusion Cull", &m_render_state.use_occlusion_culling);
  ImGui::Checkbox("Blackout Loads", &m_enable_fast_blackout_loads);

  for (size_t i = 0; i < m_bucket_renderers.size(); i++) {
    auto renderer = m_bucket_renderers[i].get();
    if (renderer && !renderer->empty()) {
      ImGui::PushID(i);
      if (ImGui::TreeNode(renderer->name_and_id().c_str())) {
        ImGui::Checkbox("Enable", &renderer->enabled());
        renderer->draw_debug_window();
        ImGui::TreePop();
      }
      ImGui::PopID();
    }
  }
  if (ImGui::TreeNode("Texture Pool")) {
    m_vulkan_info.texture_pool->draw_debug_window();
    ImGui::TreePop();
  }
  if (m_jak2_eye_renderer) {
    if (ImGui::TreeNode("Eyes")) {
      m_jak2_eye_renderer->draw_debug_window();
      ImGui::TreePop();
    }
  }
  ImGui::End();
}

/*!
 * Pre-render frame setup.
 */
void VulkanRenderer::setup_frame(const RenderOptions& settings) {
  // glfw controls the window framebuffer, so we just update the size:
  bool window_changed = m_vulkan_info.swap_chain->width() != settings.window_framebuffer_width ||
                        m_vulkan_info.swap_chain->height() != settings.window_framebuffer_height;
  bool msaa_changed = (m_device->getMsaaCount() != settings.msaa_samples);
  bool vsync_changed = false; //FIXME

  bool isValidWindow = true;

  if (window_changed) {
    m_extents.height = settings.window_framebuffer_height;
    m_extents.width = settings.window_framebuffer_width;
  }

  if (msaa_changed) { 
    m_device->setMsaaCount((VkSampleCountFlagBits)settings.msaa_samples);
  }

  if (msaa_changed || window_changed || vsync_changed) {
    recreateSwapChain(settings.gpu_sync);
  }

  ASSERT_MSG(settings.game_res_w > 0 && settings.game_res_h > 0,
             fmt::format("Bad viewport size from game_res: {}x{}\n", settings.game_res_w,
                         settings.game_res_h));

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthWriteEnable = VK_TRUE;
  
  // Note: could rely on sky renderer to clear depth and color, but this causes problems with
  // letterboxing

  // setup the draw region to letterbox later
  m_render_state.draw_region_w = settings.draw_region_width;
  m_render_state.draw_region_h = settings.draw_region_height;

  // center the letterbox
  m_render_state.draw_offset_x =
      (settings.window_framebuffer_width - m_render_state.draw_region_w) / 2;
  m_render_state.draw_offset_y =
      (settings.window_framebuffer_height - m_render_state.draw_region_h) / 2;

  if (settings.borderless_windows_hacks) {
    // pretend the framebuffer is 1 pixel shorter on borderless. fullscreen issues!
    // add one pixel of vertical letterbox on borderless to make up for extra line
    m_render_state.draw_offset_y++;
  }

  if (m_render_state.draw_region_w <= 0 || m_render_state.draw_region_h <= 0) {
    // trying to draw to 0 size region... opengl doesn't like this.
    m_render_state.draw_region_w = 640;
    m_render_state.draw_region_h = 480;
  }

  if (isValidWindow) {
    m_render_state.render_fb_x = m_render_state.draw_offset_x;
    m_render_state.render_fb_y = m_render_state.draw_offset_y;
    m_render_state.render_fb_w = m_render_state.draw_region_w;
    m_render_state.render_fb_h = m_render_state.draw_region_h;
    m_vulkan_info.swap_chain->setSwapChainOffsetExtent(
        {m_render_state.draw_offset_x, -1 * m_render_state.draw_offset_y}); //Y-axis is inverse in Vulkan compared to OpenGL
    m_vulkan_info.swap_chain->setSwapChainOffsetExtent(
        {m_render_state.draw_region_w, m_render_state.draw_region_h});
  } else {
    m_render_state.render_fb_x = 0;
    m_render_state.render_fb_y = 0;
    m_render_state.render_fb_w = settings.game_res_w;
    m_render_state.render_fb_h = settings.game_res_h;
    m_vulkan_info.swap_chain->setSwapChainOffsetExtent({0, 0});
    m_vulkan_info.swap_chain->setSwapChainOffsetExtent({settings.game_res_w, settings.game_res_h});
  }
}

/*!
 * This function finds buckets and dispatches them to the appropriate part.
 */
void VulkanRenderer::dispatch_buckets(DmaFollower dma,
                                      ScopedProfilerNode& prof,
                                      bool sync_after_buckets) {
  m_render_state.version = m_version;
  m_render_state.frame_idx++;
  switch (m_version) {
    case GameVersion::Jak1:
      dispatch_buckets_jak1(dma, prof, sync_after_buckets);
      break;
    case GameVersion::Jak2:
      dispatch_buckets_jak2(dma, prof, sync_after_buckets);
      break;
    default:
      ASSERT(false);
  }
}

/*!
 * This function finds buckets and dispatches them to the appropriate part.
 */
void VulkanRenderer::dispatch_buckets_jak1(DmaFollower dma,
                                      ScopedProfilerNode& prof,
                                      bool sync_after_buckets) {
  using namespace jak1;
  // The first thing the DMA chain should be a call to a common default-registers chain.
  // this chain resets the state of the GS. After this is buckets
  m_category_times.fill(0);

  m_render_state.buckets_base =
      dma.current_tag_offset() + 16;  // offset by 1 qw for the initial call
  m_render_state.next_bucket = m_render_state.buckets_base;

  // Find the default regs buffer
  auto initial_call_tag = dma.current_tag();
  ASSERT(initial_call_tag.kind == DmaTag::Kind::CALL);
  auto initial_call_default_regs = dma.read_and_advance();
  ASSERT(initial_call_default_regs.transferred_tag == 0);  // should be a nop.
  m_render_state.default_regs_buffer = dma.current_tag_offset();
  auto default_regs_tag = dma.current_tag();
  ASSERT(default_regs_tag.kind == DmaTag::Kind::CNT);
  ASSERT(default_regs_tag.qwc == 10);
  // TODO verify data in here.
  auto default_data = dma.read_and_advance();
  ASSERT(default_data.size_bytes > 148);
  memcpy(m_render_state.fog_color.data(), default_data.data + 144, 4);
  auto default_ret_tag = dma.current_tag();
  ASSERT(default_ret_tag.qwc == 0);
  ASSERT(default_ret_tag.kind == DmaTag::Kind::RET);
  dma.read_and_advance();

  // now we should point to the first bucket!
  ASSERT(dma.current_tag_offset() == m_render_state.next_bucket);
  m_render_state.next_bucket += 16;

  m_vulkan_info.render_command_buffer = beginFrame();
  m_vulkan_info.currentFrame = currentFrame;
  m_vulkan_info.swap_chain->clearFramebufferImage(currentFrame);
  m_vulkan_info.swap_chain->beginSwapChainRenderPass(m_vulkan_info.render_command_buffer, currentFrame);

  // loop over the buckets!
  for (int bucket_id = 0; bucket_id < (int)BucketId::MAX_BUCKETS; bucket_id++) {
    auto& renderer = m_bucket_renderers[bucket_id];
    auto& graphics_renderer = m_graphics_bucket_renderers[bucket_id];

    auto bucket_prof = prof.make_scoped_child(renderer->name_and_id());
    g_current_render = renderer->name_and_id();
    //lg::info("Render: %s start\n", g_current_render.c_str());
    graphics_renderer->render(dma, &m_render_state, bucket_prof);
    if (sync_after_buckets) {
      auto pp = scoped_prof("finish");
      vkQueueWaitIdle(m_device->graphicsQueue());  // TODO: Verify that this is correct
    }

    // lg::info("Render: {} end", g_current_render);
    //  should have ended at the start of the next chain
    ASSERT(dma.current_tag_offset() == m_render_state.next_bucket);
    m_render_state.next_bucket += 16;
    vif_interrupt_callback(bucket_id);
    m_category_times[(int)m_bucket_categories[bucket_id]] += bucket_prof.get_elapsed_time();

    // hack to draw the collision mesh in the middle the drawing
    if (bucket_id == (int)BucketId::ALPHA_TEX_LEVEL0 - 1 &&
        Gfx::g_global_settings.collision_enable) {
      auto p = prof.make_scoped_child("collision-draw");
      m_collide_renderer->render(&m_render_state, p);
    }
  }
  g_current_render = "";

  // TODO ending data.
  m_vulkan_info.swap_chain->endSwapChainRenderPass(m_vulkan_info.render_command_buffer);
  endFrame();

  vkDeviceWaitIdle(m_device->getLogicalDevice());
}

void VulkanRenderer::dispatch_buckets_jak2(DmaFollower dma,
                                           ScopedProfilerNode& prof,
                                           bool sync_after_buckets) {
  // The first thing the DMA chain should be a call to a common default-registers chain.
  // this chain resets the state of the GS. After this is buckets
  m_category_times.fill(0);

  m_render_state.buckets_base = dma.current_tag_offset();  // starts at 0 in jak 2
  m_render_state.next_bucket = m_render_state.buckets_base + 16;
  m_render_state.bucket_for_vis_copy = (int)jak2::BucketId::BUCKET_2;
  m_render_state.num_vis_to_copy = 6;

  m_vulkan_info.render_command_buffer = beginFrame();
  m_vulkan_info.swap_chain->beginSwapChainRenderPass(m_vulkan_info.render_command_buffer,
                                                     currentFrame);

  for (size_t bucket_id = 0; bucket_id < m_bucket_renderers.size(); bucket_id++) {
    auto& renderer = m_bucket_renderers[bucket_id];
    auto& graphics_renderer = m_graphics_bucket_renderers[bucket_id];

    auto bucket_prof = prof.make_scoped_child(renderer->name_and_id());
    g_current_render = renderer->name_and_id();
    //lg::info("Render: {} start", g_current_render);
    graphics_renderer->render(dma, &m_render_state, bucket_prof);
    if (sync_after_buckets) {
      auto pp = scoped_prof("finish");
      //glFinish();
      vkQueueWaitIdle(m_device->graphicsQueue());  // TODO: Verify that this is correct
    }

    // lg::info("Render: {} end", g_current_render);
    //  should have ended at the start of the next chain
    ASSERT(dma.current_tag_offset() == m_render_state.next_bucket);
    m_render_state.next_bucket += 16;
    vif_interrupt_callback(bucket_id + 1);
    m_category_times[(int)m_bucket_categories[bucket_id]] += bucket_prof.get_elapsed_time();
  }
  vif_interrupt_callback(m_bucket_renderers.size());

  m_vulkan_info.swap_chain->endSwapChainRenderPass(m_vulkan_info.render_command_buffer);
  endFrame();

  // TODO ending data.
}


void VulkanRenderer::finish_screenshot(const std::string& output_name,
                                       int width,
                                       int height,
                                       int x,
                                       int y) {
  VkDeviceSize device_memory_size = sizeof(u32) * width * height;
  std::vector<u32> buffer(width * height);

  VkImage srcImage = m_vulkan_info.swap_chain->getImage(currentImageIndex);

  StagingBuffer screenshotBuffer(m_device, device_memory_size, 1, VK_BUFFER_USAGE_TRANSFER_DST_BIT);

  if (screenshotBuffer.map() != VK_SUCCESS) {
    lg::error("Error can't get screenshot memory buffer");
  }

  m_device->transitionImageLayout(srcImage, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  m_device->copyImageToBuffer(srcImage, static_cast<uint32_t>(width),
                              static_cast<uint32_t>(height),
                              x, y, 1, screenshotBuffer.getBuffer());
  m_device->transitionImageLayout(srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

  VkDeviceSize memory_offset = sizeof(u32) * ((y * width) + x);

  void* data = screenshotBuffer.getMappedMemory();
  ::memcpy(buffer.data(), data, device_memory_size - memory_offset);
  screenshotBuffer.unmap();

  // flip upside down in place
  for (int h = 0; h < height / 2; h++) {
    for (int w = 0; w < width; w++) {
      std::swap(buffer[h * width + w], buffer[(height - h - 1) * width + w]);
    }
  }

  // set alpha. For some reason, image viewers do weird stuff with alpha.
  for (auto& px : buffer) {
    px |= 0xff000000;
  }
  file_util::write_rgba_png(output_name, buffer.data(), width, height);
}

void VulkanRenderer::do_pcrtc_effects(float alp,
                                      SharedVulkanRenderState* render_state,
                                      ScopedProfilerNode& prof) {
  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachment.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.blendConstants[0] = 0.0f;
  colorBlending.blendConstants[1] = 0.0f;
  colorBlending.blendConstants[2] = 0.0f;
  colorBlending.blendConstants[3] = 0.0f;

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;


  if (alp < 1) {
    depthStencil.depthTestEnable = VK_FALSE;
    colorBlendAttachment.blendEnable = VK_TRUE;

    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;  // Optional
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;  // Optional

    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    m_vulkan_info.swap_chain->setSwapChainOffsetExtent({0, 0});

    m_blackout_renderer->draw(Vector4f(0, 0, 0, 1.f - alp), render_state, prof);

    depthStencil.depthTestEnable = VK_TRUE;
  }
}

void VulkanRenderer::createCommandBuffers() {
  commandBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = m_device->getCommandPool();
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

  if (vkAllocateCommandBuffers(m_device->getLogicalDevice(), &allocInfo, commandBuffers.data()) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to allocate command buffers!");
  }
}

void VulkanRenderer::recreateSwapChain(bool vsyncEnabled) {
  while (m_extents.width == 0 || m_extents.height == 0) {
    glfwWaitEvents();
  }
  vkDeviceWaitIdle(m_device->getLogicalDevice());

  if (m_vulkan_info.swap_chain == nullptr) {
    m_vulkan_info.swap_chain = std::make_unique<SwapChain>(m_device, m_extents, vsyncEnabled);
  } else {
    std::shared_ptr<SwapChain> oldSwapChain = std::move(m_vulkan_info.swap_chain);
    m_vulkan_info.swap_chain =
        std::make_unique<SwapChain>(m_device, m_extents, vsyncEnabled, oldSwapChain);

    if (!oldSwapChain->compareSwapFormats(*m_vulkan_info.swap_chain.get())) {
      throw std::runtime_error("Swap chain image(or depth) format has changed!");
    }
  }
}

//TODO: Implement secondary command buffer so existing framebuffer doesn't get overriden
VkCommandBuffer VulkanRenderer::beginFrame() {
  assert(!isFrameStarted && "Can't call beginFrame while already in progress");

  auto result = m_vulkan_info.swap_chain->acquireNextImage(&currentImageIndex);
  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    recreateSwapChain(false);
    return nullptr;
  }

  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    throw std::runtime_error("failed to acquire swap chain image!");
  }

  isFrameStarted = true;

  auto commandBuffer = getCurrentCommandBuffer();
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

  if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
    throw std::runtime_error("failed to begin recording command buffer!");
  }
  return commandBuffer;
}

void VulkanRenderer::endFrame() {
  assert(isFrameStarted && "Can't call endFrame while frame is not in progress");
  auto commandBuffer = getCurrentCommandBuffer();
  if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
    throw std::runtime_error("failed to record command buffer!");
  }

  if(m_vulkan_info.swap_chain->submitCommandBuffers(&commandBuffer, &currentImageIndex) != VK_SUCCESS){
    throw std::runtime_error("failed to present swap chain image!");
  }

  isFrameStarted = false;
  currentFrame = (currentFrame + 1) % SwapChain::MAX_FRAMES_IN_FLIGHT;
}

void VulkanRenderer::freeCommandBuffers() {
  vkFreeCommandBuffers(m_device->getLogicalDevice(), m_device->getCommandPool(),
                       static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
  commandBuffers.clear();
}

