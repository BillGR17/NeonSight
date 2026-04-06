cmake_minimum_required(VERSION 3.20)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
project(NeonSight LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
add_compile_options(-Wall -Wextra -Wunused)

# Find Packages
find_package(X11 REQUIRED)
find_package(PkgConfig REQUIRED)

# X11 requirements
pkg_check_modules(XCURSOR REQUIRED xcursor)
pkg_check_modules(DBUS REQUIRED dbus-1)

# UI Requirements (ImGui needs GLFW and OpenGL)
find_package(OpenGL REQUIRED)
pkg_check_modules(GLFW3 REQUIRED glfw3)



# Download and Setup ImGui via FetchContent
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.6
)
FetchContent_MakeAvailable(imgui)

# Source files
set(SOURCES
    src/main.cpp
    src/App.cpp
    src/input/X11InputManager.cpp
    src/overlay/X11CursorInjector.cpp
    src/overlay/WaylandThemeCursorInjector.cpp
    src/utils/ImageLoader.cpp
    src/utils/XCursorThemeWriter.cpp
    src/config/AppConfig.cpp
    src/config/CursorMeta.cpp
    src/ui/ConfigWindow.cpp
    src/ui/TrayIcon.cpp
    src/utils/Resources.cpp

    # ImGui Implementations
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

add_executable(neonsight ${SOURCES})

target_include_directories(neonsight PRIVATE src)
target_include_directories(neonsight PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_include_directories(neonsight PRIVATE ${XCURSOR_INCLUDE_DIRS} ${GLFW3_INCLUDE_DIRS} ${DBUS_INCLUDE_DIRS})

# Link Libraries
target_link_libraries(neonsight PRIVATE
    X11::X11
    X11::Xfixes
    X11::Xext
    X11::Xrender
    ${XCURSOR_LIBRARIES}
    ${GLFW3_LIBRARIES}
    ${DBUS_LIBRARIES}
    OpenGL::GL
)

