# We define our own compilation flags here.
set(CMAKE_CXX_STANDARD 17)

enable_language(ASM_NASM)
set(CMAKE_ASM_NASM_SOURCE_FILE_EXTENSIONS ${CMAKE_ASM_NASM_SOURCE_FILE_EXTENSIONS} asm)
set(CMAKE_ASM_NASM_COMPILE_OBJECT "<CMAKE_ASM_NASM_COMPILER> <INCLUDES> -f ${CMAKE_ASM_NASM_OBJECT_FORMAT} -o <OBJECT> <SOURCE>")
set_source_files_properties(kernel/asm_funcs.asm PROPERTIES COMPILE_FLAGS "-g")
set(RUNTIME_SOURCE
        runtime.cpp
        discord.cpp
        system/SystemThread.cpp
        system/IOP_Kernel.cpp
        system/iop_thread.cpp
        system/Deci2Server.cpp
        system/newpad.cpp
        sce/libcdvd_ee.cpp
        sce/libscf.cpp
        sce/libdma.cpp
        sce/libpad.cpp
        sce/libgraph.cpp
        sce/deci2.cpp
        sce/sif_ee.cpp
        sce/sif_ee_memcard.cpp
        sce/iop.cpp
        sce/stubs.cpp
        kernel/common/fileio.cpp
        kernel/common/kboot.cpp
        kernel/common/kdgo.cpp
        kernel/common/kdsnetm.cpp
        kernel/common/klink.cpp
        kernel/common/klisten.cpp
        kernel/common/kmachine.cpp
        kernel/common/kmalloc.cpp
        kernel/common/kmemcard.cpp
        kernel/common/kprint.cpp
        kernel/common/kscheme.cpp
        kernel/common/ksocket.cpp
        kernel/common/ksound.cpp
        kernel/jak1/fileio.cpp
        kernel/jak1/kboot.cpp
        kernel/jak1/kdgo.cpp
        kernel/jak1/klink.cpp
        kernel/jak1/klisten.cpp
        kernel/jak1/kmachine.cpp
        kernel/jak1/kprint.cpp
        kernel/jak1/kscheme.cpp
        kernel/jak1/ksound.cpp
        kernel/jak2/fileio.cpp
        kernel/jak2/kboot.cpp
        kernel/jak2/kdgo.cpp
        kernel/jak2/klink.cpp
        kernel/jak2/klisten.cpp
        kernel/jak2/kmachine.cpp
        kernel/jak2/kmalloc.cpp
        kernel/jak2/kprint.cpp
        kernel/jak2/kscheme.cpp
        kernel/jak2/ksound.cpp
        kernel/asm_funcs.asm
        mips2c/mips2c_table.cpp
        mips2c/jak1_functions/bones.cpp
        mips2c/jak1_functions/collide_cache.cpp
        mips2c/jak1_functions/collide_edge_grab.cpp
        mips2c/jak1_functions/collide_func.cpp
        mips2c/jak1_functions/collide_mesh.cpp
        mips2c/jak1_functions/collide_probe.cpp
        mips2c/jak1_functions/draw_string.cpp
        mips2c/jak1_functions/generic_effect.cpp
        mips2c/jak1_functions/generic_effect2.cpp
        mips2c/jak1_functions/generic_merc.cpp
        mips2c/jak1_functions/generic_tie.cpp
        mips2c/jak1_functions/joint.cpp
        mips2c/jak1_functions/merc_blend_shape.cpp
        mips2c/jak1_functions/ocean.cpp
        mips2c/jak1_functions/ocean_vu0.cpp
        mips2c/jak1_functions/ripple.cpp
        mips2c/jak1_functions/shadow.cpp
        mips2c/jak1_functions/sky_tng.cpp
        mips2c/jak1_functions/sparticle.cpp
        mips2c/jak1_functions/sparticle_launcher.cpp
        mips2c/jak1_functions/test_func.cpp
        mips2c/jak1_functions/texture.cpp
        mips2c/jak1_functions/tfrag.cpp
        mips2c/jak1_functions/tie_methods.cpp
        mips2c/jak1_functions/time_of_day.cpp
        mips2c/jak2_functions/bones.cpp
        mips2c/jak2_functions/collide_cache.cpp
        mips2c/jak2_functions/collide_edge_grab.cpp
        mips2c/jak2_functions/collide_func.cpp
        mips2c/jak2_functions/collide_hash.cpp
        mips2c/jak2_functions/collide_mesh.cpp
        mips2c/jak2_functions/debug.cpp
        mips2c/jak2_functions/font.cpp
        mips2c/jak2_functions/foreground.cpp
        mips2c/jak2_functions/joint.cpp
        mips2c/jak2_functions/lights.cpp
        mips2c/jak2_functions/nav_control.cpp
        mips2c/jak2_functions/nav_mesh.cpp
        mips2c/jak2_functions/sky.cpp
        mips2c/jak2_functions/sparticle.cpp
        mips2c/jak2_functions/sparticle_launcher.cpp
        mips2c/jak2_functions/spatial_hash.cpp
        mips2c/jak2_functions/texture.cpp
        mips2c/jak2_functions/ocean.cpp
        mips2c/jak2_functions/ocean_vu0.cpp
        mips2c/jak2_functions/merc_blend_shape.cpp
        mips2c/jak2_functions/ripple.cpp
        mips2c/jak2_functions/squid.cpp
        overlord/dma.cpp
        overlord/fake_iso.cpp
        overlord/iso.cpp
        overlord/iso_api.cpp
        overlord/iso_cd.cpp
        overlord/iso_queue.cpp
        overlord/isocommon.cpp
        overlord/overlord.cpp
        overlord/ramdisk.cpp
        overlord/sbank.cpp
        overlord/soundcommon.cpp
        overlord/srpc.cpp
        overlord/ssound.cpp
        overlord/stream.cpp
        graphics/gfx.cpp
        graphics/display.cpp
        graphics/sceGraphicsInterface.cpp
        graphics/texture/jak1_tpage_dir.cpp
        graphics/texture/jak2_tpage_dir.cpp
        graphics/texture/TextureConverter.cpp
        graphics/texture/TexturePoolCommon.cpp

        graphics/general_renderer/renderer_utils/Buffer.cpp
        graphics/general_renderer/background/background_common.cpp
        graphics/general_renderer/background/Shrub.cpp
        graphics/general_renderer/background/Tfrag3.cpp
        graphics/general_renderer/background/TFragment.cpp
        graphics/general_renderer/background/Tie3.cpp
        graphics/general_renderer/foreground/Generic2.cpp
        graphics/general_renderer/foreground/Generic2_DMA.cpp
        graphics/general_renderer/foreground/Generic2_Build.cpp
        graphics/general_renderer/foreground/Merc2.cpp
        graphics/general_renderer/ocean/CommonOceanRenderer.cpp
        graphics/general_renderer/ocean/OceanMid.cpp
        graphics/general_renderer/ocean/OceanMid_PS2.cpp
        graphics/general_renderer/ocean/OceanMidAndFar.cpp
        graphics/general_renderer/ocean/OceanNear.cpp
        graphics/general_renderer/ocean/OceanNear_PS2.cpp
        graphics/general_renderer/ocean/OceanTexture.cpp
        graphics/general_renderer/ocean/OceanTexture_PC.cpp
        graphics/general_renderer/BucketRenderer.cpp
        graphics/general_renderer/debug_gui.cpp
        graphics/general_renderer/DirectRenderer.cpp
        graphics/general_renderer/DirectRenderer2.cpp
        graphics/general_renderer/EyeRenderer.cpp
        graphics/general_renderer/dma_helpers.cpp
        graphics/general_renderer/Profiler.cpp
        graphics/general_renderer/LightningRenderer.cpp
        graphics/general_renderer/Shadow_PS2.cpp
        graphics/general_renderer/ShadowRenderer.cpp
        graphics/general_renderer/SkyBlendCPU.cpp
        graphics/general_renderer/SkyBlendGPU.cpp
        graphics/general_renderer/SkyRenderer.cpp
        graphics/general_renderer/sprite/sprite_common.cpp
        graphics/general_renderer/sprite/GlowRenderer.cpp
        graphics/general_renderer/sprite/Sprite3.cpp
        graphics/general_renderer/sprite/Sprite3_Distort.cpp
        graphics/general_renderer/sprite/Sprite3_Glow.cpp
        graphics/general_renderer/sprite/SpriteRenderer.cpp
        graphics/general_renderer/DepthCue.cpp
        graphics/general_renderer/TextureUploadHandler.cpp
        graphics/general_renderer/VisDataHandler.cpp

        system/vm/dmac.cpp
        system/vm/vm.cpp
        tools/subtitles/subtitle_editor.cpp
        tools/filter_menu/filter_menu.cpp
        )

