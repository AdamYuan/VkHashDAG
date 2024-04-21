cmake_minimum_required(VERSION 3.15)
project(VkHashDAG)

set(CMAKE_CXX_STANDARD 20)

# Windows' math include does not define constants by default.
# Set this definition so it does.
# Also set NOMINMAX so the min and max functions are not overwritten with macros.
if (CMAKE_SYSTEM_NAME STREQUAL Windows)
    add_definitions(-D_USE_MATH_DEFINES)
    add_definitions(-DNOMINMAX)
ENDIF ()

# Hide the console window in visual studio projects - Release
if (MSVC)
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT IPO_SUPPORTED)
    if (IPO_SUPPORTED)
        message("Enable IPO")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif ()
    if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Ofast")
    endif ()
endif ()

add_subdirectory(dep/MyVK)
add_subdirectory(dep/glm)
add_subdirectory(dep/libfork)
add_subdirectory(dep/parallel-hashmap)
add_subdirectory(dep/thread-pool)

add_subdirectory(shader)

add_executable(VkHashDAG
        src/main.cpp
        src/GPSQueueSelector.cpp
        src/Camera.cpp
        src/DAGNodePool.cpp
        src/DAGColorOctree.cpp
        src/DAGRenderer.cpp
)
target_include_directories(VkHashDAG PRIVATE include)
target_link_libraries(VkHashDAG PRIVATE
        myvk::vulkan myvk::glfw myvk::imgui
        libfork::libfork
        glm::glm
        BS::thread_pool
        phmap::phmap
        shader
)

install(TARGETS VkHashDAG RUNTIME DESTINATION)

add_executable(HashDAGTest
        test/test.cpp
)
target_include_directories(HashDAGTest PRIVATE include)
target_compile_definitions(HashDAGTest PRIVATE -DHASHDAG_TEST)
target_link_libraries(HashDAGTest PRIVATE libfork::libfork)
target_compile_options(HashDAGTest PRIVATE -fsanitize=address)
target_link_options(HashDAGTest PRIVATE -fsanitize=address)

add_executable(VBRTest
        test/vbr_test.cpp
)
target_include_directories(VBRTest PRIVATE include)
target_compile_definitions(VBRTest PRIVATE -DHASHDAG_TEST)
target_compile_options(VBRTest PRIVATE -fsanitize=address)
target_link_options(VBRTest PRIVATE -fsanitize=address)
