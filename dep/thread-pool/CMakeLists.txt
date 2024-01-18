cmake_minimum_required(VERSION 3.15)
project(BS_thread_pool)

add_library(BS_thread_pool INTERFACE)
add_library(BS::thread_pool ALIAS BS_thread_pool)
target_include_directories(BS_thread_pool INTERFACE include)
