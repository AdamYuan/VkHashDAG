# CMake version >= 3.15.0 required because of https://github.com/Forceflow/libmorton/issues/62
# CMake version >= 3.8.2 required because of c++11
cmake_minimum_required(VERSION 3.15.0)
project(libmorton VERSION 0.2.8)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNOMINMAX=1")
endif()

set(libmorton_INCLUDE_DIR "${libmorton_SOURCE_DIR}/include")

add_library(libmorton INTERFACE)

set_property(TARGET libmorton
  PROPERTY
    PUBLIC_HEADER
      "${libmorton_SOURCE_DIR}/include/libmorton/morton_common.h"
      "${libmorton_SOURCE_DIR}/include/libmorton/morton_AVX512BITALG.h"
      "${libmorton_SOURCE_DIR}/include/libmorton/morton_BMI.h"
      "${libmorton_SOURCE_DIR}/include/libmorton/morton2D_LUTs.h"
      "${libmorton_SOURCE_DIR}/include/libmorton/morton2D.h"
      "${libmorton_SOURCE_DIR}/include/libmorton/morton3D_LUTs.h"
      "${libmorton_SOURCE_DIR}/include/libmorton/morton3D.h"
      "${libmorton_SOURCE_DIR}/include/libmorton/morton.h"
  )

target_include_directories(libmorton
  INTERFACE
    $<BUILD_INTERFACE:${libmorton_INCLUDE_DIR}>
  )

add_library(libmorton::libmorton ALIAS libmorton)