cmake_minimum_required(VERSION 3.15)
project(progschj_ThreadPool)

add_library(progschj_ThreadPool INTERFACE)
add_library(progschj::ThreadPool ALIAS progschj_ThreadPool)
target_include_directories(progschj_ThreadPool INTERFACE include)
