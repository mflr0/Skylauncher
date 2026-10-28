cmake_minimum_required(VERSION 3.20)
project(ArkeyanLauncher LANGUAGES C CXX)
set(LAUNCHER_VERSION "0.6.1" CACHE STRING "Version shown in the app bundle")
# SKYLAUNCHER_VERSION is the name before 0.6.0: still accepted.
if(SKYLAUNCHER_VERSION)
    set(LAUNCHER_VERSION "${SKYLAUNCHER_VERSION}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BUILD_SHARED_LIBS OFF)
if(MINGW)
    add_compile_definitions(_WIN32_WINNT=0x0A00) # Windows 10 APIs (CreateFile2), as MSVC's SDK headers default to
endif()
if(WIN32)
    add_compile_definitions(NOMINMAX) # windows.h's min/max macros break std::min and std::max
endif()
if(MSVC)
    add_compile_options(/utf-8) # fmt requires it
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>") # no VC++ runtime to install
endif()

if(NOT EXISTS ${CMAKE_SOURCE_DIR}/lib/SDL/CMakeLists.txt OR NOT EXISTS ${CMAKE_SOURCE_DIR}/lib/XenonRecomp/thirdparty/fmt/CMakeLists.txt)
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
skylauncher_apply_patch(${CMAKE_SOURCE_DIR}/lib/XenonRecomp ${CMAKE_SOURCE_DIR}/patches/xenonutils.patch)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
add_subdirectory(lib/SDL EXCLUDE_FROM_ALL)

# Only the XEX and XDBF readers of XenonUtils: its disassembler (GNU binutils, GPL) is not built or linked.
set(XENON_ROOT ${CMAKE_SOURCE_DIR}/lib/XenonRecomp)
add_subdirectory(${XENON_ROOT}/thirdparty/fmt EXCLUDE_FROM_ALL)
if(MINGW)
    # fmt's format.h calls malloc/free without including <cstdlib>, which libc++ does not pull in on mingw.
    target_compile_options(fmt PUBLIC "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include cstdlib>")
endif()
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
    COMMAND ${CMAKE_COMMAND} -DOUTFIT=${CMAKE_SOURCE_DIR}/resources/fonts/Outfit-Regular.ttf -DOUTFIT_BOLD=${CMAKE_SOURCE_DIR}/resources/fonts/Outfit-SemiBold.ttf
        -DBUNGEE=${CMAKE_SOURCE_DIR}/resources/fonts/Bungee-Regular.ttf -DLUCIDE=${CMAKE_SOURCE_DIR}/resources/fonts/Lucide.ttf -DICON=${CMAKE_SOURCE_DIR}/resources/icons/skylauncher-128.png -DOUTPUT=${CMAKE_BINARY_DIR}/embedded_fonts.cpp
        -P ${CMAKE_SOURCE_DIR}/cmake/embed_fonts.cmake
    DEPENDS ${CMAKE_SOURCE_DIR}/cmake/embed_fonts.cmake ${CMAKE_SOURCE_DIR}/resources/fonts/Outfit-Regular.ttf
        ${CMAKE_SOURCE_DIR}/resources/fonts/Outfit-SemiBold.ttf
        ${CMAKE_SOURCE_DIR}/resources/fonts/Bungee-Regular.ttf ${CMAKE_SOURCE_DIR}/resources/fonts/Lucide.ttf
        ${CMAKE_SOURCE_DIR}/resources/icons/skylauncher-128.png
    COMMENT "Embedding the launcher fonts")

# SSARecomp's collection library (src/shared/collection, tools/sync_collection.sh) builds its toy catalogue in.
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/embedded_catalogue.cpp
    COMMAND ${CMAKE_COMMAND} -DNAME=g_embeddedCatalogue -DINPUT=${CMAKE_SOURCE_DIR}/resources/collection/catalogue.json
        -DOUTPUT=${CMAKE_BINARY_DIR}/embedded_catalogue.cpp -P ${CMAKE_SOURCE_DIR}/cmake/embed_file.cmake
    DEPENDS ${CMAKE_SOURCE_DIR}/cmake/embed_file.cmake ${CMAKE_SOURCE_DIR}/resources/collection/catalogue.json
    COMMENT "Embedding the toy catalogue")
