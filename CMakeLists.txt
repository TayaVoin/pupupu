cmake_minimum_required(VERSION 3.14)
project(server_compact)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -Wall -O2")

set(THIRD_PARTY_DIR ${PROJECT_SOURCE_DIR}/third_party)
set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)
set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)

if (APPLE)
    list(APPEND CMAKE_PREFIX_PATH
        /opt/homebrew/opt/libpq
        /usr/local/opt/libpq
    )
endif()

# SDL2
find_package(SDL2 REQUIRED)
# OpenGL/GLEW
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
# ZeroMQ
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED IMPORTED_TARGET libzmq)
# PostgreSQL
find_package(PostgreSQL REQUIRED)
# CURL
find_package(CURL REQUIRED)
# nlohmann_json
find_package(nlohmann_json REQUIRED)

set(EXTRA_LIBS)
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    list(APPEND EXTRA_LIBS stdc++fs)
endif()

include_directories(${INCLUDE_DIR}
    ${SDL2_INCLUDE_DIRS}
    ${OPENGL_INCLUDE_DIRS}
    ${GLEW_INCLUDE_DIRS}
    ${ZMQ_INCLUDE_DIRS}
    ${PostgreSQL_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${THIRD_PARTY_DIR}/imgui
    ${THIRD_PARTY_DIR}/implot
    ${THIRD_PARTY_DIR}
)

# ImGui
add_library(imgui STATIC
    ${THIRD_PARTY_DIR}/imgui/imgui.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_draw.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_tables.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp
    ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_sdl2.cpp
    ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_opengl3.cpp
)
target_compile_options(imgui PRIVATE -fPIC)

# ImPlot
add_library(implot STATIC
    ${THIRD_PARTY_DIR}/implot/implot.cpp
    ${THIRD_PARTY_DIR}/implot/implot_items.cpp
)
target_link_libraries(implot imgui)
target_compile_options(implot PRIVATE -fPIC)

# Исходники
add_executable(server
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/ServerCore.cpp
    ${SRC_DIR}/MapManager.cpp
    ${SRC_DIR}/GuiManager.cpp
)

target_link_libraries(server PRIVATE
    imgui implot
    ${SDL2_LIBRARIES}
    ${OPENGL_LIBRARIES}
    GLEW::GLEW
    PkgConfig::ZMQ
    ${PostgreSQL_LIBRARIES}
    ${CURL_LIBRARIES}
    nlohmann_json::nlohmann_json
    pthread
    ${EXTRA_LIBS}
)
