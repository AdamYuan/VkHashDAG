cmake_minimum_required(VERSION 3.15)
project(VkHashDAG)

set(SHADER_SRC
)

find_program(GLSLC "glslc")
IF (GLSLC)
    function(add_spirv_shader INPUT_FILE)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/include/shader/${INPUT_FILE}.u32
                COMMAND glslc src/${INPUT_FILE} -o include/shader/${INPUT_FILE}.u32 -O -Os -mfmt=num --target-env=vulkan1.1
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/${INPUT_FILE}
        )
    endfunction()

    foreach (X IN LISTS SHADER_SRC)
        add_spirv_shader(${X})
    endforeach ()
ENDIF ()

set(SHADER_U32)
foreach (X IN LISTS SHADER_SRC)
    list(APPEND SHADER_U32 include/shader/${X}.u32)
endforeach ()

add_custom_target(
        shader_u32 ALL DEPENDS
        ${SHADER_U32}
)
add_library(shader INTERFACE)
add_dependencies(shader shader_u32)
target_include_directories(shader INTERFACE include)
