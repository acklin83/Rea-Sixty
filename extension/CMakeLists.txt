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
    src/HidDevice.cpp
    # Phase 2.6 — Plugin Mixer Window + Phase 2.7 — Settings Screen
    src/MixerWindow.cpp
    src/MixerLayout.cpp
    src/ManualView.cpp
    src/KnobFeelPresets.cpp
    src/SettingsScreen.cpp
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
endif()

# SWELL's swell-types.h #defines min/max as 2-arg macros, which collide
# with libstdc++'s 3-arg min/max overloads on Linux (and would on Windows
# too without NOMINMAX). The codebase already uses std::min/std::max
# everywhere, so opt out of SWELL's macros project-wide.
target_compile_definitions(reaper_uf8 PRIVATE WDL_NO_DEFINE_MINMAX)

target_link_libraries(reaper_uf8 PRIVATE reasixty_hidapi)

if(APPLE)
    target_link_libraries(reaper_uf8 PRIVATE
        "-framework CoreMIDI"
        "-framework CoreFoundation"
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
    target_link_libraries(reaper_uf8 PRIVATE delayimp.lib comdlg32.lib)
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
