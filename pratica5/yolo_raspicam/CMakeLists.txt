cmake_minimum_required(VERSION 3.16)
project(yolo_raspicam LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Só o núcleo do GStreamer (libgstreamer1.0-dev). appsink/appsrc são usados pelos
# sinais do GObject, então não precisa do libgstreamer-plugins-base1.0-dev.
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0)

# ncnn compilado da fonte e instalado com -DCMAKE_INSTALL_PREFIX=/usr/local.
# Se instalou em outro lugar: cmake -Dncnn_DIR=<prefixo>/lib/cmake/ncnn ..
find_package(ncnn REQUIRED)

add_executable(yolo_raspicam
  src/main.cpp
  src/gst_io.cpp
  src/yolo_ncnn.cpp
)
target_include_directories(yolo_raspicam PRIVATE src)
target_link_libraries(yolo_raspicam PRIVATE ncnn PkgConfig::GST)
target_compile_options(yolo_raspicam PRIVATE -Wall -Wextra)
