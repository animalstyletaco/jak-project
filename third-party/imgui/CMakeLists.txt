set(IMGUI_RUNTIME_SOURCES
        imgui
        imgui.cpp
        imgui_draw.cpp
        imgui_tables.cpp
        imgui_widgets.cpp
        imgui_impl_sdl.cpp
        imgui_stdlib.cpp
        imgui_style.cpp)

  list(APPEND IMGUI_RUNTIME_SOURCES
     imgui_impl_vulkan.cpp)
if(NOT APPLE)
   list(APPEND IMGUI_RUNTIME_SOURCES
     imgui_impl_opengl3.cpp)
endif()


add_library(imgui STATIC ${IMGUI_RUNTIME_SOURCES})
target_link_libraries(imgui SDL2::SDL2)
