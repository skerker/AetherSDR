#pragma once

// DaxTxWatchdogPolicy — the pure decision core of the 2b DAX-TX watchdog.
//
// Detects the TX low-power/DAX-stall fault (radio keyed but forward power
// collapses to ~0 W) and decides WHEN a stream reset should fire, without any
// dependency on the radio, Qt signals, or MeterModel. The caller feeds one
// Observation per evaluation tick (radio keyed? latest fwd-power reading + its
// age? monotonic clock) and gets back a Decision; it is the caller's job to
// actually invoke RadioModel::resetDaxTxStream() when the policy says
// WouldReset — and only ever in the RX gap, which is the only place this policy
// emits WouldReset.
//
// THREE power states are distinguished, because at a fixed known RF-drive
// (slider) setting the fault presents two different ways and they are almost
// certainly different failure modes:
//   * Good  — forward power matches the expected drive (~50 W).
//   * Low   — non-zero but well below expected (~5-10 W). Degraded; the stream
//             is alive. May be real PA/drive droop, NOT a dead DAX stream.
//   * Zero  — ~0 W on the meter: the dead-stream DAX stall this reset targets.
// By default ONLY the Zero state escalates to a reset (Config::resetOnLowPower
// == false): resetting the DAX stream is the proven cure for the dead-stream
// case, but there's no evidence it fixes a genuine low-power droop — so Low is
// classified and surfaced (Decision::LowPowerObserved) but not acted on unless
// the operator opts in once evidence justifies it.
//
// Design invariants (pinned by tests/dax_tx_watchdog_policy_test.cpp):
//  * Zero-only reset — by default only true-zero cycles escalate; a low-but-
//                      nonzero cycle reports LowPowerObserved and breaks the
//                      zero streak (never silently treated as either good or a
//                      dead stream).
//  * RX-gap-only     — WouldReset is emitted only on the TX->RX falling edge,
//                      never mid-transmit, so a reset can never interrupt a
//                      live TX cycle.
//  * Confirm K cycles — a single anomalous cycle never triggers; K consecutive
//                      faulted TX cycles must be observed (debounce).
//  * Staleness guard — a silent/stale power lane yields SuppressedStale and is
//                      NEVER counted as a fault (no false FAULTED on missing
//                      telemetry).
//  * No reset storm  — after a reset the caller calls noteResetPerformed(); a
//                      grace window then suppresses re-escalation while the
//                      fresh stream settles.
//
// Header-only + std/qint64 only, matching the sibling *Policy classes
// (TransmitInhibitPolicy, SliceRecreatePolicy) so it links with just Qt6::Core.

#include <QtGlobal>  // qint64

namespace AetherSDR::DaxTxWatchdogPolicy {

// Classification of a single forward-power reading against the expected drive.
enum class PowerClass {
    Unknown,  // stale sample — cannot judge
    Zero,     // <= zeroPowerCeilingW — dead-stream stall
    Low,      // (zero, lowPowerCeilingW] — degraded but alive
    Good,     // > lowPowerCeilingW — matches expected drive
};

// Emitted after each Observation.
enum class Decision {
    Idle,              // healthy, or not transmitting — nothing to do
    Arming,            // consecutive ZERO cycles seen, but < K confirmations
    WouldReset,        // K consecutive zero cycles confirmed → reset now (RX gap)
    LowPowerObserved,  // cycle was low-but-nonzero — distinct state, no reset (default)
    SuppressedStale,   // power lane stale/silent → cannot judge; never a fault
};

struct Config {
    // Thresholds are absolute watts. The caller knows the RF-drive/slider
    // setting, so it may set these relative to expected (e.g. lowPowerCeilingW
    // = 0.4 * expectedW) before constructing the Watchdog.
    double zeroPowerCeilingW{0.5};   // <= this during TX == Zero (meter effectively 0)
    double lowPowerCeilingW{10.0};   // (zero, this] == Low (degraded, non-zero; ~0-10 W
                                     // when set for ~50 W). Caller may scale to the slider.
    bool   resetOnLowPower{false};   // default: ONLY the Zero state triggers a reset
    int    confirmCycles{2};         // K: consecutive faulted TX cycles before WouldReset
    qint64 powerStaleMs{3000};       // a sample older than this is stale (unjudgeable)
    qint64 resetGraceMs{8000};       // suppress re-escalation for this long after a reset
};

// Classify a single forward-power reading (assumes the sample is fresh; the
// staleness check lives in the Watchdog).
inline PowerClass classifyPower(double fwdPowerW, const Config& cfg)
{
    if (fwdPowerW <= cfg.zeroPowerCeilingW)
        return PowerClass::Zero;
    if (fwdPowerW <= cfg.lowPowerCeilingW)
        return PowerClass::Low;
    return PowerClass::Good;
}

// One evaluation tick.
struct Observation {
    bool   transmitting{false};    // radio keyed / TX window open (WSJT-X or radio state)
    double fwdPowerW{0.0};         // latest forward-power meter reading (W)
    qint64 powerSampleAgeMs{0};    // age of that reading (for the staleness guard)
    qint64 nowMs{0};              // monotonic clock (grace / windowing)
};

class Watchdog {
public:
    Watchdog() = default;
    explicit Watchdog(const Config& cfg) : m_cfg(cfg) {}

