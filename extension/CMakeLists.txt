cmake_minimum_required(VERSION 3.20)
project(reaper_uf8 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ----- REAPER SDK + WDL (fetch if missing) ------------------------------------
# reaper_plugin.h on non-Windows includes "../WDL/swell/swell.h" relative to
# the sdk directory. We vendor both repos into vendor/ so the relative path
# resolves: vendor/reaper-sdk/sdk/reaper_plugin.h -> vendor/WDL/swell/swell.h
set(VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor")
set(REAPER_SDK_DIR "${VENDOR_DIR}/reaper-sdk")
set(WDL_DIR        "${VENDOR_DIR}/WDL")

include(FetchContent)
if(NOT EXISTS "${REAPER_SDK_DIR}/sdk/reaper_plugin.h")
    FetchContent_Declare(reaper_sdk
        GIT_REPOSITORY https://github.com/justinfrankel/reaper-sdk.git
        GIT_TAG main
        SOURCE_DIR "${REAPER_SDK_DIR}"
    )
    FetchContent_MakeAvailable(reaper_sdk)
endif()
if(NOT EXISTS "${WDL_DIR}/WDL/swell/swell.h")
    FetchContent_Declare(wdl
        GIT_REPOSITORY https://github.com/justinfrankel/WDL.git
        GIT_TAG main
        SOURCE_DIR "${WDL_DIR}"
    )
    FetchContent_MakeAvailable(wdl)
endif()

# reaper_plugin.h does `#include "../WDL/swell/swell.h"` — expects WDL to
# live as a sibling of the sdk/ directory. POSIX uses a symlink; Windows
# falls back to a copy because symbolic links there need admin / dev mode.
if(NOT EXISTS "${REAPER_SDK_DIR}/WDL")
    if(WIN32)
        file(CREATE_LINK "${WDL_DIR}/WDL" "${REAPER_SDK_DIR}/WDL"
             COPY_ON_ERROR SYMBOLIC)
    else()
        file(CREATE_LINK "${WDL_DIR}/WDL" "${REAPER_SDK_DIR}/WDL" SYMBOLIC)
    endif()
endif()

# ----- libusb + hidapi --------------------------------------------------------
# macOS: brew install libusb hidapi  → discovered via pkg-config.
# Linux: apt install libusb-1.0-0-dev libhidapi-dev → same path.
# Windows: pkg-config isn't usually present. Either let CMake find a
# vcpkg-installed copy via find_package, or point us at a prebuilt drop
# with -DLIBUSB_ROOT=<path> / -DHIDAPI_ROOT=<path>. The Root variants
# match libusb's MSVC release layout (include/, MS64/dll/) and hidapi's
# Windows release layout (include/, x64/).
if(WIN32)
    if(NOT LIBUSB_ROOT)
        message(FATAL_ERROR
            "Set -DLIBUSB_ROOT=<path>. Expected layout: include/libusb-1.0/libusb.h, MS64/dll/libusb-1.0.lib (libusb MSVC release).")
    endif()
    if(NOT HIDAPI_ROOT)
        message(FATAL_ERROR
            "Set -DHIDAPI_ROOT=<path>. Expected layout: include/hidapi.h, x64/hidapi.lib (hidapi Windows release).")
    endif()
    add_library(reasixty_libusb INTERFACE)
    target_include_directories(reasixty_libusb INTERFACE
        "${LIBUSB_ROOT}/include")
    # Pick the per-MSVC subtree from the libusb release zip. Default to
    # VS2022; override with -DLIBUSB_MSVC=VS2019 etc. if a different
    # toolset is in use.
    if(NOT LIBUSB_MSVC)
        set(LIBUSB_MSVC "VS2022")
    endif()
    target_link_libraries(reasixty_libusb INTERFACE
        "${LIBUSB_ROOT}/${LIBUSB_MSVC}/MS64/dll/libusb-1.0.lib")
    add_library(reasixty_hidapi INTERFACE)
    target_include_directories(reasixty_hidapi INTERFACE
        "${HIDAPI_ROOT}/include")
    target_link_libraries(reasixty_hidapi INTERFACE
        "${HIDAPI_ROOT}/x64/hidapi.lib")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
    # macOS exposes the hidapi module as plain "hidapi" (Homebrew). Linux
    # splits it into hidapi-hidraw (kernel-backed) and hidapi-libusb
    # (libusb-backed). Prefer hidraw on Linux so UC1's HID access doesn't
    # contend with libusb's UF8 claim on the same root hub.
    pkg_check_modules(HIDAPI IMPORTED_TARGET hidapi)
    if(NOT HIDAPI_FOUND)
        pkg_check_modules(HIDAPI IMPORTED_TARGET hidapi-hidraw)
    endif()
    if(NOT HIDAPI_FOUND)
        pkg_check_modules(HIDAPI REQUIRED IMPORTED_TARGET hidapi-libusb)
    endif()
    add_library(reasixty_libusb INTERFACE)
    target_link_libraries(reasixty_libusb INTERFACE PkgConfig::LIBUSB)
    add_library(reasixty_hidapi INTERFACE)
    target_link_libraries(reasixty_hidapi INTERFACE PkgConfig::HIDAPI)
endif()

# ----- the plugin -------------------------------------------------------------
# Generated header holding the current HEAD commit count + git-describe
# version string. Must regenerate on EVERY build because the inputs (git
# HEAD, tag list, working-tree dirty bit) change without touching any
# source file CMake can dependency-track. The previous OUTPUT+DEPENDS
# pattern only re-ran when the .cmake script itself changed, so a
# tag-push between builds produced a stale REASIXTY_VERSION_STR baked
# into the dylib (v0.1.8 Mac release shipped with v0.1.7-5-…-dirty).
# The script itself short-circuits when the new content matches the old,
# so always-run is cheap and doesn't trigger needless recompiles.
set(REASIXTY_COMMIT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/commit_count.h")
add_custom_target(reasixty_commit_count_gen ALL
    BYPRODUCTS ${REASIXTY_COMMIT_HEADER}
    COMMAND ${CMAKE_COMMAND}
        -DOUTFILE=${REASIXTY_COMMIT_HEADER}
        -DGIT_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/write_commit_count.cmake
    COMMENT "Updating commit count + version for About"
    VERBATIM)
# Force SettingsScreen.cpp recompile when the header content changes so
# REASIXTY_VERSION_STR / REASIXTY_COMMIT_COUNT actually update in the
# binary. (Without this the new header could exist on disk but cached
# .o files would keep the old macro values.)
set_source_files_properties(src/SettingsScreen.cpp
    PROPERTIES OBJECT_DEPENDS ${REASIXTY_COMMIT_HEADER})

# Bake extension/resources/factory.rea60config into a header so a
# user-triggered "Reset to factory defaults" works on a fresh REAPER
# install without needing the file on disk.
set(REASIXTY_FACTORY_BUNDLE_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/factory.rea60config")
set(REASIXTY_FACTORY_BUNDLE_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/generated/factory_bundle.h")
add_custom_command(
    OUTPUT ${REASIXTY_FACTORY_BUNDLE_HEADER}
    COMMAND ${CMAKE_COMMAND}
        -DSRCFILE=${REASIXTY_FACTORY_BUNDLE_SRC}
        -DOUTFILE=${REASIXTY_FACTORY_BUNDLE_HEADER}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_factory_bundle.cmake
    DEPENDS
        ${REASIXTY_FACTORY_BUNDLE_SRC}
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_factory_bundle.cmake
    COMMENT "Embedding factory.rea60config"
    VERBATIM
)
add_custom_target(reasixty_factory_bundle_gen ALL
    DEPENDS ${REASIXTY_FACTORY_BUNDLE_HEADER})

# Same embedding pattern for the Rea-Sixty Input Level JSFX probe. The
# extension writes this into <ResourcePath>/Effects/Rea-Sixty/ on load so it
# shows up in REAPER's FX browser without a manual copy.
set(REASIXTY_INPUT_LEVEL_JSFX_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/jsfx/rea_sixty_input_level.jsfx")
set(REASIXTY_INPUT_LEVEL_JSFX_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/generated/input_level_jsfx.h")
add_custom_command(
    OUTPUT ${REASIXTY_INPUT_LEVEL_JSFX_HEADER}
    COMMAND ${CMAKE_COMMAND}
        -DSRCFILE=${REASIXTY_INPUT_LEVEL_JSFX_SRC}
        -DOUTFILE=${REASIXTY_INPUT_LEVEL_JSFX_HEADER}
        -DSYMBOL=kInputLevelJsfx
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_factory_bundle.cmake
    DEPENDS
        ${REASIXTY_INPUT_LEVEL_JSFX_SRC}
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_factory_bundle.cmake
    COMMENT "Embedding rea_sixty_input_level.jsfx"
    VERBATIM
)
add_custom_target(reasixty_input_level_jsfx_gen ALL
    DEPENDS ${REASIXTY_INPUT_LEVEL_JSFX_HEADER})

# Same embedding pattern for the Inserts overlay / dock-panel companion Lua.
# The extension writes this into <ResourcePath>/Scripts/rea-sixty/ on load and
# can run it straight from Settings via AddRemoveReaScript + Main_OnCommand —
# no ReaPack step, no manual action hunt.
set(REASIXTY_INSERTS_OVERLAY_LUA_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/rea_sixty_inserts_overlay.lua")
set(REASIXTY_INSERTS_OVERLAY_LUA_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/generated/inserts_overlay_lua.h")
add_custom_command(
    OUTPUT ${REASIXTY_INSERTS_OVERLAY_LUA_HEADER}
    COMMAND ${CMAKE_COMMAND}
        -DSRCFILE=${REASIXTY_INSERTS_OVERLAY_LUA_SRC}
        -DOUTFILE=${REASIXTY_INSERTS_OVERLAY_LUA_HEADER}
        -DSYMBOL=kInsertsOverlayLua
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_factory_bundle.cmake
    DEPENDS
        ${REASIXTY_INSERTS_OVERLAY_LUA_SRC}
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_factory_bundle.cmake
    COMMENT "Embedding rea_sixty_inserts_overlay.lua"
    VERBATIM
)
add_custom_target(reasixty_inserts_overlay_lua_gen ALL
    DEPENDS ${REASIXTY_INSERTS_OVERLAY_LUA_HEADER})

# Same embedding pattern for the Assignment-HUD companion Lua (ReaImGui
# place-anywhere surface-mockup that mirrors the focused plug-in's UC1 control
# assignments). 2026-06-16: replaced the legacy gfx HUD with the ReaImGui one.
set(REASIXTY_ASSIGNMENT_HUD_LUA_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/rea_sixty_assignment_hud_imgui.lua")
set(REASIXTY_ASSIGNMENT_HUD_LUA_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/generated/assignment_hud_lua.h")
add_custom_command(
    OUTPUT ${REASIXTY_ASSIGNMENT_HUD_LUA_HEADER}
    COMMAND ${CMAKE_COMMAND}
        -DSRCFILE=${REASIXTY_ASSIGNMENT_HUD_LUA_SRC}
        -DOUTFILE=${REASIXTY_ASSIGNMENT_HUD_LUA_HEADER}
        -DSYMBOL=kAssignmentHudLua
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_factory_bundle.cmake
    DEPENDS
        ${REASIXTY_ASSIGNMENT_HUD_LUA_SRC}
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_factory_bundle.cmake
    COMMENT "Embedding rea_sixty_assignment_hud_imgui.lua"
    VERBATIM
)
add_custom_target(reasixty_assignment_hud_lua_gen ALL
    DEPENDS ${REASIXTY_ASSIGNMENT_HUD_LUA_HEADER})

# Same embedding pattern for the frameless focused-track panel companion Lua.
# 2026-06-16: replaced the legacy JS_Composite panel with the ReaImGui one.
set(REASIXTY_FOCUSED_PANEL_LUA_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/rea_sixty_focused_panel_imgui.lua")
set(REASIXTY_FOCUSED_PANEL_LUA_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/generated/focused_panel_lua.h")
add_custom_command(
    OUTPUT ${REASIXTY_FOCUSED_PANEL_LUA_HEADER}
    COMMAND ${CMAKE_COMMAND}
        -DSRCFILE=${REASIXTY_FOCUSED_PANEL_LUA_SRC}
        -DOUTFILE=${REASIXTY_FOCUSED_PANEL_LUA_HEADER}
        -DSYMBOL=kFocusedPanelLua
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_factory_bundle.cmake
    DEPENDS
        ${REASIXTY_FOCUSED_PANEL_LUA_SRC}
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_factory_bundle.cmake
    COMMENT "Embedding rea_sixty_focused_panel_imgui.lua"
    VERBATIM
)
add_custom_target(reasixty_focused_panel_lua_gen ALL
    DEPENDS ${REASIXTY_FOCUSED_PANEL_LUA_HEADER})

# Same embedding pattern for the transient mode-change banner companion Lua
# (Stream B3, 2026-06-22).
set(REASIXTY_MODE_BANNER_LUA_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/rea_sixty_mode_banner.lua")
set(REASIXTY_MODE_BANNER_LUA_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/generated/mode_banner_lua.h")
add_custom_command(
    OUTPUT ${REASIXTY_MODE_BANNER_LUA_HEADER}
    COMMAND ${CMAKE_COMMAND}
        -DSRCFILE=${REASIXTY_MODE_BANNER_LUA_SRC}
        -DOUTFILE=${REASIXTY_MODE_BANNER_LUA_HEADER}
        -DSYMBOL=kModeBannerLua
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_factory_bundle.cmake
    DEPENDS
        ${REASIXTY_MODE_BANNER_LUA_SRC}
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_factory_bundle.cmake
    COMMENT "Embedding rea_sixty_mode_banner.lua"
    VERBATIM
)
add_custom_target(reasixty_mode_banner_lua_gen ALL
    DEPENDS ${REASIXTY_MODE_BANNER_LUA_HEADER})

# Bake the canonical user manual (docs/user-manual.md) into a header so the
# Settings → Manual tab renders it offline, always in lockstep with the repo
# doc — no second copy to maintain, no runtime file to ship.
set(REASIXTY_USER_MANUAL_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/../docs/user-manual.md")
set(REASIXTY_USER_MANUAL_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/generated/user_manual.h")
add_custom_command(
    OUTPUT ${REASIXTY_USER_MANUAL_HEADER}
    COMMAND ${CMAKE_COMMAND}
        -DSRCFILE=${REASIXTY_USER_MANUAL_SRC}
        -DOUTFILE=${REASIXTY_USER_MANUAL_HEADER}
        -DSYMBOL=kUserManual
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_factory_bundle.cmake
    DEPENDS
        ${REASIXTY_USER_MANUAL_SRC}
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_factory_bundle.cmake
    COMMENT "Embedding docs/user-manual.md"
    VERBATIM
)
add_custom_target(reasixty_user_manual_gen ALL
    DEPENDS ${REASIXTY_USER_MANUAL_HEADER})

# Same embedding pattern for the Windows WinUSB INF. The Settings UI
# (Win-only branch) writes this to %TEMP% and feeds it to pnputil so
# the user can bind WinUSB to UF8 + UC1 with a single elevated click.
if(WIN32)
    set(REASIXTY_WINUSB_INF_SRC
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/rea_sixty_winusb.inf")
    set(REASIXTY_WINUSB_INF_HEADER
        "${CMAKE_CURRENT_BINARY_DIR}/generated/winusb_inf.h")
    add_custom_command(
        OUTPUT ${REASIXTY_WINUSB_INF_HEADER}
        COMMAND ${CMAKE_COMMAND}
            -DSRCFILE=${REASIXTY_WINUSB_INF_SRC}
            -DOUTFILE=${REASIXTY_WINUSB_INF_HEADER}
            -DSYMBOL=kWinUsbInfBytes
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_factory_bundle.cmake
        DEPENDS
            ${REASIXTY_WINUSB_INF_SRC}
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_factory_bundle.cmake
        COMMENT "Embedding rea_sixty_winusb.inf"
        VERBATIM
    )
    add_custom_target(reasixty_winusb_inf_gen ALL
        DEPENDS ${REASIXTY_WINUSB_INF_HEADER})
endif()

add_library(reaper_uf8 MODULE
    src/main.cpp
    # Per-platform location of our diagnostic logs (/tmp vs %TEMP%)
    src/LogPath.cpp
    src/Protocol.cpp
    src/Palette.cpp
    src/TrackName.cpp
    src/PluginMap.cpp
    src/UserPluginCatalog.cpp
    src/AutoLearnEngine.cpp
    src/PluginChunkPatch.cpp
    src/FocusedParam.cpp
    src/UF8Device.cpp
    src/UF1Protocol.cpp
    src/UF1Device.cpp
    src/UC1Protocol.cpp
    src/UC1Device.cpp
    src/UC1PluginMap.cpp
    src/UC1Surface.cpp
    src/ColorSync.cpp
    src/MidiBridge.cpp
    # Phase 2.7 — the dockable window and the Settings panes it hosts.
    # (Phase 2.6, the Plug-in Mixer view, was never built; its MixerLayout
    #  scaffold was removed 2026-08-14.)
    src/MixerWindow.cpp
    src/ManualView.cpp
    src/KnobFeelPresets.cpp
    src/SettingsScreen.cpp
    # Devices / Appearance / Behaviour panes, split out of SettingsScreen.cpp
    src/SettingsScreen_General.cpp
    src/ThemeBridge.cpp
    # Phase 2.7 Bindings — Phase A
    src/Bindings.cpp
    # Keyboard-macro action type — chord synthesis to REAPER's main window
    src/KeyMacro.cpp
    # Virtual notch / detent for relative-encoder writes
    src/VirtualNotch.cpp
    # Multi-track parameter sync (8 persistent groups + temp-from-selection)
    src/ParameterGroups.cpp
    # Phase 2.8 — Nav Mode (Markers & Regions) overlay
    src/MarkerOverlay.cpp
    src/NavDispatch.cpp
    # Setup bundle (save/restore of the whole configuration)
    src/SetupBundle.cpp
    # DynaMount Mode — robotic mic-stand control over the LAN
    src/DynaMountClient.cpp
    src/DynaMountManager.cpp
    src/StreamDeckBridge.cpp
    # SSL 360°Core impersonator — meter data straight from the SSL plug-in.
    # Self-contained (std lib only, no REAPER API); this is what supplies the
    # CS Gate/Comp gain reduction that no REAPER API exposes.
    src/SslMeterProtocol.cpp
    src/SslCoreImpersonator.cpp
    # Mapping Exchange — async HTTPS client + the in-app Exchange view. One
    # implementation per platform, each self-guarded by its own #if, so the
    # three compile to nothing where they don't apply: macos_http.mm
    # (NSURLSession, added under APPLE below), win_http.cpp (WinHTTP),
    # linux_http.cpp (libcurl via dlopen), HttpClient.cpp (the fallback stub).
    src/HttpClient.cpp
    src/win_http.cpp
    src/linux_http.cpp
    src/ExchangeView.cpp
)

if(APPLE)
    # NSSavePanel-backed save dialog for layer export. See
    # macos_save_dialog.mm for why we bypass SWELL/plugin_getapi.
    target_sources(reaper_uf8 PRIVATE
        src/macos_save_dialog.mm
        src/macos_open_dialog.mm)
    set_source_files_properties(
        src/macos_save_dialog.mm
        src/macos_open_dialog.mm
        PROPERTIES COMPILE_FLAGS "-fno-objc-arc")
    # AppKit window-positioning helpers for the pin-FX-GUI feature.
    # Same rationale as the save dialog: SWELL SetWindowPos /
    # GetWindowRect / GetSystemMetrics aren't reliably reachable on
    # macOS 15.
    target_sources(reaper_uf8 PRIVATE src/macos_pin_fx_gui.mm)
    set_source_files_properties(src/macos_pin_fx_gui.mm PROPERTIES
        COMPILE_FLAGS "-fno-objc-arc")
    # NSURLSession HTTPS client for the mapping exchange. -fno-objc-arc to
    # match the other .mm files; the shared state manages task lifetime.
    target_sources(reaper_uf8 PRIVATE src/macos_http.mm)
    set_source_files_properties(src/macos_http.mm PROPERTIES
        COMPILE_FLAGS "-fno-objc-arc")
endif()

# SWELL's swell-types.h #defines min/max as 2-arg macros, which collide
# with libstdc++'s 3-arg min/max overloads on Linux (and would on Windows
# too without NOMINMAX). The codebase already uses std::min/std::max
# everywhere, so opt out of SWELL's macros project-wide.
target_compile_definitions(reaper_uf8 PRIVATE WDL_NO_DEFINE_MINMAX)

# windows.h #defines min/max too, and SWELL's opt-out above does nothing about
# those. Unparenthesised std::min / std::max then fail to parse (MSVC C2589,
# "illegal token on right side of ::") — ~20 sites across main.cpp and
# SslCoreImpersonator.cpp, none of which the branch ever saw because CI only
# built main and the UF1 work lived on a branch until 2026-08-12. NOMINMAX at
# the project level beats parenthesising every call.
if(WIN32)
    target_compile_definitions(reaper_uf8 PRIVATE NOMINMAX)
endif()

# Windows gives the main thread 1 MB of stack and MSVC's prologue probes the
# whole frame before the first instruction, so an oversized local crashes in
# __chkstk with no breadcrumb and no usable stack trace (0xc00000fd). It has
# bitten twice: 699a0d0 (Config locals in bindings::load) and the Config
# temporary inside seedFactoryDefaults_, which reached 90% of the budget once
# the UF1 fields grew sizeof(Binding). macOS and Linux thread stacks are ~8 MB,
# so neither notices. Fail the build here instead of on a user's machine.
# 256 KB is well clear of the largest legitimate frame in the tree
# (drawFxLearnEditor_, ~149 KB) and well under the cliff. MSVC has no
# equivalent flag; frame size is a property of the code, so catching it on the
# other two compilers is enough. Both spellings are probed rather than assumed
# — GCC and Clang disagree about whether -Werror= accepts an option that takes
# an argument, and an unverified build flag is how the UF1 merge broke CI.
if(NOT MSVC)
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-Wframe-larger-than=262144" HAVE_WFRAME_LARGER_THAN)
    if(HAVE_WFRAME_LARGER_THAN)
        target_compile_options(reaper_uf8 PRIVATE -Wframe-larger-than=262144)
        check_cxx_compiler_flag("-Werror=frame-larger-than" HAVE_WERROR_FRAME_BARE)
        if(HAVE_WERROR_FRAME_BARE)
            target_compile_options(reaper_uf8 PRIVATE -Werror=frame-larger-than)
        else()
            check_cxx_compiler_flag("-Werror=frame-larger-than=262144"
                                    HAVE_WERROR_FRAME_ARG)
            if(HAVE_WERROR_FRAME_ARG)
                target_compile_options(reaper_uf8 PRIVATE
                    -Werror=frame-larger-than=262144)
            endif()
        endif()
    endif()
endif()

target_link_libraries(reaper_uf8 PRIVATE reasixty_hidapi)

if(APPLE)
    target_link_libraries(reaper_uf8 PRIVATE
        "-framework CoreMIDI"
        "-framework CoreFoundation"
        "-framework Foundation"
        "-framework ApplicationServices"
        "-framework AppKit")
endif()

if(WIN32)
    # ReaPack-friendly DLL discovery on Windows. With /DELAYLOAD the
    # actual LoadLibrary for libusb-1.0.dll / hidapi.dll happens lazily
    # on first call instead of at REAPER startup. REAPER_PLUGIN_ENTRY
    # extends the DLL search path to include our own DLL's directory
    # (= UserPlugins) before any libusb / hidapi function is touched,
    # so users can drop all three DLLs into UserPlugins without
    # touching the REAPER program-files directory.
    target_link_options(reaper_uf8 PRIVATE
        "/DELAYLOAD:libusb-1.0.dll"
        "/DELAYLOAD:hidapi.dll")
    # comdlg32 = Win32 GetSaveFileName (native Save-As; REAPER has no
    # BrowseForSaveFile on Windows — that is a SWELL-only symbol).
    # winhttp = the mapping exchange's HTTPS client (src/win_http.cpp). Ships
    # with Windows, so nothing extra to package.
    # setupapi = the WinUSB driver-status readout in Settings -> About. Reading
    # SPDRP_SERVICE / SPDRP_MFG needs no elevation and no PowerShell, which is
    # the whole point: the user can see what their devices are bound to without
    # a UAC prompt.
    target_link_libraries(reaper_uf8 PRIVATE
        delayimp.lib comdlg32.lib winhttp.lib setupapi.lib)
endif()

if(UNIX AND NOT APPLE)
    # dlopen for the mapping exchange's libcurl loader (src/linux_http.cpp).
    # Folded into libc from glibc 2.34, but older distributions still need -ldl
    # and CMAKE_DL_LIBS is empty where it isn't wanted.
    target_link_libraries(reaper_uf8 PRIVATE ${CMAKE_DL_LIBS})

    # Link the C++ runtime IN rather than depending on the build machine's.
    # A GCC 13 host records GLIBCXX_3.4.31 in the .so (std::string's
    # _M_replace_cold); the user's older libstdc++ has no such version and
    # REAPER's dlopen fails outright — v0.4.4 was unloadable on MX Linux 23 /
    # Debian 12, whose GCC 12 runtime tops out at GLIBCXX_3.4.30. Statically
    # linking removes the dependency instead of merely lowering it, so the
    # package no longer cares which GCC built it. (The glibc floor is handled
    # separately, by building on the baseline image — see
    # .github/workflows/build.yml and dist/check-linux-abi.py.)
    #
    # --exclude-libs,ALL localises the symbols pulled from the static
    # libstdc++.a / libgcc.a so this module cannot interpose on the C++
    # runtime of REAPER or of any other extension. Safe here because nothing
    # C++ crosses the plugin boundary: the REAPER SDK is a C API, and no
    # SWELL sources are compiled into this target.
    target_link_options(reaper_uf8 PRIVATE
        -static-libstdc++
        -static-libgcc
        -Wl,--exclude-libs,ALL)
endif()

add_dependencies(reaper_uf8 reasixty_commit_count_gen)
add_dependencies(reaper_uf8 reasixty_factory_bundle_gen)
add_dependencies(reaper_uf8 reasixty_input_level_jsfx_gen)
add_dependencies(reaper_uf8 reasixty_inserts_overlay_lua_gen)
add_dependencies(reaper_uf8 reasixty_assignment_hud_lua_gen)
add_dependencies(reaper_uf8 reasixty_focused_panel_lua_gen)
add_dependencies(reaper_uf8 reasixty_mode_banner_lua_gen)
add_dependencies(reaper_uf8 reasixty_user_manual_gen)
# Recompile the manual renderer whenever the embedded header changes so a
# fresh docs/user-manual.md propagates into the binary.
set_source_files_properties(src/ManualView.cpp
    PROPERTIES OBJECT_DEPENDS ${REASIXTY_USER_MANUAL_HEADER})
if(WIN32)
    add_dependencies(reaper_uf8 reasixty_winusb_inf_gen)
endif()
target_include_directories(reaper_uf8 PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    src
    "${REAPER_SDK_DIR}/sdk"
    "${VENDOR_DIR}/reaimgui"
    "${WDL_DIR}"
)

target_link_libraries(reaper_uf8 PRIVATE reasixty_libusb)

# REAPER loads libraries matching "reaper_*.dylib" / "reaper_*.dll". The
# CMake target name stays `reaper_uf8` for historical / build-script
# stability; the output filename reflects the product brand "Rea-Sixty".
set_target_properties(reaper_uf8 PROPERTIES
    PREFIX ""
    OUTPUT_NAME "reaper_rea-sixty"
)

if(APPLE)
    set_target_properties(reaper_uf8 PROPERTIES SUFFIX ".dylib")
elseif(WIN32)
    set_target_properties(reaper_uf8 PROPERTIES SUFFIX ".dll")
endif()

# ----- macOS dylib bundling ---------------------------------------------------
# brew's libusb / hidapi land under its prefix with absolute install
# names. End users don't have brew, so we copy those dylibs next to
# ours and rewrite the install names to @loader_path so REAPER finds
# them in UserPlugins/. With this, the shipped dylib triple is
# self-contained. The brew prefix differs by CPU (Apple Silicon:
# /opt/homebrew, Intel: /usr/local), so ask brew at configure time
# rather than hard-coding either path.
if(APPLE)
    find_program(BREW_EXECUTABLE brew)
    if(NOT BREW_EXECUTABLE)
        message(FATAL_ERROR
            "Homebrew (brew) not found. Install from https://brew.sh, "
            "then: brew install libusb hidapi")
    endif()
    execute_process(COMMAND ${BREW_EXECUTABLE} --prefix libusb
        OUTPUT_VARIABLE LIBUSB_BREW_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _libusb_brew_rc)
    execute_process(COMMAND ${BREW_EXECUTABLE} --prefix hidapi
        OUTPUT_VARIABLE HIDAPI_BREW_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _hidapi_brew_rc)
    if(NOT _libusb_brew_rc EQUAL 0 OR NOT _hidapi_brew_rc EQUAL 0)
        message(FATAL_ERROR
            "Could not query brew for libusb/hidapi prefix. "
            "Run: brew install libusb hidapi")
    endif()

    foreach(LIB libusb-1.0.0 libhidapi.0)
        if(LIB STREQUAL "libusb-1.0.0")
            set(SRC_PATH "${LIBUSB_BREW_PREFIX}/lib/${LIB}.dylib")
        else()
            set(SRC_PATH "${HIDAPI_BREW_PREFIX}/lib/${LIB}.dylib")
        endif()
        set(OLD_NAME "${SRC_PATH}")
        # install_name_tool invalidates the Homebrew lib's code signature.
        # On macOS 15+ REAPER's dlopen path SIGKILLs on the first invalid
        # signature page (Frank 2026-05-19: REAPER crash report
        # CODESIGNING / "Invalid Page" during Plugins_Load). Ad-hoc
        # re-sign each bundled lib after the install_name_tool pass so
        # the on-disk signature matches the rewritten bytes.
        add_custom_command(TARGET reaper_uf8 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SRC_PATH}"
                "$<TARGET_FILE_DIR:reaper_uf8>/${LIB}.dylib"
            COMMAND install_name_tool -change
                "${OLD_NAME}"
                "@loader_path/${LIB}.dylib"
                "$<TARGET_FILE:reaper_uf8>"
            COMMAND install_name_tool -id
                "@loader_path/${LIB}.dylib"
                "$<TARGET_FILE_DIR:reaper_uf8>/${LIB}.dylib"
            COMMAND codesign --force --sign -
                "$<TARGET_FILE_DIR:reaper_uf8>/${LIB}.dylib"
            COMMENT "Bundling ${LIB}.dylib"
            VERBATIM
        )
    endforeach()

    # Our own dylib's signature is invalidated by the install_name_tool
    # -change above. Re-sign it last so the signature reflects the final
    # bytes (including the rewritten @loader_path entries).
    add_custom_command(TARGET reaper_uf8 POST_BUILD
        COMMAND codesign --force --sign - "$<TARGET_FILE:reaper_uf8>"
        COMMENT "Ad-hoc signing reaper_rea-sixty.dylib"
        VERBATIM
    )
endif()

# ----- Linux libusb / hidapi bundling ----------------------------------------
# The macOS mirror. Without this the .so hard-NEEDs libusb-1.0.so.0 and
# libhidapi-hidraw.so.0 from the SYSTEM linker path. libusb is almost always
# present; libhidapi-hidraw0 is almost never preinstalled (minimal Kubuntu /
# MX). When it's missing REAPER's dlopen fails and the plugin is silently
# dropped — installed via ReaPack yet absent from Control/OSC/Web with no
# actions (Frank forum support, 2026-08-04).
#
# Fix: copy the system libusb / hidapi next to our .so under their SONAME and
# give the .so an $ORIGIN RUNPATH so the loader resolves them from its own
# directory. ReaPack installs every file of a type="extension" package into
# UserPlugins (verified against reapack Source::targetPath — routing is by
# package type, not per-file extension), so the bundled copies land in the
# same dir as the .so (= $ORIGIN). Self-contained, no apt step for the user.
if(UNIX AND NOT APPLE)
    set_target_properties(reaper_uf8 PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN")

    # readelf reads the DT_SONAME of the resolved dependency — the exact string
    # our .so records as its DT_NEEDED, so the bundled copy must carry that
    # name to be found. The dev symlink (libusb-1.0.so) and the realpath
    # (libusb-1.0.so.0.3.0) are both the wrong name; only the SONAME matches.
    find_program(READELF_EXECUTABLE NAMES readelf llvm-readelf)
    if(NOT READELF_EXECUTABLE)
        message(FATAL_ERROR
            "readelf not found — needed to read the SONAME of the bundled "
            "libusb / hidapi for the Linux self-contained package. "
            "Install binutils.")
    endif()

    # pkg_check_modules(IMPORTED_TARGET) populated *_LINK_LIBRARIES with
    # absolute paths; pick the entry for each library.
    function(_reasixty_resolve_lib LIST_VAR NEEDLE OUT_PATH)
        foreach(_cand IN LISTS ${LIST_VAR})
            if(_cand MATCHES "${NEEDLE}" AND EXISTS "${_cand}")
                set(${OUT_PATH} "${_cand}" PARENT_SCOPE)
                return()
            endif()
        endforeach()
        set(${OUT_PATH} "" PARENT_SCOPE)
    endfunction()

    function(_reasixty_soname LIB_PATH OUT_NAME)
        execute_process(
            COMMAND "${READELF_EXECUTABLE}" -d "${LIB_PATH}"
            OUTPUT_VARIABLE _readelf_out
            ERROR_QUIET)
        string(REGEX MATCH "Library soname: \\[([^]]+)\\]" _ignored "${_readelf_out}")
        set(${OUT_NAME} "${CMAKE_MATCH_1}" PARENT_SCOPE)
    endfunction()

    _reasixty_resolve_lib(LIBUSB_LINK_LIBRARIES "usb-1\\.0" _reasixty_libusb_path)
    _reasixty_resolve_lib(HIDAPI_LINK_LIBRARIES "hidapi" _reasixty_hidapi_path)
    if(NOT _reasixty_libusb_path OR NOT _reasixty_hidapi_path)
        message(FATAL_ERROR
            "Could not resolve absolute paths to bundle "
            "(libusb='${_reasixty_libusb_path}' hidapi='${_reasixty_hidapi_path}'). "
            "LIBUSB_LINK_LIBRARIES='${LIBUSB_LINK_LIBRARIES}' "
            "HIDAPI_LINK_LIBRARIES='${HIDAPI_LINK_LIBRARIES}'")
    endif()

    _reasixty_soname("${_reasixty_libusb_path}" _reasixty_libusb_soname)
    _reasixty_soname("${_reasixty_hidapi_path}" _reasixty_hidapi_soname)
    if(NOT _reasixty_libusb_soname OR NOT _reasixty_hidapi_soname)
        message(FATAL_ERROR
            "Could not read SONAME via ${READELF_EXECUTABLE} "
            "(libusb='${_reasixty_libusb_soname}' hidapi='${_reasixty_hidapi_soname}').")
    endif()

    # copy_if_different dereferences the symlink and writes the real ELF under
    # its SONAME next to the built .so.
    add_custom_command(TARGET reaper_uf8 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_reasixty_libusb_path}"
            "$<TARGET_FILE_DIR:reaper_uf8>/${_reasixty_libusb_soname}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_reasixty_hidapi_path}"
            "$<TARGET_FILE_DIR:reaper_uf8>/${_reasixty_hidapi_soname}"
        COMMENT "Bundling ${_reasixty_libusb_soname} + ${_reasixty_hidapi_soname} (Linux)"
        VERBATIM
    )

    message(STATUS
        "Rea-Sixty Linux bundle: ${_reasixty_libusb_soname}, "
        "${_reasixty_hidapi_soname} (RUNPATH=\$ORIGIN)")
endif()

# ----- tests + CLI helpers (skipped on Windows for the initial port) ----------
# Tests use POSIX-ish patterns; tools link directly against libusb / hidapi.
# Gated off on Windows so a fresh checkout produces the DLL without forcing
# all the smoke-test wiring to be ported at the same time.
if(NOT WIN32)
    add_executable(test_protocol tests/test_protocol.cpp src/Protocol.cpp src/Palette.cpp)
    target_include_directories(test_protocol PRIVATE src)
    enable_testing()
    add_test(NAME protocol COMMAND test_protocol)

    add_executable(test_uc1_protocol tests/test_uc1_protocol.cpp src/UC1Protocol.cpp)
    target_include_directories(test_uc1_protocol PRIVATE src)
    add_test(NAME uc1_protocol COMMAND test_uc1_protocol)

    add_executable(test_dynamount tests/test_dynamount.cpp
        src/DynaMountClient.cpp src/DynaMountManager.cpp)
    target_include_directories(test_dynamount PRIVATE src)
    find_package(Threads REQUIRED)
    target_link_libraries(test_dynamount PRIVATE Threads::Threads)
    add_test(NAME dynamount COMMAND test_dynamount)

    add_executable(test_ssl_meter tests/test_ssl_meter.cpp src/SslMeterProtocol.cpp)
    target_include_directories(test_ssl_meter PRIVATE src)
    add_test(NAME ssl_meter COMMAND test_ssl_meter)

    # Socket-reclaim regression for the 2026-08-17 host kill. Drives the real
    # impersonator over loopback, so it is POSIX-only (SO_LINGER/RST + /dev/fd)
    # and needs no REAPER API. Exits 77 = skipped when the ports are taken.
    if(NOT WIN32)
        add_executable(test_sslcore_fd_reclaim tests/test_sslcore_fd_reclaim.cpp
            src/SslCoreImpersonator.cpp src/SslMeterProtocol.cpp src/LogPath.cpp)
        target_include_directories(test_sslcore_fd_reclaim PRIVATE src)
        target_link_libraries(test_sslcore_fd_reclaim PRIVATE Threads::Threads)
        add_test(NAME sslcore_fd_reclaim COMMAND test_sslcore_fd_reclaim)
        set_tests_properties(sslcore_fd_reclaim PROPERTIES SKIP_RETURN_CODE 77)
    endif()

    # Includes UserPluginCatalog.cpp directly (see the file header) — do NOT
    # also list it as a source or every symbol lands twice.
    add_executable(test_user_catalog_uf1 tests/test_user_catalog_uf1.cpp
        src/PluginMap.cpp src/LogPath.cpp)
    target_include_directories(test_user_catalog_uf1 PRIVATE
        src "${REAPER_SDK_DIR}/sdk" "${WDL_DIR}")
    add_test(NAME user_catalog_uf1 COMMAND test_user_catalog_uf1)

    add_executable(uf8_color_test tools/uf8_color_test.cpp src/Protocol.cpp)
    target_include_directories(uf8_color_test PRIVATE src)
    target_link_libraries(uf8_color_test PRIVATE reasixty_libusb)

    add_executable(uf8_palette_probe tools/uf8_palette_probe.cpp src/Protocol.cpp)
    target_include_directories(uf8_palette_probe PRIVATE src)
    target_link_libraries(uf8_palette_probe PRIVATE reasixty_libusb)

    add_executable(uf8_hid_probe tools/uf8_hid_probe.cpp)
    target_link_libraries(uf8_hid_probe PRIVATE reasixty_hidapi)

    # SSL 360° Core-impersonation handshake probe (POSIX dev tool).
    add_executable(ssl_core_probe tools/ssl_core_probe.cpp src/SslMeterProtocol.cpp)
    target_include_directories(ssl_core_probe PRIVATE src)
endif()
