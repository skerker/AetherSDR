// Unit tests for DaxTxWatchdogPolicy (2b DAX-TX watchdog decision core).
//
// Pins the state machine that decides WHEN a DAX-TX stream reset should fire on
// the TX low-power/DAX-stall fault. Pure logic, no radio: we synthesize TX/RX
// cycles and assert the emitted Decision.
//
// Three power states at a fixed ~50 W drive:
//   Good (>10 W) — matches slider; Low (0-10 W) — degraded but alive;
//   Zero (<=0.5 W) — dead-stream stall. ONLY Zero escalates to a reset by
//   default; Low is captured (LowPowerObserved) but not acted on.
//
// Guards: zero-only reset, low captured-not-acted, RX-gap-only, confirm-K
// debounce, staleness guard (never a false fault), no reset storm (grace).

#include "models/DaxTxWatchdogPolicy.h"

#include <iostream>

using namespace AetherSDR;
using namespace AetherSDR::DaxTxWatchdogPolicy;

namespace {

bool expect(bool condition, const char* label)
{
    std::cout << (condition ? "[ OK ] " : "[FAIL] ") << label << '\n';
    return condition;
}

// Drive one complete TX cycle (rising edge -> `txTicks` transmit ticks -> the
// falling edge) followed by a quiet RX tick, and return the decision emitted at
// the FALLING EDGE (where a real reset would fire). `powerW` is the forward
// power reported for the whole cycle; `ageMs` its sample age; `t` a mutable
// clock advanced one ms per tick.
Decision runCycle(Watchdog& wd, double powerW, qint64 ageMs, qint64& t,
                  int txTicks = 3)
{
    for (int i = 0; i < txTicks; ++i)
        wd.update({.transmitting = true, .fwdPowerW = powerW,
                   .powerSampleAgeMs = ageMs, .nowMs = t++});
    const Decision d = wd.update({.transmitting = false, .fwdPowerW = powerW,
                                  .powerSampleAgeMs = ageMs, .nowMs = t++});
    wd.update({.transmitting = false, .powerSampleAgeMs = ageMs, .nowMs = t++});
    return d;
}

constexpr double kGood = 50.0;  // healthy forward power (W)
constexpr double kLow  = 8.0;   // low-but-nonzero (~0-10 W) — degraded, alive
constexpr double kZero = 0.0;   // ~0 W — dead-stream stall

} // namespace

