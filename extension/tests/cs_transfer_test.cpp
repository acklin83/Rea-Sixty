// Standalone test harness for CS-Switch value transfer (applyParamValue_).
//
// It re-implements the PURE search / decision logic of applyParamValue_ from
// extension/src/main.cpp, parameterised by a pluggable "destination param model".
//
// A destination param model has TWO halves, mirroring a real VST3 plug-in:
//   commit(n)  -> the normalised value the plug-in ACTUALLY stores when you write
//                 n (the internal quantiser). For a continuous param this is the
//                 identity; for a stepped param it snaps to the nearest detent.
//   display(n) -> the string the plug-in's GetFormattedParamValue returns.
//
// The crucial subtlety the real bx_console SSL 4000 G exhibits: its display() is
// computed from the *requested* norm (the linear dB formula), NOT from the
// committed/snapped norm. So GetFormattedParamValue right after a write can LIE
// about what the plug-in committed. The only API witness of the truth is
// GetParamNormalized, which returns commit(n). The fix makes the search read that
// back and format AT the committed norm, so the optimiser targets the COMMITTED
// value.
//
// Build + run:  clang++ -std=c++17 -O2 -o /tmp/cstest cs_transfer_test.cpp && /tmp/cstest

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <functional>
#include <vector>

// ---------------------------------------------------------------------------
// Mirror of the relevant types/parsers from main.cpp (kept byte-for-byte where
// it matters: parseUnitValue_ and unitTol_ are copied verbatim so the harness
// exercises the SAME parse/tolerance the extension uses).
// ---------------------------------------------------------------------------
enum class CsUnit { None, Hz, Db, Ms, Ratio, Pct, St, Deg };
struct CsUnitVal { bool ok = false; double base = 0.0; CsUnit dim = CsUnit::None; };
enum class CsQuantityKind { Generic, Db, Freq, Time, Ratio, Q, Pct };

static CsUnitVal parseUnitValue_(const char* s)
{
    CsUnitVal r;
    if (!s) return r;
    while (*s == ' ' || *s == '\t') ++s;
    char num[48]; int n = 0;
    const char* p = s;
    if (*p == '+' || *p == '-') num[n++] = *p++;
    bool anyDigit = false;
    while (*p && n < 46 && ((*p >= '0' && *p <= '9') || *p == '.' || *p == ',')) {
        if (*p != ',') { num[n] = *p; if (*p >= '0' && *p <= '9') anyDigit = true; ++n; }
        ++p;
    }
    if ((*p == 'e' || *p == 'E') && n < 44) {
        num[n++] = *p++;
        if ((*p == '+' || *p == '-') && n < 46) num[n++] = *p++;
        while (*p >= '0' && *p <= '9' && n < 46) num[n++] = *p++;
    }
    num[n] = 0;
    if (!anyDigit) return r;
    const double v = std::strtod(num, nullptr);
    const char* u = p;
    while (*u == ' ' || *u == '\t') ++u;
    if (*u == ':') {
        char* e2 = nullptr;
        const double d = std::strtod(u + 1, &e2);
        r.ok = true; r.dim = CsUnit::Ratio;
        r.base = (e2 != u + 1 && d != 0.0) ? v / d : v;
        return r;
    }
    char t[8] = {0};
    for (int i = 0; i < 7 && u[i]; ++i) { char c = u[i]; if (c >= 'A' && c <= 'Z') c += 32; t[i] = c; }
    auto pre = [&](const char* pp) { for (int i = 0; pp[i]; ++i) if (t[i] != pp[i]) return false; return true; };
    double mult = 1.0; CsUnit dim = CsUnit::None;
    if      (t[0] == 'k')  { mult = 1000.0; dim = pre("khz") ? CsUnit::Hz : CsUnit::None; }
    else if (pre("hz"))   { dim = CsUnit::Hz; }
    else if (pre("db"))   { dim = CsUnit::Db; }
    else if (pre("ms"))   { dim = CsUnit::Ms; }
    else if (pre("us"))   { dim = CsUnit::Ms; mult = 0.001; }
    else if (pre("st") || pre("semi")) { dim = CsUnit::St; }
    else if (pre("deg"))  { dim = CsUnit::Deg; }
    else if (pre("s"))    { dim = CsUnit::Ms; mult = 1000.0; }
    else if (t[0] == '%') { dim = CsUnit::Pct; }
    r.ok = true; r.base = v * mult; r.dim = dim;
    return r;
}