# ...and where each game's pictures are on the disc: resources/games/<id>/art.json (synced too), joined into one array.
file(GLOB SKYLAUNCHER_ART_MANIFESTS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/resources/games/*/art.json)
list(SORT SKYLAUNCHER_ART_MANIFESTS)
string(REPLACE ";" "|" SKYLAUNCHER_ART_MANIFEST_LIST "${SKYLAUNCHER_ART_MANIFESTS}")
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/embedded_art_manifests.cpp
    COMMAND ${CMAKE_COMMAND} "-DINPUTS=${SKYLAUNCHER_ART_MANIFEST_LIST}" -DOUTPUT=${CMAKE_BINARY_DIR}/art_manifests.json
        -P ${CMAKE_SOURCE_DIR}/cmake/join_json.cmake
    COMMAND ${CMAKE_COMMAND} -DNAME=g_embeddedArtManifests -DINPUT=${CMAKE_BINARY_DIR}/art_manifests.json
        -DOUTPUT=${CMAKE_BINARY_DIR}/embedded_art_manifests.cpp -P ${CMAKE_SOURCE_DIR}/cmake/embed_file.cmake
    DEPENDS ${CMAKE_SOURCE_DIR}/cmake/join_json.cmake ${CMAKE_SOURCE_DIR}/cmake/embed_file.cmake ${SKYLAUNCHER_ART_MANIFESTS}
    COMMENT "Embedding the art manifests" VERBATIM)

# The launcher's languages: resources/lang/<code>.txt, one file each (docs/translations.md).
file(GLOB SKYLAUNCHER_LANGUAGES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/resources/lang/*.txt)
list(SORT SKYLAUNCHER_LANGUAGES)
string(REPLACE ";" "|" SKYLAUNCHER_LANGUAGE_LIST "${SKYLAUNCHER_LANGUAGES}")
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/embedded_languages.cpp
    COMMAND ${CMAKE_COMMAND} "-DINPUTS=${SKYLAUNCHER_LANGUAGE_LIST}" -DOUTPUT=${CMAKE_BINARY_DIR}/embedded_languages.cpp
        -P ${CMAKE_SOURCE_DIR}/cmake/embed_languages.cmake
    DEPENDS ${CMAKE_SOURCE_DIR}/cmake/embed_languages.cmake ${SKYLAUNCHER_LANGUAGES}
    COMMENT "Embedding the languages" VERBATIM)

# Dear ImGui (MIT) with its SDL 3 platform and SDL_Renderer backends.
set(IMGUI_ROOT ${CMAKE_SOURCE_DIR}/lib/imgui)
add_library(imgui STATIC ${IMGUI_ROOT}/imgui.cpp ${IMGUI_ROOT}/imgui_draw.cpp ${IMGUI_ROOT}/imgui_tables.cpp
    ${IMGUI_ROOT}/imgui_widgets.cpp ${IMGUI_ROOT}/backends/imgui_impl_sdl3.cpp ${IMGUI_ROOT}/backends/imgui_impl_sdlrenderer3.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_ROOT} ${IMGUI_ROOT}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3-static)

# Everything but the window: disc checks, settings, title metadata. Shared by the app and the test.
add_library(ArkeyanLauncherCore STATIC
    src/launcher/launcher.cpp
    src/launcher/launcher_games.cpp
    src/launcher/i18n.cpp
    src/launcher/game_install.cpp
    src/launcher/ui/launcher_ui.cpp
    src/launcher/ui/collection_page.cpp
    src/launcher/ui/collection_figure.cpp
    src/launcher/ui/collection_add.cpp
    src/launcher/ui/collection_manage.cpp
    src/launcher/ui/collection_model.cpp
    src/launcher/ui/game_look.cpp
    src/launcher/ui/components/core.cpp
    src/launcher/ui/components/buttons.cpp
    src/launcher/ui/components/inputs.cpp
    src/launcher/ui/components/layout.cpp
    src/launcher/ui/components/feedback.cpp
    src/launcher/ui/components/format.cpp
    src/launcher/ui/gallery.cpp
    src/assets/launcher_assets.cpp
    src/shared/figures/figure_catalog.cpp
    src/shared/figures/figure_crypto.cpp
    src/shared/figures/figure_data.cpp
    src/shared/figures/figure_tag.cpp
    src/shared/figures/figure_icons.cpp
    src/shared/collection/catalogue.cpp
    src/shared/collection/collection.cpp
    src/shared/collection/datadir.cpp
    src/shared/collection/game_data.cpp
    src/shared/collection/icons.cpp
    src/shared/collection/json.cpp
    src/shared/collection/save_backup.cpp
    src/shared/collection/search.cpp
    src/shared/collection/upgrades.cpp
    src/shared/art/alchemy_archive.cpp
    src/shared/art/disc_languages.cpp
    src/shared/art/game_art.cpp
    src/shared/art/game_text.cpp
    src/shared/art/lux_images.cpp
    src/shared/art/xenos_texture.cpp
    src/shared/stb_impl.cpp
    src/shared/vfs.cpp
    src/shared/platform/platform_file.cpp
    src/shared/platform/platform_process.cpp
    lib/miniz/miniz.c
    ${CMAKE_BINARY_DIR}/embedded_fonts.cpp
    ${CMAKE_BINARY_DIR}/embedded_catalogue.cpp
    ${CMAKE_BINARY_DIR}/embedded_art_manifests.cpp
    ${CMAKE_BINARY_DIR}/embedded_languages.cpp)
# The launcher shell: install progress, game runs and logs, updates, About (kept apart from the list above).
target_sources(ArkeyanLauncherCore PRIVATE
    src/launcher/install_checklist.cpp
    src/launcher/game_run.cpp
    src/launcher/portal_status.cpp
    src/launcher/game_keys.cpp
    src/launcher/game_settings.cpp
    src/launcher/game_copy.cpp
    src/launcher/shortcut.cpp
    src/launcher/uninstall.cpp
    src/launcher/ui/uninstall_page.cpp
    src/launcher/ui/game_settings_page.cpp
    src/launcher/ui/game_data_page.cpp
    src/launcher/report.cpp
    src/launcher/problem_report.cpp
    src/launcher/updater.cpp
    src/shared/mini_json.cpp
    src/launcher/ui/install_page.cpp
    src/launcher/ui/support_page.cpp
    src/launcher/ui/update_page.cpp)
target_include_directories(ArkeyanLauncherCore PUBLIC src PRIVATE lib/stb lib/miniz)
target_link_libraries(ArkeyanLauncherCore PUBLIC SDL3::SDL3-static fmt::fmt XenonXex imgui)
target_compile_definitions(ArkeyanLauncherCore PRIVATE SKYLAUNCHER_VERSION_STRING="${LAUNCHER_VERSION}" SSA_SOURCE_FONT_DIR="${CMAKE_SOURCE_DIR}/resources/fonts")
if(APPLE)
    target_link_libraries(ArkeyanLauncherCore PUBLIC "-framework CoreGraphics" "-framework CoreFoundation")
endif()
if(WIN32)
    target_link_libraries(ArkeyanLauncherCore PUBLIC ole32 uuid) # the desktop shortcut (IShellLink)
endif()

# Fonts are data, not game content: Outfit and Bungee (SIL Open Font License, licence files next to them).
set(SKYLAUNCHER_FONTS
    ${CMAKE_SOURCE_DIR}/resources/fonts/Outfit-Regular.ttf
    ${CMAKE_SOURCE_DIR}/resources/fonts/Outfit-SemiBold.ttf
    ${CMAKE_SOURCE_DIR}/resources/fonts/Bungee-Regular.ttf
    ${CMAKE_SOURCE_DIR}/resources/fonts/OFL-Outfit.txt
    ${CMAKE_SOURCE_DIR}/resources/fonts/OFL-Bungee.txt)

add_executable(ArkeyanLauncher WIN32 MACOSX_BUNDLE src/main.cpp)
target_link_libraries(ArkeyanLauncher PRIVATE ArkeyanLauncherCore)
# Windows: the manifest declares per-monitor v2 DPI awareness. MSVC's linker merges a .manifest source; mingw links it
# as a resource.
if(MINGW)
    # The version information (Explorer's Details tab) and the manifest and icon, as resources.
    string(REGEX MATCHALL "[0-9]+" LAUNCHER_VERSION_PARTS "${LAUNCHER_VERSION}")
    list(LENGTH LAUNCHER_VERSION_PARTS LAUNCHER_VERSION_PART_COUNT)
    foreach(index RANGE 0 3)
        if(index LESS LAUNCHER_VERSION_PART_COUNT)
            list(GET LAUNCHER_VERSION_PARTS ${index} LAUNCHER_VERSION_PART_${index})
        else()
            set(LAUNCHER_VERSION_PART_${index} 0)
        endif()
    endforeach()
    configure_file(resources/windows/skylauncher.rc.in ${CMAKE_BINARY_DIR}/skylauncher.rc @ONLY)
    target_sources(ArkeyanLauncher PRIVATE ${CMAKE_BINARY_DIR}/skylauncher.rc)
elseif(WIN32)
    target_sources(ArkeyanLauncher PRIVATE resources/windows/skylauncher.manifest)
endif()
# The bundle id stays the one of the first releases (a new one would only make macOS ask again for folder permissions).
set_target_properties(ArkeyanLauncher PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Arkeyan Launcher"
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.mflr0.skylauncher
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${LAUNCHER_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${LAUNCHER_VERSION}")
install(TARGETS ArkeyanLauncher BUNDLE DESTINATION . RUNTIME DESTINATION bin)
if(NOT APPLE)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/resources/fonts/ DESTINATION share/skylauncher/fonts)
endif()
if(APPLE)
    set_source_files_properties(${SKYLAUNCHER_FONTS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/fonts)
    target_sources(ArkeyanLauncher PRIVATE ${SKYLAUNCHER_FONTS})
else()
    add_custom_command(TARGET ArkeyanLauncher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/resources/fonts $<TARGET_FILE_DIR:ArkeyanLauncher>/fonts)
endif()

# Checks against the player's own disc image: ArkeyanLauncherTest <game disc .iso> <scratch dir> [<damaged copy>...]
add_executable(ArkeyanLauncherTest EXCLUDE_FROM_ALL tests/launcher_test.cpp)
target_link_libraries(ArkeyanLauncherTest PRIVATE ArkeyanLauncherCore)
target_include_directories(ArkeyanLauncherTest PRIVATE lib/stb)
# A stand-in for the kit's SkyBuild, in <build>/fake-kit: the install screens and ArkeyanLauncherTest run against it.
add_executable(FakeSkyBuild EXCLUDE_FROM_ALL tests/fake_skybuild.c)
set_target_properties(FakeSkyBuild PROPERTIES OUTPUT_NAME SkyBuild RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/fake-kit)
file(WRITE ${CMAKE_BINARY_DIR}/fake-kit/kit.txt "version=0.0\nbuild=fake-1\ngames=ssa,giants\n")
add_dependencies(ArkeyanLauncherTest FakeSkyBuild ArkeyanLauncher)
target_compile_definitions(ArkeyanLauncherTest PRIVATE SKYLAUNCHER_FAKE_KIT="${CMAKE_BINARY_DIR}/fake-kit" SKYLAUNCHER_TEST_EXE="$<TARGET_FILE:ArkeyanLauncher>")

# The launcher shell's checks (tests/shell_test.cpp), run by ArkeyanLauncherTest.
target_sources(ArkeyanLauncherTest PRIVATE tests/shell_test.cpp tests/settings_test.cpp tests/copy_test.cpp tests/i18n_test.cpp tests/uninstall_test.cpp tests/text_fit_test.cpp)
target_compile_definitions(ArkeyanLauncherTest PRIVATE SKYLAUNCHER_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(ArkeyanLauncherTest PRIVATE lib/miniz)
