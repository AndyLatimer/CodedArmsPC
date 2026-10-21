cmake_minimum_required(VERSION 3.16)
project(CodedArmsPort VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# SDL2: system package on Linux; bundled MinGW/VC development package on Windows.
if(WIN32 AND NOT SDL2_DIR)
  if(MINGW AND EXISTS "${CMAKE_SOURCE_DIR}/third_party/SDL2-2.30.9/cmake")
    set(SDL2_DIR "${CMAKE_SOURCE_DIR}/third_party/SDL2-2.30.9/cmake")
  elseif(MSVC AND EXISTS "${CMAKE_SOURCE_DIR}/third_party/SDL2-VC/cmake")
    set(SDL2_DIR "${CMAKE_SOURCE_DIR}/third_party/SDL2-VC/cmake")
  endif()
endif()
find_package(SDL2 REQUIRED CONFIG)

if(MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# Engine library: everything except the entry point, so tests can link it.
add_library(ca_engine STATIC
  src/core/Log.cpp
  src/core/Config.cpp
  src/platform/Input.cpp
  src/game/Level.cpp
  src/game/Player.cpp
  src/game/Collision.cpp
  src/assets/Formats.cpp
  src/game/OriginalWorld.cpp
  src/render/GL.cpp
  src/render/Renderer.cpp
)
target_include_directories(ca_engine PUBLIC src)
target_include_directories(ca_engine SYSTEM PUBLIC third_party/stb)
target_link_libraries(ca_engine PUBLIC SDL2::SDL2)

add_executable(CodedArms WIN32 src/main.cpp src/App.cpp)
if(TARGET SDL2::SDL2main)
  target_link_libraries(CodedArms PRIVATE SDL2::SDL2main)
endif()
target_link_libraries(CodedArms PRIVATE ca_engine)

add_executable(ca_assetcheck tools/assetcheck.cpp)
target_link_libraries(ca_assetcheck PRIVATE ca_engine)

enable_testing()
add_executable(test_core tests/test_core.cpp)
if(TARGET SDL2::SDL2main)
  target_link_libraries(test_core PRIVATE SDL2::SDL2main)
endif()
target_link_libraries(test_core PRIVATE ca_engine)
add_test(NAME core COMMAND test_core)

if(MINGW)
  foreach(t CodedArms ca_assetcheck test_core)
    target_link_options(${t} PRIVATE -static-libgcc -static-libstdc++ -static)
  endforeach()
endif()
