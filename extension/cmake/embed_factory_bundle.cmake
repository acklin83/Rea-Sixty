# Read the canonical factory.rea60config and emit a C++ header that
# exposes its contents as a raw-string literal. Re-runs when the
# resource file changes so a fresh export from Frank propagates to the
# next build without manual steps.

if(NOT DEFINED SRCFILE OR NOT DEFINED OUTFILE)
    message(FATAL_ERROR "embed_factory_bundle.cmake: SRCFILE and OUTFILE required")
endif()

if(NOT EXISTS ${SRCFILE})
    message(FATAL_ERROR "embed_factory_bundle.cmake: SRCFILE not found: ${SRCFILE}")
endif()

file(READ ${SRCFILE} BUNDLE_CONTENT)

# Sanity: raw-string delimiter must not appear in the payload.
string(FIND "${BUNDLE_CONTENT}" ")REASIXTY\"" CLASH_AT)
if(NOT CLASH_AT EQUAL -1)
    message(FATAL_ERROR
        "embed_factory_bundle.cmake: payload contains ')REASIXTY\"' — pick a different raw-string delimiter.")
endif()

set(NEW_CONTENT
"#pragma once
// Auto-generated from extension/resources/factory.rea60config — do not edit.
namespace uf8 {
namespace setup_bundle {
inline constexpr char kFactoryBundle[] = R\"REASIXTY(${BUNDLE_CONTENT})REASIXTY\";
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
