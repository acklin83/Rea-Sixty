# Runs on every build. Resolves the current HEAD commit count + the
# nearest version tag via git and writes a small header consumed by
# SettingsScreen.cpp. Skips the write when neither value changed so the
# include is mtime-stable and SettingsScreen.cpp doesn't recompile every
# build for no reason.

if(NOT DEFINED OUTFILE OR NOT DEFINED GIT_ROOT)
    message(FATAL_ERROR "write_commit_count.cmake: OUTFILE and GIT_ROOT required")
endif()

execute_process(
    COMMAND git rev-list --count HEAD
    WORKING_DIRECTORY ${GIT_ROOT}
    OUTPUT_VARIABLE COMMIT_COUNT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE GIT_RC
)

if(NOT GIT_RC EQUAL 0 OR COMMIT_COUNT STREQUAL "")
    set(COMMIT_COUNT 0)
endif()

# `git describe --tags --always --dirty` → "v0.1.7", "v0.1.7-5-g1c923ab"
# (post-release commits), or "v0.1.7-5-g1c923ab-dirty" (uncommitted
# working tree). Falls back to "unknown" when git isn't available or
# the tree has no tags reachable from HEAD.
execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${GIT_ROOT}
    OUTPUT_VARIABLE VERSION_STR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE GIT_DESC_RC
)

if(NOT GIT_DESC_RC EQUAL 0 OR VERSION_STR STREQUAL "")
    set(VERSION_STR "unknown")
endif()

# Strip post-release suffixes ("-5-g1c923ab", "-dirty") so the codename
# lookup matches the most recent tag. A dev build between v0.1.10 and
# v0.1.11 still shows v0.1.10's codename — fine, until v0.1.11 ships
# with its own entry the next codename overrides on the first tagged
# build.
set(VERSION_TAG "${VERSION_STR}")
string(REGEX REPLACE "-[0-9]+-g[0-9a-f]+.*$" "" VERSION_TAG "${VERSION_TAG}")
string(REGEX REPLACE "-dirty$"               "" VERSION_TAG "${VERSION_TAG}")

set(VERSION_NAME "")
# CMakeLists invokes us with -DGIT_ROOT=${CMAKE_SOURCE_DIR}, which is
# the `extension/` directory itself — not the repo root. The TSV sits
# directly inside `extension/`.
set(VERSION_NAMES_FILE "${GIT_ROOT}/version-names.tsv")
if(EXISTS "${VERSION_NAMES_FILE}")
    file(STRINGS "${VERSION_NAMES_FILE}" VN_LINES)
    foreach(LINE IN LISTS VN_LINES)
        # Skip comments + blank lines.
        if(LINE MATCHES "^[ \t]*$" OR LINE MATCHES "^[ \t]*#")
            continue()
        endif()
        # Tag and codename separated by any whitespace run (tab or
        # spaces both fine). Capture tag as the leading non-whitespace
        # token, codename as everything after the first whitespace run.
        string(REGEX MATCH "^([^ \t]+)[ \t]+(.+)$" _ "${LINE}")
        if(CMAKE_MATCH_1 STREQUAL "${VERSION_TAG}")
            set(VERSION_NAME "${CMAKE_MATCH_2}")
            break()
        endif()
    endforeach()
endif()

set(NEW_CONTENT "#pragma once\n#define REASIXTY_COMMIT_COUNT ${COMMIT_COUNT}\n#define REASIXTY_VERSION_STR \"${VERSION_STR}\"\n#define REASIXTY_VERSION_NAME \"${VERSION_NAME}\"\n")

if(EXISTS ${OUTFILE})
    file(READ ${OUTFILE} OLD_CONTENT)
    if(OLD_CONTENT STREQUAL NEW_CONTENT)
        return()
    endif()
endif()

file(WRITE ${OUTFILE} "${NEW_CONTENT}")
