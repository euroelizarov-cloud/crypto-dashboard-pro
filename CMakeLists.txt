cmake_minimum_required(VERSION 3.16)

project(dashboard VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Найдём Qt
### For WebAssembly build we enforce Qt6 (wasm toolchain ships only with Qt6)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets WebSockets Charts)

# Исходники
set(PROJECT_SOURCES
    main.cpp
    "main — 55 тикер квен.cpp"
    "main — копия-тикер-грок.cpp"
)

# Создание исполняемого файла
qt_add_executable(dashboard
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
)

# Подключаем библиотеки после создания цели!
target_link_libraries(dashboard PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::WebSockets
)

# Настройки для macOS/iOS
if (NOT EMSCRIPTEN)
    set_target_properties(dashboard PROPERTIES
            MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
            MACOSX_BUNDLE TRUE
            WIN32_EXECUTABLE TRUE
    )
endif()

# Установка
include(GNUInstallDirs)
install(TARGETS dashboard
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Завершаем сборку для Qt 6
qt_finalize_executable(dashboard)

# Helpful message for WebAssembly build
message(STATUS "Qt6 WebAssembly dashboard configuration completed.")

# --- Unified target ---
set(UNIFIED_SOURCES
    unified/main_unified.cpp
)

qt_add_executable(dashboard_unified
    MANUAL_FINALIZATION
    ${UNIFIED_SOURCES}
)

target_link_libraries(dashboard_unified PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::WebSockets
)

if (NOT EMSCRIPTEN)
    set_target_properties(dashboard_unified PROPERTIES
            MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
            MACOSX_BUNDLE TRUE
            WIN32_EXECUTABLE TRUE
    )
endif()

install(TARGETS dashboard_unified
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_finalize_executable(dashboard_unified)

# --- Charts refactor target ---
set(CHARTS_SOURCES
    charts_refactor/main_unified_charts.cpp
)

qt_add_executable(dashboard_charts
    MANUAL_FINALIZATION
    ${CHARTS_SOURCES}
)

target_link_libraries(dashboard_charts PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::WebSockets
    Qt6::Charts
)

if (NOT EMSCRIPTEN)
    set_target_properties(dashboard_charts PROPERTIES
            MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
            MACOSX_BUNDLE TRUE
            WIN32_EXECUTABLE TRUE
    )
endif()

install(TARGETS dashboard_charts
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_finalize_executable(dashboard_charts)

add_subdirectory(modular_dashboard)