if(WIN32)
  if (WIN32)
    set(GLSL_VALIDATOR "$ENV{VULKAN_SDK}/Bin/glslangValidator.exe")
  else()
    set(GLSL_VALIDATOR "$ENV{VULKAN_SDK}/Bin/glslangValidator")
  endif()
  
  file(GLOB_RECURSE GLSL_SOURCE_FILES
      "${CMAKE_CURRENT_SOURCE_DIR}/graphics/vulkan_renderer/shaders/*.frag"
      "${CMAKE_CURRENT_SOURCE_DIR}/graphics/vulkan_renderer/shaders/*.vert"
      )
  
  foreach(GLSL ${GLSL_SOURCE_FILES})
    get_filename_component(FILE_NAME ${GLSL} NAME)
    set(SPIRV "${CMAKE_CURRENT_SOURCE_DIR}/graphics/vulkan_renderer/shaders/${FILE_NAME}.spv")
    add_custom_command(
      OUTPUT ${SPIRV}
      COMMAND ${GLSL_VALIDATOR} -V ${GLSL} -o ${SPIRV}
      DEPENDS ${GLSL})
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
  endforeach(GLSL)

  add_custom_target(shaders ALL DEPENDS ${SPIRV_BINARY_FILES})

  list(APPEND RUNTIME_SOURCE
        graphics/pipelines/vulkan_pipeline.cpp
        graphics/vulkan_renderer/vulkan_utils/GraphicsDeviceVulkan.cpp
        graphics/vulkan_renderer/vulkan_utils/GraphicsPipelineLayout.cpp
        graphics/vulkan_renderer/vulkan_utils/DescriptorLayout.cpp
        graphics/vulkan_renderer/vulkan_utils/Image.cpp
        graphics/vulkan_renderer/vulkan_utils/VulkanBuffer.cpp
        graphics/vulkan_renderer/vulkan_utils/SwapChain.cpp
        graphics/vulkan_renderer/vulkan_utils/ImguiVulkanHelper.cpp
        graphics/vulkan_renderer/background/background_common.cpp
        graphics/vulkan_renderer/background/Shrub.cpp
        graphics/vulkan_renderer/background/Tfrag3.cpp
        graphics/vulkan_renderer/background/TFragment.cpp
        graphics/vulkan_renderer/background/Tie3.cpp
        graphics/vulkan_renderer/foreground/Merc2.cpp
        graphics/vulkan_renderer/foreground/Generic2_Vulkan.cpp
        graphics/vulkan_renderer/ocean/CommonOceanRenderer.cpp
        graphics/vulkan_renderer/ocean/OceanMid.cpp
        graphics/vulkan_renderer/ocean/OceanMidAndFar.cpp
        graphics/vulkan_renderer/ocean/OceanNear.cpp
        graphics/vulkan_renderer/ocean/OceanTexture.cpp
        graphics/vulkan_renderer/loader/Loader.cpp
        graphics/vulkan_renderer/loader/LoaderStages.cpp
        graphics/vulkan_renderer/BucketRenderer.cpp
        graphics/vulkan_renderer/CollideMeshRenderer.cpp
        graphics/vulkan_renderer/DirectRenderer.cpp
        graphics/vulkan_renderer/DirectRenderer2.cpp
        graphics/vulkan_renderer/EyeRenderer.cpp
        graphics/vulkan_renderer/FullScreenDraw.cpp
        graphics/vulkan_renderer/LightningRenderer.cpp
        graphics/vulkan_renderer/FramebufferHelper.cpp
        graphics/vulkan_renderer/Shader.cpp
        graphics/vulkan_renderer/ShadowRenderer.cpp
        graphics/vulkan_renderer/SkyBlendCPU.cpp
        graphics/vulkan_renderer/SkyBlendGPU.cpp
        graphics/vulkan_renderer/SkyRenderer.cpp
        graphics/vulkan_renderer/sprite/GlowRenderer.cpp
        graphics/vulkan_renderer/sprite/Sprite3.cpp
        graphics/vulkan_renderer/sprite/Sprite3_Distort.cpp
        graphics/vulkan_renderer/sprite/SpriteRenderer.cpp
        graphics/vulkan_renderer/DepthCue.cpp
        graphics/vulkan_renderer/VulkanRenderer.cpp
        graphics/texture/VulkanTexturePool.cpp
        ../third-party/glad/src/vulkan.c)
