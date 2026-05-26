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

set(NEW_CONTENT "#pragma once\n#define REASIXTY_COMMIT_COUNT ${COMMIT_COUNT}\n#define REASIXTY_VERSION_STR \"${VERSION_STR}\"\n")

if(EXISTS ${OUTFILE})
    file(READ ${OUTFILE} OLD_CONTENT)
    if(OLD_CONTENT STREQUAL NEW_CONTENT)
        return()
    endif()
endif()

file(WRITE ${OUTFILE} "${NEW_CONTENT}")
