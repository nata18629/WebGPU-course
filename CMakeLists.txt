cmake_minimum_required(VERSION 3.0...3.25)
project(
    WebGPUDawn # name of the project, which will also be the name of the visual studio solution if you use it
    VERSION 0.1.0 # any version number
    LANGUAGES CXX C # programming languages used by the project
)

add_executable(App main.cpp webgpu-raii.hpp)
add_subdirectory(glfw)
add_subdirectory(glfw3webgpu)
add_subdirectory(webgpu)
target_link_libraries(App PRIVATE glfw webgpu glfw3webgpu)
set_target_properties(App PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    COMPILE_WARNING_AS_ERROR ON
)