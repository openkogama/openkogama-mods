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

# host test: drives the core with synthetic dispatches, no game needed
add_executable(okhost_test test/host_test.cpp)
target_link_libraries(okhost_test PRIVATE okmod_core)
target_include_directories(okhost_test PRIVATE ${CMAKE_SOURCE_DIR}/../include ${CMAKE_SOURCE_DIR}/src)
enable_testing()
add_test(NAME host COMMAND okhost_test $<TARGET_FILE_DIR:hello>)

# loads a dll by path; proves the patched game pulls in okmod
if(WIN32)
    add_executable(loadtest test/loadtest.cpp)
endif()
