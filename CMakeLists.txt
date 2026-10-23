cmake_minimum_required(VERSION 3.20)
project(Skylauncher LANGUAGES C CXX)
set(SKYLAUNCHER_VERSION "0.1.0" CACHE STRING "Version shown in the app bundle")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BUILD_SHARED_LIBS OFF)
if(MSVC)
    add_compile_options(/utf-8) # fmt requires it
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>") # no VC++ runtime to install
endif()

if(NOT EXISTS ${CMAKE_SOURCE_DIR}/thirdparty/SDL/CMakeLists.txt OR NOT EXISTS ${CMAKE_SOURCE_DIR}/thirdparty/XenonRecomp/thirdparty/fmt/CMakeLists.txt)
    message(FATAL_ERROR "Submodules missing: run `git submodule update --init --recursive`")
endif()

# Applies `patch` to the submodule in `directory` unless it is already applied.
function(skylauncher_apply_patch directory patch)
    execute_process(COMMAND git apply --reverse --check ${patch}
        WORKING_DIRECTORY ${directory} RESULT_VARIABLE already_applied OUTPUT_QUIET ERROR_QUIET)
    if(NOT already_applied EQUAL 0)
        execute_process(COMMAND git apply ${patch} WORKING_DIRECTORY ${directory} RESULT_VARIABLE result)
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "Could not apply ${patch} to ${directory}")
        endif()
    endif()
endfunction()

# XenonUtils (XenonRecomp, MIT) reads the disc's default.xex: some images declare a larger size than their blocks fill.
skylauncher_apply_patch(${CMAKE_SOURCE_DIR}/thirdparty/XenonRecomp ${CMAKE_SOURCE_DIR}/patches/xenonutils.patch)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
add_subdirectory(thirdparty/SDL EXCLUDE_FROM_ALL)

# Only the XEX and XDBF readers of XenonUtils: its disassembler (GNU binutils, GPL) is not built or linked.
set(XENON_ROOT ${CMAKE_SOURCE_DIR}/thirdparty/XenonRecomp)
add_subdirectory(${XENON_ROOT}/thirdparty/fmt EXCLUDE_FROM_ALL)
add_library(XenonXex STATIC
    ${XENON_ROOT}/XenonUtils/xex.cpp
    ${XENON_ROOT}/XenonUtils/image.cpp
    ${XENON_ROOT}/XenonUtils/xdbf_wrapper.cpp
    ${XENON_ROOT}/XenonUtils/xex_patcher.cpp
    ${XENON_ROOT}/XenonUtils/memory_mapped_file.cpp
    ${XENON_ROOT}/thirdparty/libmspack/libmspack/mspack/lzxd.c
    ${XENON_ROOT}/thirdparty/tiny-AES-c/aes.c)
target_include_directories(XenonXex PUBLIC ${XENON_ROOT}/XenonUtils ${XENON_ROOT}/thirdparty/simde
    PRIVATE ${XENON_ROOT}/thirdparty/libmspack/libmspack/mspack ${XENON_ROOT}/thirdparty/tiny-AES-c ${XENON_ROOT}/thirdparty/TinySHA1)
target_compile_definitions(XenonXex PRIVATE NOMINMAX)
target_link_libraries(XenonXex PUBLIC fmt::fmt)

# The fonts are also built into the executable, so a lone executable works (Windows .exe).
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/embedded_fonts.cpp
    COMMAND ${CMAKE_COMMAND} -DOUTFIT=${CMAKE_SOURCE_DIR}/resources/fonts/Outfit-Variable.ttf
        -DBUNGEE=${CMAKE_SOURCE_DIR}/resources/fonts/Bungee-Regular.ttf -DOUTPUT=${CMAKE_BINARY_DIR}/embedded_fonts.cpp
        -P ${CMAKE_SOURCE_DIR}/cmake/embed_fonts.cmake
    DEPENDS ${CMAKE_SOURCE_DIR}/cmake/embed_fonts.cmake ${CMAKE_SOURCE_DIR}/resources/fonts/Outfit-Variable.ttf
        ${CMAKE_SOURCE_DIR}/resources/fonts/Bungee-Regular.ttf
    COMMENT "Embedding the launcher fonts")

# Dear ImGui (MIT) with its SDL 3 platform and SDL_Renderer backends.
set(IMGUI_ROOT ${CMAKE_SOURCE_DIR}/thirdparty/imgui)
add_library(imgui STATIC ${IMGUI_ROOT}/imgui.cpp ${IMGUI_ROOT}/imgui_draw.cpp ${IMGUI_ROOT}/imgui_tables.cpp
    ${IMGUI_ROOT}/imgui_widgets.cpp ${IMGUI_ROOT}/backends/imgui_impl_sdl3.cpp ${IMGUI_ROOT}/backends/imgui_impl_sdlrenderer3.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_ROOT} ${IMGUI_ROOT}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3-static)

# Everything but the window: disc checks, settings, title metadata. Shared by the app and the test.
add_library(SkylauncherCore STATIC
    src/launcher.cpp
    src/launcher_games.cpp
    src/launcher_ui.cpp
    src/launcher_assets.cpp
    src/figures/figure_catalog.cpp
    src/stb_impl.cpp
    src/vfs.cpp
    src/platform/platform_file.cpp
    thirdparty/miniz/miniz.c
    ${CMAKE_BINARY_DIR}/embedded_fonts.cpp)
target_include_directories(SkylauncherCore PUBLIC src PRIVATE thirdparty/stb thirdparty/miniz)
target_link_libraries(SkylauncherCore PUBLIC SDL3::SDL3-static fmt::fmt XenonXex imgui)
target_compile_definitions(SkylauncherCore PRIVATE SSA_SOURCE_FONT_DIR="${CMAKE_SOURCE_DIR}/resources/fonts")
if(APPLE)
    target_link_libraries(SkylauncherCore PUBLIC "-framework CoreGraphics")
endif()

# Fonts are data, not game content: Outfit and Bungee (SIL Open Font License, licence files next to them).
set(SKYLAUNCHER_FONTS
    ${CMAKE_SOURCE_DIR}/resources/fonts/Outfit-Variable.ttf
    ${CMAKE_SOURCE_DIR}/resources/fonts/Bungee-Regular.ttf
    ${CMAKE_SOURCE_DIR}/resources/fonts/OFL-Outfit.txt
    ${CMAKE_SOURCE_DIR}/resources/fonts/OFL-Bungee.txt)

add_executable(Skylauncher WIN32 MACOSX_BUNDLE src/main.cpp)
target_link_libraries(Skylauncher PRIVATE SkylauncherCore)
set_target_properties(Skylauncher PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME Skylauncher
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.mflr0.skylauncher
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${SKYLAUNCHER_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${SKYLAUNCHER_VERSION}")
install(TARGETS Skylauncher BUNDLE DESTINATION . RUNTIME DESTINATION bin)
if(NOT APPLE)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/resources/fonts/ DESTINATION share/skylauncher/fonts)
endif()
if(APPLE)
    set_source_files_properties(${SKYLAUNCHER_FONTS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/fonts)
    target_sources(Skylauncher PRIVATE ${SKYLAUNCHER_FONTS})
else()
    add_custom_command(TARGET Skylauncher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/resources/fonts $<TARGET_FILE_DIR:Skylauncher>/fonts)
endif()

# Checks against the player's own disc image: SkylauncherTest <game disc .iso> <scratch dir> [<damaged copy>...]
add_executable(SkylauncherTest EXCLUDE_FROM_ALL tests/launcher_test.cpp)
target_link_libraries(SkylauncherTest PRIVATE SkylauncherCore)
