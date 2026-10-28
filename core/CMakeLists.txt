cmake_minimum_required(VERSION 3.20)
project(okmod CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# static CRT so okmod.dll ships without a redistributable dependency
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(okmod_core STATIC
    src/core.cpp
    src/runtime_il2cpp.cpp
    src/runtime_mono.cpp
)
target_include_directories(okmod_core PUBLIC ${CMAKE_SOURCE_DIR}/../include PRIVATE ${CMAKE_SOURCE_DIR}/src)

# okmod.dll: what the patched game loads.
add_library(okmod SHARED src/dllmain.cpp)
target_link_libraries(okmod PRIVATE okmod_core)
target_include_directories(okmod PRIVATE ${CMAKE_SOURCE_DIR}/../include ${CMAKE_SOURCE_DIR}/src)

# native mods: the ABI/loader proof, and the Explorer feature on the framework
add_library(hello SHARED ../mods/hello/hello.c)
target_include_directories(hello PRIVATE ${CMAKE_SOURCE_DIR}/../include)
set_target_properties(hello PROPERTIES PREFIX "" OUTPUT_NAME "hello")

add_library(explorer SHARED ../mods/explorer/explorer.c)
target_include_directories(explorer PRIVATE ${CMAKE_SOURCE_DIR}/../include)
set_target_properties(explorer PROPERTIES PREFIX "" OUTPUT_NAME "explorer")

add_library(buildtools SHARED ../mods/buildtools/buildtools.c)
target_include_directories(buildtools PRIVATE ${CMAKE_SOURCE_DIR}/../include)
set_target_properties(buildtools PROPERTIES PREFIX "" OUTPUT_NAME "buildtools")

add_library(marker SHARED ../mods/marker/marker.c)
target_include_directories(marker PRIVATE ${CMAKE_SOURCE_DIR}/../include)
set_target_properties(marker PROPERTIES PREFIX "" OUTPUT_NAME "marker")

add_library(camera SHARED ../mods/camera/camera.c)
target_include_directories(camera PRIVATE ${CMAKE_SOURCE_DIR}/../include)
set_target_properties(camera PROPERTIES PREFIX "" OUTPUT_NAME "camera")

add_library(tweaks SHARED ../mods/tweaks/tweaks.c)
target_include_directories(tweaks PRIVATE ${CMAKE_SOURCE_DIR}/../include)
set_target_properties(tweaks PROPERTIES PREFIX "" OUTPUT_NAME "tweaks")

add_library(scrollzoom SHARED ../mods/scrollzoom/scrollzoom.c)
target_include_directories(scrollzoom PRIVATE ${CMAKE_SOURCE_DIR}/../include)
set_target_properties(scrollzoom PROPERTIES PREFIX "" OUTPUT_NAME "scrollzoom")

add_library(hideui SHARED ../mods/hideui/hideui.c)
target_include_directories(hideui PRIVATE ${CMAKE_SOURCE_DIR}/../include)
set_target_properties(hideui PROPERTIES PREFIX "" OUTPUT_NAME "hideui")

add_library(farview SHARED ../mods/farview/farview.c)
target_include_directories(farview PRIVATE ${CMAKE_SOURCE_DIR}/../include)
set_target_properties(farview PROPERTIES PREFIX "" OUTPUT_NAME "farview")

add_library(freecam SHARED ../mods/freecam/freecam.c)
target_include_directories(freecam PRIVATE ${CMAKE_SOURCE_DIR}/../include)
set_target_properties(freecam PROPERTIES PREFIX "" OUTPUT_NAME "freecam")

add_library(antiafk SHARED ../mods/antiafk/antiafk.c)
target_include_directories(antiafk PRIVATE ${CMAKE_SOURCE_DIR}/../include)
set_target_properties(antiafk PROPERTIES PREFIX "" OUTPUT_NAME "antiafk")

set(KOGAMASTUDIO ${CMAKE_SOURCE_DIR}/../mods/kogamastudio)
file(GLOB KOGAMASTUDIO_SOURCES ${KOGAMASTUDIO}/src/*.cpp ${KOGAMASTUDIO}/imgui/*.cpp ${KOGAMASTUDIO}/imnodes/*.cpp)
add_library(kogamastudio SHARED ${KOGAMASTUDIO_SOURCES})
target_include_directories(kogamastudio PRIVATE ${CMAKE_SOURCE_DIR}/../include ${KOGAMASTUDIO} ${KOGAMASTUDIO}/src ${KOGAMASTUDIO}/imgui)
set_target_properties(kogamastudio PROPERTIES PREFIX "" OUTPUT_NAME "kogamastudio" CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(kogamastudio PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
if(MSVC)
    target_compile_options(kogamastudio PRIVATE /utf-8 /bigobj)
endif()
target_link_libraries(kogamastudio PRIVATE d3d11 dxgi dcomp dwmapi d3dcompiler comdlg32 shell32 ole32 user32 gdi32)

# host test: drives the core with synthetic dispatches, no game needed
add_executable(okhost_test test/host_test.cpp)
target_link_libraries(okhost_test PRIVATE okmod_core)
target_include_directories(okhost_test PRIVATE ${CMAKE_SOURCE_DIR}/../include ${CMAKE_SOURCE_DIR}/src)
enable_testing()
add_test(NAME host COMMAND okhost_test $<TARGET_FILE_DIR:hello>)

add_library(framerate SHARED test/framerate.c)
target_include_directories(framerate PRIVATE ${CMAKE_SOURCE_DIR}/../include)
set_target_properties(framerate PROPERTIES PREFIX "" OUTPUT_NAME "framerate")

# loads a dll by path; proves the patched game pulls in okmod
if(WIN32)
    add_executable(loadtest test/loadtest.cpp)
endif()

# scripts: lua in a sandbox, drawn into the game's own frame
set(SCRIPTS ${CMAKE_SOURCE_DIR}/../mods/scripts)
set(IMGUI ${KOGAMASTUDIO}/imgui)
file(GLOB LUA_SOURCES ${SCRIPTS}/lua/*.c)
set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX COMPILE_DEFINITIONS luai_matchstep=scripts_matchstep)
add_library(scripts_core STATIC
    ${LUA_SOURCES}
    ${SCRIPTS}/src/runtime.cpp
    ${SCRIPTS}/src/api.cpp
    ${SCRIPTS}/src/game.cpp
    ${SCRIPTS}/src/images.cpp
    ${SCRIPTS}/src/textures.cpp
    ${SCRIPTS}/src/frame.cpp
    ${IMGUI}/imgui.cpp
    ${IMGUI}/imgui_draw.cpp
    ${IMGUI}/imgui_tables.cpp
    ${IMGUI}/imgui_widgets.cpp
)
target_include_directories(scripts_core PUBLIC ${CMAKE_SOURCE_DIR}/../include ${SCRIPTS}/lua ${SCRIPTS}/src ${IMGUI})
target_compile_definitions(scripts_core PUBLIC NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(scripts_core PUBLIC windowscodecs ole32)
if(MSVC)
    target_compile_options(scripts_core PUBLIC /utf-8)
endif()

add_library(scripts SHARED ${SCRIPTS}/src/scripts.cpp ${SCRIPTS}/src/render.cpp ${SCRIPTS}/src/paint9.cpp ${SCRIPTS}/src/remote.cpp ${IMGUI}/imgui_impl_win32.cpp)
target_link_libraries(scripts PRIVATE scripts_core d3d11 dxgi d3dcompiler dwmapi winhttp bcrypt shell32 user32 gdi32)
set_target_properties(scripts PROPERTIES PREFIX "" OUTPUT_NAME "scripts")

add_executable(scripts_test ${SCRIPTS}/test/scripts_test.cpp)
target_link_libraries(scripts_test PRIVATE scripts_core)
add_test(NAME scripts COMMAND scripts_test ${SCRIPTS}/test)
set_tests_properties(scripts PROPERTIES TIMEOUT 120)
