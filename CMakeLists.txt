cmake_minimum_required(VERSION 3.20)
project(CustomAudioCodec)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# NEW: We added src/app.rc so the icon gets baked into the exe!
add_executable(codec WIN32 src/main.cpp src/app.rc)

target_link_libraries(codec winmm dwmapi comctl32)