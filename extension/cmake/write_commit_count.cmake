# Runs on every build. Resolves the current HEAD commit count via git
# and writes a small header consumed by SettingsScreen.cpp. Skips the
# write when the count hasn't changed so the include is mtime-stable
# and SettingsScreen.cpp doesn't recompile every build for no reason.

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

set(NEW_CONTENT "#pragma once\n#define REASIXTY_COMMIT_COUNT ${COMMIT_COUNT}\n")

if(EXISTS ${OUTFILE})
    file(READ ${OUTFILE} OLD_CONTENT)
    if(OLD_CONTENT STREQUAL NEW_CONTENT)
        return()
    endif()
endif()

file(WRITE ${OUTFILE} "${NEW_CONTENT}")
