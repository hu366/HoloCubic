# 全息棱镜宠物助手 — 顶层 CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# 将 main 目录作为一个 ESP-IDF 组件
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(hologram_pet)
