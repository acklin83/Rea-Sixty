#pragma once

// ⛔ wdl_json_parser DOES NOT FREE THE TREE IT HANDS BACK.
//
// Its destructor empties only the SPARE lists, i.e. whatever `dispose_element()`
// has returned to the pool. A tree still standing when the parser dies is leaked
// element by element, plus the WDL_PtrList heap buffer behind every array. Every
// call site in this project got that wrong, and it stayed invisible for as long
// as the parsing happened once, at load.
//
// Then the Hue worker started parsing the bridge's lights, grouped lights and
// scenes ONCE A SECOND. On 2026-08-27 that took Frank's Mac Studio down: REAPER
// at 171 GB, the kernel's jetsam report naming it as the largest process, the
// machine out of memory with the session open. malloc_history put the whole
// weight on one stack, HueManager::runRefresh -> parseScenes -> the parser.
//
// So: never hold a bare wdl_json_parser. Declare the parser, parse, and put this
// guard right underneath. It hands the tree back on every exit path, including
// the early returns that made "just call dispose at the end" the wrong fix.
//
//   wdl_json_parser p;
//   const wdl_json_element* root = p.parse(s.c_str(), (int)s.size());
//   JsonTreeGuard rootGuard{p, root};
//
// The strings the elements point into live in the parser's stringstore, which
// outlives the guard, so values read before the scope ends stay valid.

#include "WDL/jsonparse.h"

struct JsonTreeGuard {
    JsonTreeGuard(wdl_json_parser& p, const wdl_json_element* r)
        : parser_(p), root_(r) {}

    ~JsonTreeGuard()
    {
        parser_.dispose_element(const_cast<wdl_json_element*>(root_));
    }

    JsonTreeGuard(const JsonTreeGuard&)            = delete;
    JsonTreeGuard& operator=(const JsonTreeGuard&) = delete;

  private:
    wdl_json_parser&        parser_;
    const wdl_json_element* root_;
};
