#include "TxDaxWatchdog.h"

#include <QDateTime>
#include <QLoggingCategory>

#include "RadioModel.h"

// Own category so the watchdog is silent unless explicitly enabled in logging
// rules; default warning-level keeps it quiet in normal runs.
Q_LOGGING_CATEGORY(lcTxDaxWatchdog, "aether.txdaxwatchdog")

namespace AetherSDR {

using DaxTxWatchdogPolicy::Decision;
using DaxTxWatchdogPolicy::Observation;

namespace {
const char* decisionName(Decision d)
{
    switch (d) {
    case Decision::Idle:             return "Idle";
    case Decision::Arming:           return "Arming";
    case Decision::WouldReset:       return "WouldReset";
    case Decision::LowPowerObserved: return "LowPowerObserved";
    case Decision::SuppressedStale:  return "SuppressedStale";
    }
    return "?";
}
} // namespace

TxDaxWatchdog::TxDaxWatchdog(RadioModel& radio, QObject* parent)
    : QObject(parent)
    , m_radio(radio)
{
    m_enabled = enabledByEnv();

    // TX/RX cycle edges (rising clears per-cycle accumulators; falling judges).
    connect(&m_radio, &RadioModel::radioTransmittingChanged,
            this, &TxDaxWatchdog::onTransmittingChanged);
    // Forward-power samples arrive here repeatedly during a transmit.
    connect(&m_radio.meterModel(), &MeterModel::txMetersChanged,
            this, &TxDaxWatchdog::onTxMeters);
    // Clear all state on a disconnect so a reconnect starts fresh.
    connect(&m_radio, &RadioModel::connectionStateChanged,
            this, &TxDaxWatchdog::onConnectionChanged);

    if (m_enabled) {
        qCInfo(lcTxDaxWatchdog).noquote()
            << "TX-DAX watchdog ENABLED (opt-in) — zero-power auto-reset armed on the DAX path;"
            << QStringLiteral("K=%1 zeroCeil=%2W lowCeil=%3W staleMs=%4 graceMs=%5 resetOnLow=%6")
                   .arg(m_cfg.confirmCycles)
                   .arg(m_cfg.zeroPowerCeilingW)
                   .arg(m_cfg.lowPowerCeilingW)
                   .arg(m_cfg.powerStaleMs)
                   .arg(m_cfg.resetGraceMs)
                   .arg(m_cfg.resetOnLowPower ? "true" : "false");
    }
}

bool TxDaxWatchdog::enabledByEnv()
{
    const QByteArray v = qgetenv("AETHER_TX_DAX_WATCHDOG").trimmed().toLower();
    return v == "1" || v == "true" || v == "on" || v == "yes";
}

void TxDaxWatchdog::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    m_policy.reset();
    m_lastLogged = Decision::Idle;
    qCInfo(lcTxDaxWatchdog) << "TX-DAX watchdog" << (on ? "enabled" : "disabled");
}

bool TxDaxWatchdog::armed() const
{
    // Zero-only DAX-stall recovery: only meaningful on a connected radio, on the
    // DAX/digital TX path (never SSB/voice). resetDaxTxStream() re-checks
    // connection + re-entrancy itself, but gating here avoids pointless churn.
    return m_enabled
        && m_radio.isConnected()
        && m_radio.transmitModel().daxOn();
}

void TxDaxWatchdog::onTransmittingChanged(bool transmitting)
{
    tick(transmitting);
}

void TxDaxWatchdog::onTxMeters(float /*fwdPower*/, float /*swr*/)
{
    // Only judge in-cycle samples while the radio is actually keyed; a stray
    // meter update in the RX gap must not masquerade as a TX sample. The
    // falling-edge judgment is driven by onTransmittingChanged(false).
    if (!m_radio.isRadioTransmitting())
        return;
    tick(true);
}

void TxDaxWatchdog::onConnectionChanged(bool connected)
{
    if (!connected) {
        m_policy.reset();
        m_lastLogged = Decision::Idle;
    }
}

void TxDaxWatchdog::tick(bool transmitting)
{
    if (!armed()) {
        // Disarmed (off, disconnected, or off the DAX path) — keep no stale
        // cycle state so re-arming starts clean.
        m_policy.reset();
        m_lastLogged = Decision::Idle;
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    MeterModel& meters = m_radio.meterModel();
    const qint64 updatedAt = meters.fwdPowerUpdatedAtMs();

    Observation o;
    o.transmitting     = transmitting;
    o.fwdPowerW        = static_cast<double>(meters.fwdPower());
    // Age from the same epoch clock MeterModel stamps with; if never updated,
    // force "stale" so the policy's guard suppresses (never a false fault).
    o.powerSampleAgeMs = (updatedAt > 0) ? (now - updatedAt) : (m_cfg.powerStaleMs + 1);
    o.nowMs            = now;

    const Decision decision = m_policy.update(o);

    // Log only on a change of decision class, so per-tick TX meter updates
    // don't flood the log. WouldReset/reset is always worth an INFO line.
    if (decision != m_lastLogged) {
        m_lastLogged = decision;
        if (decision == Decision::Arming || decision == Decision::LowPowerObserved
            || decision == Decision::SuppressedStale) {
            qCInfo(lcTxDaxWatchdog).noquote()
                << "state ->" << decisionName(decision)
                << QStringLiteral("(fwd=%1W age=%2ms faulted=%3)")
                       .arg(o.fwdPowerW, 0, 'f', 2)
                       .arg(o.powerSampleAgeMs)
                       .arg(m_policy.faultedCycles());
        }
    }

    if (decision == Decision::WouldReset) {
        // The policy only ever emits WouldReset on the TX->RX falling edge, so
        // this is already the RX gap. Re-check isRadioTransmitting() anyway as
        // defense-in-depth — the same F3 guard the bridge verb enforces — so a
        // reset can never zero the emission id mid-transmit even if some future
        // signal ordering surprised us.
        if (m_radio.isRadioTransmitting()) {
            qCWarning(lcTxDaxWatchdog)
                << "WouldReset while still transmitting — deferring (RX-gap-only guard)";
            return;
        }
        qCInfo(lcTxDaxWatchdog).noquote()
            << "confirmed zero-power DAX-TX stall over" << m_policy.faultedCycles()
            << "cycles — firing resetDaxTxStream() in the RX gap";
        m_radio.resetDaxTxStream();
        m_policy.noteResetPerformed(now);
        m_lastLogged = Decision::Idle;  // fresh stream — re-observe from scratch
    }
}

} // namespace AetherSDR
