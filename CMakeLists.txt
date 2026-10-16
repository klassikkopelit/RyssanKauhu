cmake_minimum_required(VERSION 3.10)
project(ryssankauhu C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(ryssankauhu WIN32
    src/main.c
    src/game.c
    src/platform.c
    src/gfx.c
    src/sound.c
    src/data.c
    src/files.c)

# "-sdl" like the other ports, so it is never mistaken for the original RYSS.EXE.
set_target_properties(ryssankauhu PROPERTIES OUTPUT_NAME ryssankauhu-sdl)

# Scripted input and screenshots for automated testing (see platform.c).
option(RYSS_AUTOTEST "Build with the IT_SCRIPT test hooks" OFF)
if(RYSS_AUTOTEST)
    target_compile_definitions(ryssankauhu PRIVATE RYSS_AUTOTEST)
endif()

if(EMSCRIPTEN)
    # Web version, built with "emcmake cmake" (see README.md). SDL2 comes with
    # Emscripten, and GRAPH.DAT from assets/ is packed into the page.
    # Produces index.html, index.js, index.wasm and index.data.
    target_compile_options(ryssankauhu PRIVATE -sUSE_SDL=2)
    target_link_options(ryssankauhu PRIVATE
        -sUSE_SDL=2
        -sASYNCIFY                 # lets the game's loops yield to the browser
        -sALLOW_MEMORY_GROWTH=1
        -sENVIRONMENT=web
        -sEXPORTED_FUNCTIONS=_main,_web_push_scancode
        -sMINIFY_HTML=0
        "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/web/shell.html"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets/GRAPH.DAT@/game/GRAPH.DAT")
    set_target_properties(ryssankauhu PROPERTIES
        OUTPUT_NAME index
        SUFFIX ".html"
        LINK_DEPENDS "${CMAKE_SOURCE_DIR}/web/shell.html;${CMAKE_SOURCE_DIR}/assets/GRAPH.DAT")
else()
    find_package(SDL2 REQUIRED)
    if(TARGET SDL2::SDL2)
        if(TARGET SDL2::SDL2main)
            target_link_libraries(ryssankauhu PRIVATE SDL2::SDL2main)
        endif()
        target_link_libraries(ryssankauhu PRIVATE SDL2::SDL2)
    elseif(TARGET SDL2::SDL2-static)
        if(TARGET SDL2::SDL2main)
            target_link_libraries(ryssankauhu PRIVATE SDL2::SDL2main)
        endif()
        target_link_libraries(ryssankauhu PRIVATE SDL2::SDL2-static)
    else()
        target_include_directories(ryssankauhu PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(ryssankauhu PRIVATE ${SDL2_LIBRARIES})
    endif()
endif()

if(MSVC)
    target_compile_definitions(ryssankauhu PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(ryssankauhu PRIVATE /W3 /utf-8)
else()
    target_compile_options(ryssankauhu PRIVATE -Wall -Wextra -O2)
endif()

# Windows: copy SDL2.dll next to the exe when SDL2 was found as a shared lib
if(WIN32 AND TARGET SDL2::SDL2)
  get_target_property(_sdl_type SDL2::SDL2 TYPE)
  if(_sdl_type STREQUAL "SHARED_LIBRARY")
    add_custom_command(TARGET ryssankauhu POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:ryssankauhu>)
  endif()
endif()
