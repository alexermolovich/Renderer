add_library(foundation
    camera.cpp
)
find_package(glm REQUIRED)


target_include_directories(foundation
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        src
        external
)

target_link_libraries(foundation
    PUBLIC
        glm::glm
)

target_compile_features(foundation PUBLIC cxx_std_17)