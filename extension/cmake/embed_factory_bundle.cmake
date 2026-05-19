# Read the canonical factory.rea60config and emit a C++ header that
# exposes its contents as a byte array + size. Re-runs when the resource
# file changes so a fresh export from Frank propagates to the next build
# without manual steps.
#
# Why a byte array instead of a raw string literal: MSVC truncates
# string literals beyond 16380 characters (C2026), and the bundle is
# ~47 KB. A byte array bypasses the limit entirely and works the same
# on Clang / GCC / MSVC.

if(NOT DEFINED SRCFILE OR NOT DEFINED OUTFILE)
    message(FATAL_ERROR "embed_factory_bundle.cmake: SRCFILE and OUTFILE required")
endif()

if(NOT EXISTS ${SRCFILE})
    message(FATAL_ERROR "embed_factory_bundle.cmake: SRCFILE not found: ${SRCFILE}")
endif()

file(READ ${SRCFILE} HEX_CONTENT HEX)
string(LENGTH "${HEX_CONTENT}" HEX_LEN)
math(EXPR BYTE_COUNT "${HEX_LEN} / 2")

# Emit 16 bytes per line for readable diffs.
set(BYTES "")
set(IDX 0)
while(IDX LESS HEX_LEN)
    string(SUBSTRING "${HEX_CONTENT}" ${IDX} 2 BYTE)
    if(BYTES STREQUAL "")
        set(BYTES "0x${BYTE}")
    else()
        # Insert newlines every 32 hex chars (16 bytes) for legibility.
        math(EXPR BYTES_SO_FAR "${IDX} / 2")
        math(EXPR LINE_BREAK "${BYTES_SO_FAR} % 16")
        if(LINE_BREAK EQUAL 0)
            set(BYTES "${BYTES},\n    0x${BYTE}")
        else()
            set(BYTES "${BYTES}, 0x${BYTE}")
        endif()
    endif()
    math(EXPR IDX "${IDX} + 2")
endwhile()

# Symbol name override for reuse (Windows WinUSB INF uses the same
# helper). Defaults to the factory bundle's name when not overridden.
if(NOT SYMBOL)
    set(SYMBOL "kFactoryBundle")
endif()

set(NEW_CONTENT
"#pragma once
// Auto-generated from ${SRCFILE} — do not edit.
#include <cstddef>
namespace uf8 {
namespace setup_bundle {
inline constexpr unsigned char ${SYMBOL}Bytes[] = {
    ${BYTES}
};
inline constexpr size_t ${SYMBOL}Size = ${BYTE_COUNT};
}
}
")

if(EXISTS ${OUTFILE})
    file(READ ${OUTFILE} OLD_CONTENT)
    if(OLD_CONTENT STREQUAL NEW_CONTENT)
        return()
    endif()
endif()

file(WRITE ${OUTFILE} "${NEW_CONTENT}")
