cmake_minimum_required(VERSION 3.16)

project(dashboard VERSION 0.6.0 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Найдём Qt
### For WebAssembly build we enforce Qt6 (wasm toolchain ships only with Qt6)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets WebSockets Charts)

message(STATUS "Root legacy targets removed; building only modular_dashboard (v${PROJECT_VERSION}).")

add_subdirectory(modular_dashboard)
