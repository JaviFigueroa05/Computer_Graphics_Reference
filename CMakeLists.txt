cmake_minimum_required(VERSION 3.25)

project(CGCV_Reference VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# GLFW
set(GLFW_DIR external/glfw)
option(GLFW_BUILD_EXAMPLES "Build the GLFW example programs" OFF)
option(GLFW_BUILD_TESTS "Build the GLFW test programs" OFF)
option(GLFW_BUILD_DOCS "Build the GLFW documentation" OFF)
option(GLFW_INSTALL "Generate installation target" OFF)
option(GLFW_DOCUMENT_INTERNALS "Include internals in documentation" OFF)
add_subdirectory(${GLFW_DIR} binary_dir EXCLUDE_FROM_ALL)
include_directories(${GLFW_DIR}/include)

# ImGui
set(IMGUI_DIR external/imgui)
include_directories(${IMGUI_DIR} ${IMGUI_DIR}/backends ..)

# Vulkan
find_package(Vulkan REQUIRED)

# GLM
set(GLM_DIR external/glm)
include_directories(${GLM_DIR}/glm)

# VkBootstrap
set(VKB_DIR external/vk-bootstrap)
option(VK_BOOTSTRAP_TEST "Build the vk-bootstrap tests" OFF)
option(VK_BOOTSTRAP_WERROR "Ignore build errors" OFF)
add_subdirectory(${VKB_DIR})

# Vulkan Memory Allocator
set(VMA_DIR external/vma)
add_subdirectory(${VMA_DIR})
include_directories(${VMA_DIR}/include)

# Shader compilation
file(GLOB SHADERS shaders/*.vert shaders/*.frag shaders/*.comp shaders/*.geom shaders/*.tesc shaders/*.tese shaders/*.mesh shaders/*.task shaders/*.rgen shaders/*.rchit shaders/*.rmiss)

foreach(SHADER IN LISTS SHADERS)
    get_filename_component(FILENAME ${SHADER} NAME)
    add_custom_command(OUTPUT ${FILENAME}.spv
        COMMAND glslc ${SHADER} -o ${FILENAME}.spv
        DEPENDS ${SHADER}
        COMMENT "Compiling ${FILENAME}")
list(APPEND SPV_SHADERS ${FILENAME}.spv)
endForeach()

add_custom_target(shaders ALL DEPENDS ${SPV_SHADERS})

# Source files
set(LIBRARIES "glfw;Vulkan::Vulkan;vk-bootstrap;GPUOpen::VulkanMemoryAllocator")
file(GLOB sources src/*.cpp)

add_executable(CGCV_Reference 
    ${sources} 
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp 
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp 
    ${IMGUI_DIR}/imgui.cpp 
    ${IMGUI_DIR}/imgui_draw.cpp 
    ${IMGUI_DIR}/imgui_demo.cpp 
    ${IMGUI_DIR}/imgui_tables.cpp 
    ${IMGUI_DIR}/imgui_widgets.cpp
    )
target_link_libraries(CGCV_Reference ${LIBRARIES})
target_compile_definitions(CGCV_Reference PUBLIC -DImTextureID=ImU64)