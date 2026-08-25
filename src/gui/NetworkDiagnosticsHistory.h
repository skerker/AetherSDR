#pragma once

// Extracted verbatim from NetworkDiagnosticsDialog.h (#2554). The System Info
// dialog mirrors this history's shape (raw window plus compaction), and the
// sampling model is a data concern rather than a dialog concern. The sample
// struct comes along because the history stores a QVector of it.

#include "core/PanadapterStream.h"
#include "models/DigitalVoiceWaveformHistory.h"

#include <QObject>
#include <QTimer>
#include <QVector>

namespace AetherSDR {

class RadioModel;
class AudioEngine;

struct NetworkDiagnosticsSample {
    qint64 timestampMs{0};
    int    rttMs{0};
    int    audioGapMs{0};
    int    audioJitterMs{0};
    double rxKbps{0.0};
    double txKbps{0.0};
    double audioKbps{0.0};
    double fftKbps{0.0};
    double waterfallKbps{0.0};
    double meterKbps{0.0};
    double daxKbps{0.0};
    double packetLossPct{0.0};
    double audioLossPct{0.0};
    double fftLossPct{0.0};
    double waterfallLossPct{0.0};
    double meterLossPct{0.0};
    double daxLossPct{0.0};
    double audioBufferMs{0.0};
    double underrunsPerSecond{0.0};
    double audioFeedRateHz{0.0};
    double audioFeedDeficitMs{0.0};
    double audioLatePacketsPerSecond{0.0};
    qint64 audioLatePackets{0};
    qint64 audioPacketGaps{0};
    qint64 audioLastPacketAgeMs{0};
    quint16 audioPacketClassCode{0};
    int audioStreamCount{0};
    int  adaptiveFpsCap{0};      // 0 = throttle inactive
    bool digitalVoiceWaveformValid{false};
    bool digitalVoiceRxValid{false};
    bool digitalVoiceTxValid{false};
    int digitalVoiceWaveformObservationCount{0};
    int digitalVoiceTxObservationCount{0};
    double digitalVoiceRxSampleRateHz{0.0};
    double digitalVoiceVitaGapsPerSecond{0.0};
    double digitalVoiceSourceBlocksPerSecond{0.0};
    double digitalVoiceTurnaroundMeanUs{0.0};
    quint64 digitalVoiceTurnaroundMaxUs{0};
    quint32 digitalVoiceQueueMax{0};
    double digitalVoiceTxSampleRateHz{0.0};
    double digitalVoiceTxVitaGapsPerSecond{0.0};
    quint32 digitalVoiceTxNullFrames{0};
    quint32 digitalVoiceTxPcmClips{0};
    quint32 digitalVoiceTxPcmInvalid{0};
    quint32 digitalVoiceTxSendFailures{0};
    quint32 digitalVoiceTxQueueMax{0};
    quint32 digitalVoiceTxTailSamples{0};
    quint64 digitalVoiceTxTailUs{0};
};

class NetworkDiagnosticsHistory : public QObject {
public:
    struct ThrottleEvent {
        qint64  timestampMs{0};
        bool    active{false};
        int     fpsCap{0};
    };

    explicit NetworkDiagnosticsHistory(RadioModel* model, AudioEngine* audio, QObject* parent = nullptr);

    const QVector<NetworkDiagnosticsSample>& samples() const { return m_samples; }
    NetworkDiagnosticsSample latestSample() const;
    const QVector<ThrottleEvent>& throttleEvents() const { return m_throttleEvents; }
    int throttleSessionCount() const { return m_throttleSessionCount; }
    bool hasDigitalVoiceWaveformTelemetry() const { return m_hasDigitalVoiceWaveformTelemetry; }

private:
    void sampleNow();
    void pruneSamples(qint64 nowMs);

    RadioModel* m_model{nullptr};
    AudioEngine* m_audio{nullptr};
    QTimer m_sampleTimer;
    QVector<NetworkDiagnosticsSample> m_samples;
    qint64 m_lastRxBytes{0};
    qint64 m_lastTxBytes{0};
    qint64 m_lastSampleMs{0};
    quint64 m_lastAudioUnderrunCount{0};
    qint64 m_lastAudioLatePackets{0};
    qint64 m_lastCatBytes[PanadapterStream::CatCount]{};
    QVector<ThrottleEvent> m_throttleEvents;
    int    m_throttleSessionCount{0};
    int    m_currentFpsCap{0};  // tracks latest state for sampleNow()
    DigitalVoiceWaveformHistoryTracker m_digitalVoiceWaveformHistory;
    bool m_hasDigitalVoiceWaveformTelemetry{false};
};

} // namespace AetherSDR