endif()
   list(APPEND RUNTIME_SOURCE
        graphics/opengl_renderer/background/background_common.cpp
        graphics/opengl_renderer/background/Shrub.cpp
        graphics/opengl_renderer/background/Tfrag3.cpp
        graphics/opengl_renderer/background/TFragment.cpp
        graphics/opengl_renderer/background/Tie3.cpp
        graphics/opengl_renderer/foreground/Merc2.cpp
        graphics/opengl_renderer/foreground/Generic2.cpp
        graphics/opengl_renderer/foreground/Generic2_DMA.cpp
        graphics/opengl_renderer/foreground/Generic2_Build.cpp
        graphics/opengl_renderer/foreground/Generic2_OpenGL.cpp
        graphics/opengl_renderer/sprite/GlowRenderer.cpp
        graphics/opengl_renderer/sprite/Sprite3.cpp
        graphics/opengl_renderer/sprite/Sprite3_Distort.cpp
        graphics/opengl_renderer/sprite/Sprite3_Glow.cpp
        graphics/opengl_renderer/sprite/SpriteRenderer.cpp
        graphics/opengl_renderer/ocean/CommonOceanRenderer.cpp
        graphics/opengl_renderer/ocean/OceanMid.cpp
        graphics/opengl_renderer/ocean/OceanMid_PS2.cpp
        graphics/opengl_renderer/ocean/OceanMidAndFar.cpp
        graphics/opengl_renderer/ocean/OceanNear.cpp
        graphics/opengl_renderer/ocean/OceanNear_PS2.cpp
        graphics/opengl_renderer/ocean/OceanTexture.cpp
        graphics/opengl_renderer/ocean/OceanTexture_PC.cpp
        graphics/opengl_renderer/loader/Loader.cpp
        graphics/opengl_renderer/loader/LoaderStages.cpp
        graphics/opengl_renderer/BucketRenderer.cpp
        graphics/opengl_renderer/CollideMeshRenderer.cpp
        graphics/opengl_renderer/DirectRenderer.cpp
        graphics/opengl_renderer/DirectRenderer2.cpp
        graphics/opengl_renderer/EyeRenderer.cpp
        graphics/opengl_renderer/LightningRenderer.cpp
        graphics/opengl_renderer/opengl_utils.cpp
        graphics/opengl_renderer/OpenGLRenderer.cpp
        graphics/opengl_renderer/Shader.cpp
        graphics/opengl_renderer/Shadow_PS2.cpp
        graphics/opengl_renderer/ShadowRenderer.cpp
        graphics/opengl_renderer/SkyBlendCPU.cpp
        graphics/opengl_renderer/SkyBlendGPU.cpp
        graphics/opengl_renderer/SkyRenderer.cpp
        graphics/opengl_renderer/TextureUploadHandler.cpp
        graphics/opengl_renderer/VisDataHandler.cpp
        graphics/opengl_renderer/DepthCue.cpp
        graphics/pipelines/opengl.cpp
        graphics/texture/TexturePool.cpp
        ../third-party/glad/src/glad.c)