static double unitTol_(CsUnit u, double v)
{
    const double a = std::fabs(v);
    switch (u) {
        case CsUnit::Db:    return 0.1;
        case CsUnit::Hz:    return std::fmax(0.5,  a * 0.01);
        case CsUnit::Ms:    return std::fmax(0.05, a * 0.02);
        case CsUnit::Ratio: return std::fmax(0.02, a * 0.02);
        case CsUnit::Pct:   return 0.5;
        case CsUnit::St:    return 0.05;
        case CsUnit::Deg:   return 1.0;
        default:            return std::fmax(1e-4, a * 0.01);
    }
}

struct CsVal {
    bool   valid   = false;
    bool   numeric = false;
    double eng     = 0.0;
    double norm    = 0.0;
    char   text[24] = {0};
    CsQuantityKind kind = CsQuantityKind::Generic;
    CsUnit dim     = CsUnit::None;
    double appliedNorm = -1.0;
};

// ---------------------------------------------------------------------------
// Destination param model. commit() = internal quantiser (what GetParamNormalized
// returns). display() = GetFormattedParamValue (may be computed from the
// REQUESTED norm, i.e. may LIE). The harness drives them exactly like the
// extension drives TrackFX_SetParamNormalized / GetParamNormalized /
// GetFormattedParamValue.
// ---------------------------------------------------------------------------
struct DstModel {
    std::function<double(double)>            commit;   // requested norm -> stored norm
    std::function<std::string(double)>       display;  // stored... or requested? per-model
    bool displayFromRequested = false;  // true = display lies (uses requested norm)
};

// --- helpers to build models -----------------------------------------------
static std::string fmt1(double x) { char b[32]; std::snprintf(b, sizeof(b), "%.1f", x); return b; }
static std::string fmt1sign(double x) { char b[32]; std::snprintf(b, sizeof(b), "%+.1f", x); return b; }

// Continuous linear dB [-20,20], one-decimal, "+x.x" sign format (bx-like display).
static DstModel modelLinearDbBx(double lo, double hi) {
    DstModel m;
    m.commit  = [](double n){ return n; };
    m.display = [lo,hi](double n){ return fmt1sign(lo + n*(hi-lo)); };
    return m;
}
// Continuous linear dB, plain "%.1f" (4K E style, no forced sign).
static DstModel modelLinearDb(double lo, double hi) {
    DstModel m;
    m.commit  = [](double n){ return n; };
    m.display = [lo,hi](double n){ return fmt1(lo + n*(hi-lo)); };
    return m;
}
// Continuous linear dB with an explicit "dB" suffix in the readout (so the
// cross-dimension guard can actually recognise the destination's dimension).
static DstModel modelLinearDbUnit(double lo, double hi) {
    DstModel m;
    m.commit  = [](double n){ return n; };
    m.display = [lo,hi](double n){ return fmt1(lo + n*(hi-lo)) + " dB"; };
    return m;
}
// Continuous log frequency. Display shows kHz>=1000 as "x.xk", else "%.1f".
static DstModel modelLogFreq(double lo, double hi) {
    DstModel m;
    m.commit  = [](double n){ return n; };
    m.display = [lo,hi](double n){
        double f = lo * std::pow(hi/lo, n);
        char b[32];
        if (f >= 1000.0) std::snprintf(b, sizeof(b), "%.3gk", f/1000.0);
        else             std::snprintf(b, sizeof(b), "%.1f", f);
        return std::string(b);
    };
    return m;
}
// Coarse-stepped dB: only multiples of `step` exist. commit snaps the norm to the
// nearest detent; display reads the COMMITTED value (honest stepped plug-in).
static DstModel modelSteppedDbHonest(double lo, double hi, double step) {
    DstModel m;
    auto snapNorm = [lo,hi,step](double n){
        double v = lo + n*(hi-lo);
        double q = std::round(v/step)*step;
        return (q - lo)/(hi - lo);
    };
    m.commit  = snapNorm;
    m.display = [lo,hi,snapNorm](double n){ return fmt1sign(lo + snapNorm(n)*(hi-lo)); };
    return m;
}
// READOUT-LIES dB: internal grid is coarse (step), but GetFormattedParamValue is
// computed from the REQUESTED norm (the fine linear formula) — so it shows e.g.
// "-2.4" while the committed value is the snapped -2.0/-2.5. This reproduces the
// bx_console SSL 4000 G EQ High Gain failure.
static DstModel modelReadoutLies(double lo, double hi, double step) {
    DstModel m;
    m.commit  = [lo,hi,step](double n){
        double v = lo + n*(hi-lo);
        double q = std::round(v/step)*step;
        return (q - lo)/(hi - lo);
    };
    m.display = [lo,hi](double n){ return fmt1sign(lo + n*(hi-lo)); }; // LIES: from requested n
    m.displayFromRequested = true;
    return m;
}

