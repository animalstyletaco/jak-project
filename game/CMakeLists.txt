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
        mips2c/functions/bones.cpp
        mips2c/functions/collide_cache.cpp
        mips2c/functions/collide_edge_grab.cpp
        mips2c/functions/collide_func.cpp
        mips2c/functions/collide_mesh.cpp
        mips2c/functions/collide_probe.cpp
        mips2c/functions/draw_string.cpp
        mips2c/functions/generic_effect.cpp
        mips2c/functions/generic_effect2.cpp
        mips2c/functions/generic_merc.cpp
        mips2c/functions/generic_tie.cpp
        mips2c/functions/joint.cpp
        mips2c/functions/merc_blend_shape.cpp
        mips2c/functions/ocean.cpp
        mips2c/functions/ocean_vu0.cpp
        mips2c/functions/ripple.cpp
        mips2c/functions/shadow.cpp
        mips2c/functions/sky_tng.cpp
        mips2c/functions/sparticle.cpp
        mips2c/functions/sparticle_launcher.cpp
        mips2c/functions/test_func.cpp
        mips2c/functions/texture.cpp
        mips2c/functions/tfrag.cpp
        mips2c/functions/tie_methods.cpp
        mips2c/functions/time_of_day.cpp
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
        graphics/texture/TextureConverter.cpp

        system/vm/dmac.cpp
        system/vm/vm.cpp
        tools/subtitles/subtitle_editor.h
        tools/subtitles/subtitle_editor.cpp)

if(WIN32)
  list(APPEND RUNTIME_SOURCE
        graphics/pipelines/vulkan.cpp
        graphics/vulkan_renderer/VulkanRenderer.cpp
        graphics/vulkan_renderer/background/background_common.cpp
        graphics/vulkan_renderer/background/Shrub.cpp
        graphics/vulkan_renderer/background/Tfrag3.cpp
        graphics/vulkan_renderer/background/TFragment.cpp
        graphics/vulkan_renderer/background/Tie3.cpp
        graphics/vulkan_renderer/foreground/Merc2.cpp
        graphics/vulkan_renderer/foreground/Generic2.cpp
        graphics/vulkan_renderer/foreground/Generic2_DMA.cpp
        graphics/vulkan_renderer/foreground/Generic2_Build.cpp
        graphics/vulkan_renderer/foreground/Generic2_Vulkan.cpp
        graphics/vulkan_renderer/ocean/CommonOceanRenderer.cpp
        graphics/vulkan_renderer/ocean/OceanMid.cpp
        graphics/vulkan_renderer/ocean/OceanMid_PS2.cpp
        graphics/vulkan_renderer/ocean/OceanMidAndFar.cpp
        graphics/vulkan_renderer/ocean/OceanNear.cpp
        graphics/vulkan_renderer/ocean/OceanNear_PS2.cpp
        graphics/vulkan_renderer/ocean/OceanTexture.cpp
        graphics/vulkan_renderer/ocean/OceanTexture_PC.cpp
        graphics/vulkan_renderer/loader/Loader.cpp
        graphics/vulkan_renderer/loader/LoaderStages.cpp
        graphics/vulkan_renderer/BucketRenderer.cpp
        graphics/vulkan_renderer/CollideMeshRenderer.cpp
        graphics/vulkan_renderer/debug_gui.cpp
        graphics/vulkan_renderer/DirectRenderer.cpp
        graphics/vulkan_renderer/DirectRenderer2.cpp
        graphics/vulkan_renderer/EyeRenderer.cpp
        graphics/vulkan_renderer/dma_helpers.cpp
        graphics/vulkan_renderer/vulkan_utils.cpp
        graphics/vulkan_renderer/Profiler.cpp
        graphics/vulkan_renderer/Shader.cpp
        graphics/vulkan_renderer/Shadow_PS2.cpp
        graphics/vulkan_renderer/ShadowRenderer.cpp
        graphics/vulkan_renderer/SkyBlendCPU.cpp
        graphics/vulkan_renderer/SkyBlendGPU.cpp
        graphics/vulkan_renderer/SkyRenderer.cpp
        graphics/vulkan_renderer/Sprite3.cpp
        graphics/vulkan_renderer/SpriteRenderer.cpp
        graphics/vulkan_renderer/TextureUploadHandler.cpp
        graphics/vulkan_renderer/DepthCue.cpp
        graphics/texture/TexturePoolVulkan.cpp
        ../third-party/glad/src/vulkan.c)
else()
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
        graphics/opengl_renderer/debug_gui.cpp
        graphics/opengl_renderer/DirectRenderer.cpp
        graphics/opengl_renderer/DirectRenderer2.cpp
        graphics/opengl_renderer/EyeRenderer.cpp
        graphics/opengl_renderer/dma_helpers.cpp
        graphics/opengl_renderer/opengl_utils.cpp
        graphics/opengl_renderer/OpenGLRenderer.cpp
        graphics/opengl_renderer/Profiler.cpp
        graphics/opengl_renderer/Shader.cpp
        graphics/opengl_renderer/Shadow_PS2.cpp
        graphics/opengl_renderer/ShadowRenderer.cpp
        graphics/opengl_renderer/SkyBlendCPU.cpp
        graphics/opengl_renderer/SkyBlendGPU.cpp
        graphics/opengl_renderer/SkyRenderer.cpp
        graphics/opengl_renderer/Sprite3.cpp
        graphics/opengl_renderer/SpriteRenderer.cpp
        graphics/opengl_renderer/TextureUploadHandler.cpp
        graphics/opengl_renderer/DepthCue.cpp
        graphics/texture/TexturePool.cpp
        graphics/pipelines/opengl.cpp
        ../third-party/glad/src/glad.c)
endif()

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

target_link_libraries(runtime common fmt glfw imgui discord-rpc sound stb_image libco)
if(WIN32)
    target_link_libraries(runtime mman)
else()
    target_link_libraries(runtime pthread dl)
endif()

add_executable(gk main.cpp)
target_link_libraries(gk runtime)
