#pragma once

// TxDaxWatchdog — the 2b live wiring around the pure DaxTxWatchdogPolicy.
//
// Drives the (already-unit-tested) DaxTxWatchdogPolicy state machine from live
// radio signals and, on a confirmed zero-power DAX-TX stall, auto-fires
// RadioModel::resetDaxTxStream() — the exact recovery proven manually on-air
// via the 2c `txstream reset` bridge verb (0 W -> ~33 W, 2026-07-09).
//
// Safety (matches AETHERSDR-TX-DAX-RESET-IMPL-PLAN.md §5, CLAUDE.md ethos):
//   * OFF by default — arm only via env AETHER_TX_DAX_WATCHDOG=1 (or setEnabled).
//     Auto-actions on the TX path are strictly opt-in.
//   * DAX-digital path only — armed only while TransmitModel::daxOn() (never an
//     SSB/voice QSO) and while the radio is connected.
//   * RX-gap only — the policy emits WouldReset solely on the TX->RX falling
//     edge, so a reset can never chop a live transmit.
//   * Zero-only — by default only true-zero cycles escalate; the low-power
//     sub-state is logged (LowPowerObserved) but not acted on. On-air 07-09
//     confirmed the reset is a no-op on low-power, so acting on it is pointless.
//   * Re-entrancy / not-connected are additionally guarded inside
//     resetDaxTxStream() itself (m_daxTxResetPending, isConnected()).
//
// Lives in src/models/ (depends on RadioModel/MeterModel/TransmitModel — all
// models); owned by MainWindow, parented for lifetime.

#include <QObject>

#include "DaxTxWatchdogPolicy.h"

namespace AetherSDR {

class RadioModel;

class TxDaxWatchdog : public QObject {
    Q_OBJECT
public:
    explicit TxDaxWatchdog(RadioModel& radio, QObject* parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);

    // True if AETHER_TX_DAX_WATCHDOG is set to a truthy value (1/true/on/yes).
    static bool enabledByEnv();

    // Expose the tunables so a future settings surface / test can adjust them.
    const DaxTxWatchdogPolicy::Config& config() const { return m_cfg; }
    void setConfig(const DaxTxWatchdogPolicy::Config& cfg) { m_cfg = cfg; }

private slots:
    void onTransmittingChanged(bool transmitting);
    void onTxMeters(float fwdPower, float swr);
    void onConnectionChanged(bool connected);

private:
    // Feed one Observation reflecting `transmitting` + the latest meter sample;
    // act on WouldReset. No-op (and clears policy state) while disarmed.
    void tick(bool transmitting);

    // All must hold to arm: enabled AND connected AND on the DAX/digital TX path.
    bool armed() const;

    RadioModel&                    m_radio;
    DaxTxWatchdogPolicy::Config    m_cfg{};
    DaxTxWatchdogPolicy::Watchdog  m_policy{m_cfg};
    bool                           m_enabled{false};
    // Last decision we logged, so per-tick meter updates during TX don't spam.
    DaxTxWatchdogPolicy::Decision  m_lastLogged{DaxTxWatchdogPolicy::Decision::Idle};
};

} // namespace AetherSDR