int main()
{
    bool ok = true;

    // 0. classifyPower boundaries.
    {
        Config c;  // zeroCeil 0.5, lowCeil 10
        ok &= expect(classifyPower(0.0, c)  == PowerClass::Zero
                         && classifyPower(0.5, c) == PowerClass::Zero
                         && classifyPower(1.0, c) == PowerClass::Low
                         && classifyPower(10.0, c) == PowerClass::Low
                         && classifyPower(50.0, c) == PowerClass::Good,
                     "classifyPower splits zero / low / good at the ceilings");
    }

    // 1. Steady good power across many cycles — never escalates.
    {
        Watchdog wd;
        qint64 t = 0;
        bool anyReset = false;
        for (int c = 0; c < 10; ++c)
            anyReset |= (runCycle(wd, kGood, 0, t) == Decision::WouldReset);
        ok &= expect(!anyReset && wd.faultedCycles() == 0,
                     "steady good power never triggers a reset");
    }

    // 2. K consecutive ZERO cycles — WouldReset on exactly the Kth falling edge.
    {
        Watchdog wd(Config{.confirmCycles = 2});
        qint64 t = 0;
        const Decision c1 = runCycle(wd, kZero, 0, t);
        const Decision c2 = runCycle(wd, kZero, 0, t);
        ok &= expect(c1 == Decision::Arming && c2 == Decision::WouldReset,
                     "K=2 zero cycles: arm then reset on the 2nd");
    }

    // 3. LOW power is captured, never reset (default resetOnLowPower=false) —
    //    even sustained across many cycles.
    {
        Watchdog wd(Config{.confirmCycles = 2});
        qint64 t = 0;
        bool anyReset = false;
        Decision last = Decision::Idle;
        for (int c = 0; c < 6; ++c) {
            last = runCycle(wd, kLow, 0, t);
            anyReset |= (last == Decision::WouldReset);
        }
        ok &= expect(!anyReset && last == Decision::LowPowerObserved
                         && wd.faultedCycles() == 0
                         && wd.lastCycleClass() == PowerClass::Low,
                     "low power is captured (LowPowerObserved), never reset");
    }

    // 3b. LOW cycle breaks the consecutive-ZERO streak (only sustained zero escalates).
    {
        Watchdog wd(Config{.confirmCycles = 2});
        qint64 t = 0;
        const Decision z1  = runCycle(wd, kZero, 0, t);  // arm (zero 1/2)
        const Decision low = runCycle(wd, kLow,  0, t);  // breaks streak -> low
        const Decision z2  = runCycle(wd, kZero, 0, t);  // arm again (1/2), NOT reset
        ok &= expect(z1 == Decision::Arming && low == Decision::LowPowerObserved
                         && z2 == Decision::Arming && wd.faultedCycles() == 1,
                     "a low cycle breaks the zero streak (zero must be sustained)");
    }

    // 4. Opt-in: with resetOnLowPower, K low cycles DO escalate.
    {
        Watchdog wd(Config{.resetOnLowPower = true, .confirmCycles = 2});
        qint64 t = 0;
        const Decision c1 = runCycle(wd, kLow, 0, t);
        const Decision c2 = runCycle(wd, kLow, 0, t);
        ok &= expect(c1 == Decision::Arming && c2 == Decision::WouldReset,
                     "resetOnLowPower opt-in escalates sustained low power");
    }

    // 5. Staleness guard — only-stale lane is never a fault, ever.
    {
        Watchdog wd(Config{.confirmCycles = 2, .powerStaleMs = 3000});
        qint64 t = 0;
        bool anyReset = false;
        for (int c = 0; c < 6; ++c)  // zero power BUT every sample stale (10 s old)
            anyReset |= (runCycle(wd, kZero, /*ageMs=*/10000, t) == Decision::WouldReset);
        ok &= expect(!anyReset && wd.faultedCycles() == 0,
                     "stale power lane never counts as a fault");
    }

    // 5b. Stale mid-TX reports SuppressedStale.
    {
        Watchdog wd;
        const Decision mid = wd.update({.transmitting = true, .fwdPowerW = kZero,
                                        .powerSampleAgeMs = 10000, .nowMs = 0});
        ok &= expect(mid == Decision::SuppressedStale,
                     "stale samples mid-TX report SuppressedStale");
    }

    // 6. RX-gap-only — WouldReset never emitted while transmitting, even after
    //    many zero-power ticks within one long TX cycle.
    {
        Watchdog wd(Config{.confirmCycles = 2});
        qint64 t = 0;
        bool resetMidTx = false;
        for (int i = 0; i < 20; ++i) {
            const Decision d = wd.update({.transmitting = true, .fwdPowerW = kZero,
                                          .powerSampleAgeMs = 0, .nowMs = t++});
            resetMidTx |= (d == Decision::WouldReset);
        }
        ok &= expect(!resetMidTx, "never resets mid-TX (RX-gap-only)");
    }

    // 7. Debounce + recovery — a lone zero cycle only arms; a good cycle clears
    //    the count, so the next lone zero cycle arms again (not reset).
    {
        Watchdog wd(Config{.confirmCycles = 2});
        qint64 t = 0;
        const Decision f1   = runCycle(wd, kZero, 0, t);  // arm (1/2)
        const Decision good = runCycle(wd, kGood, 0, t);  // recover -> clear
        const Decision f2   = runCycle(wd, kZero, 0, t);  // arm again (1/2), NOT reset
        ok &= expect(f1 == Decision::Arming && good == Decision::Idle
                         && f2 == Decision::Arming && wd.faultedCycles() == 1,
                     "a good cycle resets the zero-fault count (debounce)");
    }

    // 8. No reset storm — after WouldReset + noteResetPerformed, zero cycles in
    //    the grace window are suppressed; escalation resumes after grace.
    {
        Watchdog wd(Config{.confirmCycles = 2, .resetGraceMs = 8000});
        qint64 t = 0;
        runCycle(wd, kZero, 0, t);                          // arm
        const Decision r = runCycle(wd, kZero, 0, t);       // WouldReset
        wd.noteResetPerformed(t);                           // caller fired the reset
        const Decision inGrace = runCycle(wd, kZero, 0, t); // still zero, within grace
        t += 9000;                                          // let grace expire
        runCycle(wd, kZero, 0, t);                          // arm (1/2) again
        const Decision afterGrace = runCycle(wd, kZero, 0, t); // reset again (2/2)
        ok &= expect(r == Decision::WouldReset && inGrace == Decision::Idle
                         && afterGrace == Decision::WouldReset,
                     "post-reset grace suppresses a reset storm, then resumes");
    }

    // 9. Brief mid-cycle dip that recovers within the same TX is not a fault.
    {
        Watchdog wd(Config{.confirmCycles = 1});  // even the strictest K
        qint64 t = 0;
        wd.update({.transmitting = true, .fwdPowerW = kZero, .powerSampleAgeMs = 0, .nowMs = t++});
        wd.update({.transmitting = true, .fwdPowerW = kGood, .powerSampleAgeMs = 0, .nowMs = t++});
        const Decision fall = wd.update({.transmitting = false, .powerSampleAgeMs = 0, .nowMs = t++});
        ok &= expect(fall == Decision::Idle,
                     "a cycle that recovers to good power is healthy, not faulted");
    }

    // 10. reset() clears all accumulated state.
    {
        Watchdog wd(Config{.confirmCycles = 2});
        qint64 t = 0;
        runCycle(wd, kZero, 0, t);          // arm (faultedCycles == 1)
        wd.reset();
        ok &= expect(wd.faultedCycles() == 0 && !wd.transmitting()
                         && wd.lastCycleClass() == PowerClass::Unknown,
                     "reset() clears fault count, TX state, and last class");
    }

    return ok ? 0 : 1;
}
