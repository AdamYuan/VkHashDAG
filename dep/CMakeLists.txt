cmake_minimum_required(VERSION 3.15)
project(VkHashDAG)

add_subdirectory(MyVK)

add_library(dep INTERFACE)
target_link_libraries(dep INTERFACE myvk::vulkan myvk::glfw myvk::imgui)
