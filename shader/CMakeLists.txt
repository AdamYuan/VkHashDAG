cmake_minimum_required(VERSION 3.15)
project(VkHashDAG)

find_program(GLSLC "glslc" REQUIRED)

add_library(shader INTERFACE)
target_include_directories(shader INTERFACE include)

function(add_shader INPUT_FILE OUTPUT_FILE OPTION)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/include/shader/${OUTPUT_FILE}
            COMMAND ${GLSLC} src/${INPUT_FILE} -o include/shader/${OUTPUT_FILE} -mfmt=num --target-env=vulkan1.3 ${OPTION}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/${INPUT_FILE}
    )
    add_custom_target(shader_${OUTPUT_FILE} ALL DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/include/shader/${OUTPUT_FILE})
    add_dependencies(shader shader_${OUTPUT_FILE})
endfunction()

add_shader(quad.vert quad.vert.u32 "-O")
add_shader(beam.frag beam.frag.u32 "-O")
add_shader(trace.frag trace.frag.u32 "-O")
add_shader(trace.frag trace_beam.frag.u32 "-O;-D BEAM_OPTIMIZATION")
add_shader(crosshair.vert crosshair.vert.u32 "-O;-D LENGTH=32")
add_shader(crosshair.frag crosshair.frag.u32 "-O")