    // Feed one tick; returns the current decision. Pure — no side effects
    // beyond the internal state machine.
    Decision update(const Observation& o)
    {
        const bool txNow = o.transmitting;
        const bool fresh = o.powerSampleAgeMs <= m_cfg.powerStaleMs;

        // Rising edge: a new TX cycle begins — clear per-cycle accumulators.
        if (txNow && !m_wasTransmitting) {
            m_cycleSawFresh = false;
            m_cycleSawGood  = false;
            m_cycleSawZero  = false;
        }

        // During TX: accumulate this cycle's evidence. Never act here
        // (RX-gap-only) — return only a tentative in-cycle hint.
        if (txNow) {
            if (fresh) {
                m_cycleSawFresh = true;
                switch (classifyPower(o.fwdPowerW, m_cfg)) {
                case PowerClass::Good: m_cycleSawGood = true; break;
                case PowerClass::Zero: m_cycleSawZero = true; break;
                case PowerClass::Low:  break;  // captured, no accumulator flag
                case PowerClass::Unknown: break;
                }
            }
            m_wasTransmitting = true;

            if (m_cycleSawGood)  return Decision::Idle;
            if (!m_cycleSawFresh) return Decision::SuppressedStale;  // only stale so far
            if (m_cycleSawZero)  return Decision::Arming;            // zero tentative
            return Decision::LowPowerObserved;                       // low, no zero yet
        }

        // Falling edge: the TX cycle just ended — this IS the RX gap, so judge it.
        if (m_wasTransmitting) {
            m_wasTransmitting = false;
            const bool sawFresh = m_cycleSawFresh;
            const bool sawGood  = m_cycleSawGood;
            const bool sawZero  = m_cycleSawZero;
            m_cycleSawFresh = false;
            m_cycleSawGood  = false;
            m_cycleSawZero  = false;

            if (!sawFresh) {               // never a fresh sample → cannot judge
                m_lastCycleClass = PowerClass::Unknown;
                return Decision::SuppressedStale;
            }
            if (sawGood) {                 // cycle produced healthy power → clear faults
                m_lastCycleClass = PowerClass::Good;
                m_faultedCycles = 0;
                return Decision::Idle;
            }
            // No good power this cycle. A zero sample makes it a ZERO cycle
            // (the dead-stream stall we reset for); otherwise it's a LOW cycle.
            const bool zeroCycle = sawZero;
            m_lastCycleClass = zeroCycle ? PowerClass::Zero : PowerClass::Low;

            // A ZERO cycle always faults; a LOW cycle faults only if the operator
            // opted in (default: capture-only — we don't know a reset cures a real
            // low-power droop, only the dead-stream case). A LOW cycle also breaks
            // the consecutive-zero streak, so only *sustained* zero escalates.
            const bool faulting = zeroCycle || m_cfg.resetOnLowPower;
            if (!faulting) {
                m_faultedCycles = 0;
                return Decision::LowPowerObserved;  // distinct state, logged, no action
            }

            // Suppress escalation while the post-reset grace window is open so a
            // just-reset stream that hasn't settled can't trigger a reset storm.
            if (m_lastResetMs >= 0 && (o.nowMs - m_lastResetMs) < m_cfg.resetGraceMs) {
                m_faultedCycles = 0;
                return Decision::Idle;
            }
            ++m_faultedCycles;
            if (m_faultedCycles >= m_cfg.confirmCycles)
                return Decision::WouldReset;
            return Decision::Arming;
        }

        // Steady RX, not transmitting — nothing to judge.
        return Decision::Idle;
    }

    // The caller tells us a reset actually fired (opens the grace window and
    // clears the fault count so we re-observe the fresh stream from scratch).
    void noteResetPerformed(qint64 nowMs)
    {
        m_lastResetMs = nowMs;
        m_faultedCycles = 0;
    }

    int        faultedCycles() const { return m_faultedCycles; }
    bool       transmitting()  const { return m_wasTransmitting; }
    PowerClass lastCycleClass() const { return m_lastCycleClass; }

    // Full state clear (e.g. radio disconnect / session change).
    void reset()
    {
        m_wasTransmitting = false;
        m_cycleSawFresh = false;
        m_cycleSawGood  = false;
        m_cycleSawZero  = false;
        m_faultedCycles = 0;
        m_lastResetMs = -1;
        m_lastCycleClass = PowerClass::Unknown;
    }

private:
    Config m_cfg{};
    bool   m_wasTransmitting{false};
    bool   m_cycleSawFresh{false};  // any fresh sample this cycle?
    bool   m_cycleSawGood{false};   // any fresh Good sample this cycle?
    bool   m_cycleSawZero{false};   // any fresh Zero sample this cycle?
    int    m_faultedCycles{0};      // consecutive confirmed-ZERO cycles
    qint64 m_lastResetMs{-1};       // when noteResetPerformed() last fired
    PowerClass m_lastCycleClass{PowerClass::Unknown};  // last judged cycle (for logs)
};

} // namespace AetherSDR::DaxTxWatchdogPolicy
