cmake_minimum_required(VERSION 3.16)

project(modular_dashboard VERSION 0.6.0 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets WebSockets Charts)

set(HEADERS
    include/MainWindow.h
    include/DataWorker.h
    include/DynamicSpeedometerCharts.h
    include/Profiler.h
    include/ThemeManager.h
    include/TransitionOverlay.h
)

set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/DataWorker.cpp
    src/DynamicSpeedometerCharts.cpp
    src/Profiler.cpp
    src/ThemeManager.cpp
    src/TransitionOverlay.cpp
)

qt_add_executable(modular_dashboard
    MANUAL_FINALIZATION
    ${HEADERS}
    ${SOURCES}
)

target_include_directories(modular_dashboard PRIVATE include)

target_link_libraries(modular_dashboard PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::WebSockets
    Qt6::Charts
)

set_target_properties(modular_dashboard PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

install(TARGETS modular_dashboard
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_finalize_executable(modular_dashboard)