// ---------------------------------------------------------------------------
// The transfer engine under test. `committed` selects the FIX behaviour:
//   false = LEGACY  : dstAt formats at the REQUESTED norm (old code).
//   true  = FIXED   : dstAt commits (GetParamNormalized) then formats at the
//                     COMMITTED norm — search optimises the real value.
// Returns the COMMITTED engineering value the model would show in its GUI after
// the transfer (i.e. parse(display(commit(writtenNorm)))), which is the only
// thing the user sees. That is what the assertions check.
// ---------------------------------------------------------------------------
struct Result { double writtenNorm; double committedEng; std::string committedDisp; bool resolvable; };

static Result transfer(const DstModel& M, const CsVal& v, bool committed)
{
    // Honest readout of the model: SetParamNormalized(n) then GetFormatted.
    // LEGACY: GetFormatted uses requested n. FIXED: re-read committed norm first.
    auto formatAt = [&](double reqN, double& usedN) -> std::string {
        double storeN = M.commit(reqN);          // plug-in stores this
        usedN = committed ? storeN : reqN;        // FIXED reads committed; LEGACY uses requested
        if (M.displayFromRequested && !committed) // legacy + lying model: display(requested)
            return M.display(reqN);
        // honest model: display is a pure function of stored value anyway.
        // For the lying model in FIXED mode we must format the COMMITTED value;
        // the lying display() takes a norm and applies the linear formula, so we
        // feed it the committed norm to get the truthful string.
        return M.display(usedN);
    };
    auto dstAt = [&](double n, CsUnitVal& out, std::string& str) -> bool {
        double usedN;
        str = formatAt(n, usedN);
        out = parseUnitValue_(str.c_str());
        return out.ok;
    };

    const int N = 64;
    Result R{}; R.writtenNorm = v.norm;

    if (v.kind == CsQuantityKind::Generic) {
        R.writtenNorm = v.norm;
        double usedN; R.committedDisp = formatAt(v.norm, usedN);
        CsUnitVal e = parseUnitValue_(R.committedDisp.c_str());
        R.committedEng = e.ok ? e.base : v.eng;
        R.resolvable = false;
        return R;
    }

    // Pass 1 — sample range + dimension.
    std::vector<double> sN, sB;
    double gMin = 1e300, gMax = -1e300;
    CsUnit dstDim = CsUnit::None; bool dstDimMixed = false;
    for (int i = 0; i <= N; ++i) {
        const double n = (double)i / N;
        CsUnitVal e; std::string b;
        if (!dstAt(n, e, b)) continue;
        sN.push_back(n); sB.push_back(e.base);
        if (e.base < gMin) gMin = e.base;
        if (e.base > gMax) gMax = e.base;
        if (e.dim != CsUnit::None) {
            if (dstDim == CsUnit::None) dstDim = e.dim;
            else if (dstDim != e.dim)   dstDimMixed = true;
        }
    }
    const bool resolvable = !sN.empty();
    R.resolvable = resolvable;

    // CROSS-DIM guard.
    if (resolvable && !dstDimMixed && dstDim != CsUnit::None
        && v.dim != CsUnit::None && v.dim != dstDim) {
        R.writtenNorm = v.norm;
        double usedN; R.committedDisp = formatAt(v.norm, usedN);
        CsUnitVal e = parseUnitValue_(R.committedDisp.c_str());
        R.committedEng = e.ok ? e.base : v.eng;
        return R;
    }

    // EXACT shortcut.
    if (resolvable) {
        CsUnitVal e0; std::string b0;
        if (dstAt(v.norm, e0, b0)
            && std::fabs(e0.base - v.eng) <= unitTol_(e0.dim, v.eng)) {
            R.writtenNorm = v.norm;
            double usedN; R.committedDisp = formatAt(v.norm, usedN);
            CsUnitVal e = parseUnitValue_(R.committedDisp.c_str());
            R.committedEng = e.ok ? e.base : v.eng;
            return R;
        }
    }

    // SCALE reconcile (Freq/Time only).
    double target = v.eng;
    const bool scalable = (v.kind == CsQuantityKind::Freq || v.kind == CsQuantityKind::Time);
    if (resolvable && scalable && gMin > 0.0 && target > 0.0
        && (target < gMin || target > gMax)) {
        const double facs[] = { 1000.0, 0.001, 1e6, 1e-6 };
        for (double f : facs) {
            const double t2 = target * f;
            if (t2 < gMin || t2 > gMax) continue;
            const double off = (target > gMax) ? target / gMax : gMin / target;
            if (off >= 10.0) { target = t2; break; }
        }
    }
    // OUT-OF-RANGE guard.
    if (resolvable && scalable && gMin > 0.0 && target > 0.0
        && (target > gMax * 1.5 || target < gMin / 1.5)) {
        R.writtenNorm = v.norm;
        double usedN; R.committedDisp = formatAt(v.norm, usedN);
        CsUnitVal e = parseUnitValue_(R.committedDisp.c_str());
        R.committedEng = e.ok ? e.base : v.eng;  // model default-ish; not asserted in OOR tests
        return R;
    }

    // Pass 2 — nearest-display search (coarse + fine), strict min, break on err==0.
    double bestN = v.norm, bestErr = 1e300;
    if (resolvable) {
        for (size_t i = 0; i < sN.size(); ++i) {
            const double err = std::fabs(sB[i] - target);
            if (err < bestErr) { bestErr = err; bestN = sN[i]; }
        }
        const double span = 1.0 / N;
        const double lo = std::fmax(0.0, bestN - span), hi = std::fmin(1.0, bestN + span);
        const int FINE = 200;
        for (int i = 0; i <= FINE; ++i) {
            const double n = lo + (hi - lo) * i / FINE;
            CsUnitVal e; std::string b;
            if (!dstAt(n, e, b)) continue;
            const double err = std::fabs(e.base - target);
            if (err < bestErr) { bestErr = err; bestN = n; }
            if (err == 0.0) break;
        }
    }
    R.writtenNorm = bestN;
    double usedN; R.committedDisp = formatAt(bestN, usedN);
    CsUnitVal e = parseUnitValue_(R.committedDisp.c_str());
    R.committedEng = e.ok ? e.base : target;
    return R;
}

