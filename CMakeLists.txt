# Lumen – Build mit Visual Studio / MSVC
#   cmake -B build -G "Visual Studio 17 2022" -A x64
#   cmake --build build --config Release
cmake_minimum_required(VERSION 3.16)
project(Lumen LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB LUMEN_SRC CONFIGURE_DEPENDS src/*.cpp)
add_executable(Lumen WIN32 ${LUMEN_SRC} res/lumen.rc)
target_compile_definitions(Lumen PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
  target_compile_options(Lumen PRIVATE /utf-8 /W3 /permissive-)
  set_property(TARGET Lumen PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")   # keine VC-Runtime nötig
  target_link_options(Lumen PRIVATE /MANIFEST:NO)                            # Manifest kommt aus lumen.rc
else()
  target_link_options(Lumen PRIVATE -municode -static)
endif()

target_link_libraries(Lumen PRIVATE
  d3d11 dxgi mfuuid ole32 oleaut32 uuid windowscodecs shell32 comdlg32
  dwmapi wtsapi32 urlmon wininet propsys comctl32 user32 gdi32 advapi32)
