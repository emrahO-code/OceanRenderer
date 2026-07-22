cmake_minimum_required(VERSION 3.24)
project(WaterRenderer VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
option(WATER_FETCH_DEPENDENCIES "Download missing approved dependencies" ON)
option(WATER_ENABLE_VALIDATION "Enable Vulkan validation layers in debug builds" ON)

include(FetchContent)

# FetchContent normally downloads one copy per build directory. Reuse sources
# from another local build tree so CLion's cmake-build-* profiles work offline
# after any profile has populated the dependencies once.
foreach(DEPENDENCY spdlog imgui)
    string(TOUPPER "${DEPENDENCY}" DEPENDENCY_UPPER)
    foreach(CACHE_ROOT
        "${CMAKE_SOURCE_DIR}/build/_deps"
        "${CMAKE_SOURCE_DIR}/build-release/_deps")
        set(CACHED_SOURCE "${CACHE_ROOT}/${DEPENDENCY}-src")
        if(IS_DIRECTORY "${CACHED_SOURCE}")
            set(
                "FETCHCONTENT_SOURCE_DIR_${DEPENDENCY_UPPER}"
                "${CACHED_SOURCE}"
                CACHE PATH "Shared local source for ${DEPENDENCY}" FORCE)
            break()
        endif()
    endforeach()
endforeach()

find_package(Vulkan REQUIRED COMPONENTS glslc)
find_package(glfw3 3.3 QUIET)
find_package(glm QUIET)
find_package(spdlog QUIET)

if(NOT TARGET glfw)
    if(NOT WATER_FETCH_DEPENDENCIES)
        message(FATAL_ERROR "GLFW3 not found")
    endif()
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glfw)
endif()

if(NOT TARGET glm::glm)
    if(NOT WATER_FETCH_DEPENDENCIES)
        message(FATAL_ERROR "GLM not found")
    endif()
    FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1)
    FetchContent_MakeAvailable(glm)
endif()

if(NOT TARGET spdlog::spdlog)
    if(NOT WATER_FETCH_DEPENDENCIES)
        message(FATAL_ERROR "spdlog not found")
    endif()
    FetchContent_Declare(spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.14.1)
    set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(spdlog)
endif()

# Dear ImGui has no upstream CMake project. Build only the Vulkan/GLFW backends.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.5)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp")
target_include_directories(imgui PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends")
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)

set(SHADER_SOURCES
    shaders/sky.vert
    shaders/sky.frag
    shaders/water.vert
    shaders/water.frag
    shaders/ocean_spectrum.comp
    shaders/ocean_fft.comp
    shaders/ocean_finalize.comp)
set(SHADER_BINARIES)
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(FILE_NAME "${SHADER}" NAME)
    set(SPIRV "${CMAKE_BINARY_DIR}/shaders/${FILE_NAME}.spv")
    add_custom_command(
        OUTPUT "${SPIRV}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/shaders"
        COMMAND Vulkan::glslc "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}" -o "${SPIRV}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}"
        VERBATIM)
    list(APPEND SHADER_BINARIES "${SPIRV}")
endforeach()
add_custom_target(WaterShaders ALL DEPENDS ${SHADER_BINARIES})

add_executable(WaterRenderer
    src/main.cpp
    src/Application/Application.cpp
    src/Camera/Camera.cpp
    src/Ocean/TessendorfOcean.cpp
    src/Renderer/OceanCompute.cpp
    src/Renderer/RendererUI.cpp
    src/Renderer/SceneRendering.cpp
    src/Renderer/VulkanContext.cpp
    src/Renderer/VulkanRenderer.cpp)
add_dependencies(WaterRenderer WaterShaders)
target_include_directories(WaterRenderer PRIVATE
    src)
target_compile_definitions(WaterRenderer PRIVATE
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    WATER_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders"
    $<$<BOOL:${WATER_ENABLE_VALIDATION}>:WATER_ENABLE_VALIDATION=1>)
target_link_libraries(WaterRenderer PRIVATE
    Vulkan::Vulkan glfw glm::glm spdlog::spdlog imgui)

if(MSVC)
    target_compile_options(WaterRenderer PRIVATE /W4 /permissive-)
else()
    target_compile_options(WaterRenderer PRIVATE
        -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()

install(TARGETS WaterRenderer RUNTIME DESTINATION bin)