// ---------------------------------------------------------------------------
// Test scaffolding
// ---------------------------------------------------------------------------
static int g_fail = 0, g_pass = 0;

static CsVal mkVal(double eng, double norm, CsQuantityKind k, CsUnit dim) {
    CsVal v; v.valid = true; v.numeric = true; v.eng = eng; v.norm = norm; v.kind = k; v.dim = dim;
    return v;
}

static void check(const char* name, const DstModel& M, const CsVal& v,
                  double expect, double tol, bool committed)
{
    Result R = transfer(M, v, committed);
    bool ok = std::fabs(R.committedEng - expect) <= tol;
    std::printf("  [%s] %-44s committed=%-8.4g (disp='%s' n=%.5f)  expect=%.4g  %s\n",
                ok ? "PASS" : "FAIL", name, R.committedEng, R.committedDisp.c_str(),
                R.writtenNorm, expect, ok ? "" : "  <-- MISMATCH");
    if (ok) ++g_pass; else ++g_fail;
}

int main()
{
    // Norm such that bx linear [-20,20] reads the source engineering value.
    auto bxNorm = [](double db){ return (db + 20.0) / 40.0; };
    auto eNorm  = [](double db, double lo, double hi){ return (db - lo)/(hi - lo); };

    std::printf("=== LEGACY (old code: display at requested norm) ===\n");
    {
        // Must-pass clean cases still work in legacy.
        check("SSL->SSL dB +5.0 bit-exact", modelLinearDb(-20,20),
              mkVal(5.0, eNorm(5,-20,20), CsQuantityKind::Db, CsUnit::Db), 5.0, 0.05, false);
        check("dB -3.8 (4K E [-22,22])", modelLinearDb(-22,22),
              mkVal(-3.8, eNorm(-3.8,-22,22), CsQuantityKind::Db, CsUnit::Db), -3.8, 0.05, false);
        check("Freq 8.5k (log 1500..16k)", modelLogFreq(1500,16000),
              mkVal(8500, 0.5, CsQuantityKind::Freq, CsUnit::Hz), 8500, 60, false);
        check("Freq 233 (log 16..350)", modelLogFreq(16,350),
              mkVal(233, 0.5, CsQuantityKind::Freq, CsUnit::Hz), 233, 3, false);
        check("Freq 10k (log 3000..22k)", modelLogFreq(3000,22000),
              mkVal(10000, 0.5, CsQuantityKind::Freq, CsUnit::Hz), 10000, 80, false);
        // The bx readout-lies failure — LEGACY lands on the LYING display value
        // -2.4, whose committed value (snapped to the 0.5 grid) is -2.0. This is
        // the reported bug. We assert the legacy WRONG value to document it.
        check("bx -2.38 legacy lands lying -2.4 (commits -2.0)", modelReadoutLies(-20,20,0.5),
              mkVal(-2.38, bxNorm(-2.38), CsQuantityKind::Db, CsUnit::Db),
              /*expect the WRONG legacy display*/ -2.4, 0.05, false);
    }

    std::printf("\n=== FIXED (commit-aware: re-read committed norm, format there) ===\n");
    {
        // 1. SSL<->SSL bit-exact dB (EXACT path).
        check("SSL->SSL dB +5.0 bit-exact", modelLinearDb(-20,20),
              mkVal(5.0, eNorm(5,-20,20), CsQuantityKind::Db, CsUnit::Db), 5.0, 0.05, true);
        check("SSL->SSL dB -3.8 bit-exact", modelLinearDb(-22,22),
              mkVal(-3.8, eNorm(-3.8,-22,22), CsQuantityKind::Db, CsUnit::Db), -3.8, 0.05, true);
        check("SSL->SSL dB -1.2 bit-exact", modelLinearDb(-22,22),
              mkVal(-1.2, eNorm(-1.2,-22,22), CsQuantityKind::Db, CsUnit::Db), -1.2, 0.05, true);

        // 2. Cross-range dB into bx (sign-format display), clean values.
        check("dB +5.0 -> bx [-20,20]", modelLinearDbBx(-20,20),
              mkVal(5.0, 0.5 /*src norm differs*/, CsQuantityKind::Db, CsUnit::Db), 5.0, 0.05, true);
        check("dB -3.8 -> bx [-20,20]", modelLinearDbBx(-20,20),
              mkVal(-3.8, 0.5, CsQuantityKind::Db, CsUnit::Db), -3.8, 0.05, true);

        // 3. Frequency (log) must-pass.
        check("Freq 8.5k (log 1500..16k)", modelLogFreq(1500,16000),
              mkVal(8500, 0.5, CsQuantityKind::Freq, CsUnit::Hz), 8500, 60, true);
        check("Freq 233 (log 16..350)", modelLogFreq(16,350),
              mkVal(233, 0.5, CsQuantityKind::Freq, CsUnit::Hz), 233, 3, true);
        check("Freq 10k (log 3000..22k)", modelLogFreq(3000,22000),
              mkVal(10000, 0.5, CsQuantityKind::Freq, CsUnit::Hz), 10000, 80, true);
        check("Freq 11.5k (log 3000..22k)", modelLogFreq(3000,22000),
              mkVal(11500, 0.5, CsQuantityKind::Freq, CsUnit::Hz), 11500, 120, true);

        // 4. Honest stepped dB (API-style 0.5 dB grid) — must land on a real step.
        check("stepped dB +5.0 (0.5 grid)", modelSteppedDbHonest(-20,20,0.5),
              mkVal(5.0, 0.5, CsQuantityKind::Db, CsUnit::Db), 5.0, 0.26, true);
        check("stepped dB -3.8 (0.5 grid -> -4.0)", modelSteppedDbHonest(-20,20,0.5),
              mkVal(-3.8, 0.5, CsQuantityKind::Db, CsUnit::Db), -4.0, 0.26, true);

        // 5. THE FAILURE: bx readout-lies, src -2.38. With 0.5 dB internal grid the
        //    truthful committed value nearest -2.38 is -2.5. FIXED must land -2.5,
        //    NOT the lying -2.4 (which actually commits as -2.0 in legacy).
        check("bx -2.38 READOUT-LIES -> -2.5 committed", modelReadoutLies(-20,20,0.5),
              mkVal(-2.38, bxNorm(-2.38), CsQuantityKind::Db, CsUnit::Db), -2.5, 0.05, true);
        check("bx -2.0 READOUT-LIES stays -2.0", modelReadoutLies(-20,20,0.5),
              mkVal(-2.0, bxNorm(-2.0), CsQuantityKind::Db, CsUnit::Db), -2.0, 0.05, true);
        check("bx +5.0 READOUT-LIES stays +5.0", modelReadoutLies(-20,20,0.5),
              mkVal(5.0, bxNorm(5.0), CsQuantityKind::Db, CsUnit::Db), 5.0, 0.05, true);

        // 6. Generic never searches — passes normalised through.
        {
            CsVal g = mkVal(8500, 0.7, CsQuantityKind::Generic, CsUnit::Hz);
            Result R = transfer(modelLinearDbBx(-20,20), g, true);
            bool ok = std::fabs(R.writtenNorm - 0.7) < 1e-9;
            std::printf("  [%s] %-44s writtenNorm=%.5f (expect 0.7 passthrough)\n",
                        ok?"PASS":"FAIL", "Generic Hz->dB passthrough", R.writtenNorm);
            if (ok) ++g_pass; else ++g_fail;
        }

        // 7. Cross-dimension guard: src Hz, dst dB -> normalised passthrough.
        {
            CsVal x = mkVal(8500, 0.42, CsQuantityKind::Freq, CsUnit::Hz);
            Result R = transfer(modelLinearDbUnit(-20,20), x, true);
            bool ok = std::fabs(R.writtenNorm - 0.42) < 1e-9;
            std::printf("  [%s] %-44s writtenNorm=%.5f (expect 0.42 passthrough)\n",
                        ok?"PASS":"FAIL", "Cross-dim Hz->dB guard", R.writtenNorm);
            if (ok) ++g_pass; else ++g_fail;
        }

        // 8. Out-of-range freq must NOT rail (returns without searching the rail).
        //    src 10800 Hz onto [16,350] Hz param -> writtenNorm stays the src norm
        //    (guard kept default), never a rail (0 or 1).
        {
            CsVal x = mkVal(10800, 0.33, CsQuantityKind::Freq, CsUnit::Hz);
            Result R = transfer(modelLogFreq(16,350), x, true);
            bool ok = R.writtenNorm > 0.0 && R.writtenNorm < 1.0;  // not railed
            std::printf("  [%s] %-44s writtenNorm=%.5f (expect not railed)\n",
                        ok?"PASS":"FAIL", "Out-of-range freq no-rail", R.writtenNorm);
            if (ok) ++g_pass; else ++g_fail;
        }
    }

    std::printf("\n=== SUMMARY: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
