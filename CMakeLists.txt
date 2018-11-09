cmake_minimum_required(VERSION 2.6)
project(openvr-window-overlay)

link_libraries(openvr_api)

find_package(OpenGL REQUIRED)
find_package(X11)

find_package(PkgConfig)

pkg_check_modules(DEPS REQUIRED gstreamer-1.0 gstreamer-video-1.0 gstreamer-app-1.0 gstreamer-gl-1.0 gtk+-3.0 sdl2)

link_libraries(${DEPS_LIBRARIES} ${OPENGL_LIBRARIES} ${X11_LIBRARIES})
include_directories(${DEPS_INCLUDE_DIRS} ${OPENGL_INCLUDE_DIRS} ${X11_INCLUDE_DIR})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -pedantic -Wextra")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -pedantic -Wextra")

add_executable(openvr-window-overlay overlay.cpp)

install(TARGETS openvr-window-overlay RUNTIME DESTINATION bin)
