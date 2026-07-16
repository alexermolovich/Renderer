cmake_minimum_required(VERSION 3.20)
project(VkRenderer LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/external/imgui)
set(EXTERNAL_DIR ${CMAKE_SOURCE_DIR}/external)
set(ENKI_DIR ${CMAKE_SOURCE_DIR}/external/enkiTS)


find_package(Vulkan REQUIRED)
find_package(assimp REQUIRED)
find_package(SDL2 REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(glfw3 REQUIRED)

add_subdirectory(external/tinygltf)
add_subdirectory(foundation)

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

# Add Tracy directory (assume it's cloned into thirdparty/tracy)
set(TRACY_ENABLE ON CACHE BOOL "Enable Tracy Profiler" FORCE)
add_subdirectory(external/tracy)

target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_link_libraries(imgui PUBLIC
    SDL2::SDL2
    Vulkan::Vulkan
)

add_library(tlsf STATIC
    ${EXTERNAL_DIR}/tlsf.c
)
target_include_directories(tlsf PUBLIC
    ${EXTERNAL_DIR}
)

add_library(enkiTS STATIC
    ${ENKI_DIR}/TaskScheduler.cpp
)
target_include_directories(tlsf PUBLIC
    ${ENKI_DIR}/include
)

add_subdirectory(external/cglm)

file(GLOB_RECURSE SRC_FILES
    src/core/*.cpp
    src/demo/*.cpp
)

add_executable(vkml ${SRC_FILES})

target_include_directories(vkml
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        src
        external
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
        ${ENKI_DIR}/include
)

target_link_libraries(vkml PRIVATE
    Vulkan::Vulkan
    spirv-cross-core
    spirv-cross-glsl
    imgui
    tlsf
    enkiTS
    glfw
    tinygltf 
    glm::glm
    meshoptimizer
    foundation 
    Tracy::TracyClient


  )



set(SHADER_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SPIRV_DIR ${CMAKE_BINARY_DIR}/shaders)

file(MAKE_DIRECTORY ${SPIRV_DIR})

file(GLOB SHADERS CONFIGURE_DEPENDS
    ${SHADER_DIR}/*.vert
    ${SHADER_DIR}/*.frag
    ${SHADER_DIR}/*.comp
    )

set(SPIRV_SHADERS "")

foreach(SHADER ${SHADERS})
    get_filename_component(FILE_NAME ${SHADER} NAME)
    set(SPIRV ${SPIRV_DIR}/${FILE_NAME}.spv)

    add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${Vulkan_GLSLC_EXECUTABLE} -I ${SHADER_DIR} ${SHADER} -o ${SPIRV}
        DEPENDS ${SHADER} ${SHADER_DIR}/config.h
        COMMENT "Compiling shader ${FILE_NAME}"
    )

    list(APPEND SPIRV_SHADERS ${SPIRV})
endforeach()

add_custom_target(shaders ALL DEPENDS ${SPIRV_SHADERS})
add_dependencies(vkml shaders)
