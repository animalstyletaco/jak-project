set(IMGUI_RUNTIME_SOURCES
    imgui.cpp
    imgui_draw.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
    imgui_impl_glfw.cpp
    imgui_stdlib.cpp
    imgui_stdlib.h)

if(APPLE)
  list(APPEND IMGUI_RUNTIME_SOURCES
     imgui_impl_vulkan.cpp)
else()
   list(APPEND IMGUI_RUNTIME_SOURCES
     imgui_impl_opengl3.cpp)
endif()

add_library(imgui STATIC ${IMGUI_RUNTIME_SOURCES})
target_link_libraries(imgui glfw)