find_package(Git)

function(write_svnrev_h)
    set(GIT_VERSION "")
    set(GIT_SHORT_SHA "")
    set(GIT_TAG "")
    if (GIT_FOUND AND EXISTS ${CMAKE_SOURCE_DIR}/.git)
        EXECUTE_PROCESS(WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
                OUTPUT_VARIABLE GIT_SHORT_SHA
                OUTPUT_STRIP_TRAILING_WHITESPACE)
        EXECUTE_PROCESS(WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} COMMAND ${GIT_EXECUTABLE} tag --points-at HEAD
                OUTPUT_VARIABLE GIT_TAG
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
    else()
        set(GIT_VERSION "unk. rev.")
    endif()
    if(GIT_TAG)
        set(GIT_VERSION ${GIT_TAG})
    elseif(GIT_SHORT_SHA)
        string(SUBSTRING ${GIT_SHORT_SHA} 0 6 GIT_SHORT_SHA)
        set(GIT_VERSION "rev. ${GIT_SHORT_SHA}")
    else()
        set(GIT_VERSION "unk. rev.")
    endif()

    file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/kernel/svnrev.h "#define GIT_VERSION \"${GIT_VERSION}\"\n")
endfunction()

write_svnrev_h()

add_subdirectory(sound)

# we build the runtime as a static library.
add_library(runtime STATIC ${RUNTIME_SOURCE})

target_link_libraries(runtime common fmt glfw imgui discord-rpc sound stb_image libco SQLiteCpp)
if(WIN32)
    target_link_libraries(runtime mman)
else()
    target_link_libraries(runtime pthread dl)
endif()

if(WIN32)
    add_executable(gk main.cpp ${SHADERS})
    add_dependencies(gk shaders)
else()
    add_executable(gk main.cpp)
endif()

 target_link_libraries(gk runtime)
