cmake_minimum_required(VERSION 3.10)
project(PhoenixDisplay VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -flto -Wall -Wextra")

cmake_policy(SET CMP0072 NEW)

find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)

pkg_check_modules(GPS QUIET gps)
if(NOT GPS_FOUND)
    message(STATUS "GPS non trouvé, support désactivé")
    set(GPS_INCLUDE_DIRS "")
    set(GPS_LIBRARIES "")
endif()

# ImGui
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/imgui")
if(NOT EXISTS "${IMGUI_DIR}/imgui.cpp")
    message(FATAL_ERROR "ImGui introuvable dans ${IMGUI_DIR}. Lancez install.sh pour le télécharger.")
endif()

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# Sources de l'application (GLOB_RECURSE pour être sûr)
file(GLOB_RECURSE APP_SOURCES "src/*.cpp")
if(NOT APP_SOURCES)
    message(FATAL_ERROR "Aucun fichier .cpp trouvé dans src/")
endif()

add_executable(${PROJECT_NAME} ${APP_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${GLFW_INCLUDE_DIRS}
    ${JSONCPP_INCLUDE_DIRS}
    ${GPS_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME}
    imgui
    ${GLFW_LIBRARIES}
    OpenGL::GL
    ${JSONCPP_LIBRARIES}
    ${GPS_LIBRARIES}
    pthread
    dl
    X11
    Xrandr
    xcb
)

if(GPS_FOUND)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_GPS)
endif()