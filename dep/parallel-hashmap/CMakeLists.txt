#[===================================================================[
    parallel-hashmap library by Gregory Popovitch

    CMake projects that wish to use this library may do
    something like :

    include(FetchContent)
    FetchContent_Declare(
        parallel-hashmap
        GIT_REPOSITORY https://github.com/greg7mdp/parallel-hashmap.git
        GIT_TAG        v1.3.8 # adjust tag/branch/commit as needed
    )
    FetchContent_MakeAvailable(parallel-hashmap)

    ...
    include_directories(${parallel-hashmap_SOURCE_DIR})

#]===================================================================]

cmake_minimum_required(VERSION 3.8)

list (APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(DetectVersion)

cmake_policy(SET CMP0048 NEW)             ## set VERSION as documented by the project() command.
cmake_policy(SET CMP0076 NEW)             ## accept new policy

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 11)            ## compile with C++11 support
endif()

if(NOT CMAKE_CXX_STANDARD_REQUIRED)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

if(NOT DEFINED PHMAP_MASTER_PROJECT)
    set(PHMAP_MASTER_PROJECT OFF)
    if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
        set(PHMAP_MASTER_PROJECT ON)
    endif()
endif()

project(phmap VERSION ${DETECTED_PHMAP_VERSION} LANGUAGES CXX)

## ----------------------------- options -----------------------------

set(PHMAP_DIR parallel_hashmap)
set(PHMAP_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/${PHMAP_DIR}/phmap.h 
                  ${CMAKE_CURRENT_SOURCE_DIR}/${PHMAP_DIR}/phmap_base.h 
                  ${CMAKE_CURRENT_SOURCE_DIR}/${PHMAP_DIR}/phmap_bits.h 
                  ${CMAKE_CURRENT_SOURCE_DIR}/${PHMAP_DIR}/phmap_config.h
                  ${CMAKE_CURRENT_SOURCE_DIR}/${PHMAP_DIR}/phmap_dump.h 
                  ${CMAKE_CURRENT_SOURCE_DIR}/${PHMAP_DIR}/phmap_fwd_decl.h 
                  ${CMAKE_CURRENT_SOURCE_DIR}/${PHMAP_DIR}/phmap_utils.h 
                  ${CMAKE_CURRENT_SOURCE_DIR}/${PHMAP_DIR}/meminfo.h 
                  ${CMAKE_CURRENT_SOURCE_DIR}/${PHMAP_DIR}/btree.h)

include(helpers)

add_library(${PROJECT_NAME} INTERFACE)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_sources(${PROJECT_NAME} INTERFACE ${PHMAP_HEADERS})

target_include_directories(
     ${PROJECT_NAME} INTERFACE
     $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
     $<INSTALL_INTERFACE:include>)
