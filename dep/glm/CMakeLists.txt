cmake_minimum_required(VERSION 3.15)
project(GLM)

add_library(glm INTERFACE)
add_library(glm::glm ALIAS glm)
target_compile_definitions(glm INTERFACE GLM_FORCE_SWIZZLE GLM_ENABLE_EXPERIMENTAL GLM_FORCE_DEPTH_ZERO_TO_ONE)
target_include_directories(glm INTERFACE include)
