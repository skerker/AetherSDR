#pragma once
#include "CwRxModel.h"
#include <QMutex>
#include <QString>
#include <atomic>
#include <memory>
#include <vector>

class QThread;

namespace AetherSDR {

class DeepCwEngine;

// DeepCW (RFC #4817, K5PTB's prototype) behind the CwRxBackend interface.
// Consumes the converted mono 24 kHz feed (feedFixed24), resamples it to the
// model's 3200 Hz on its own worker, and commits text through DeepCwCommitter.
// Owned directly by MainWindow for the CW Neural applet, beside the panel's
// CwRxModel; not in CwRxModel's catalog. Inert until loadModel() succeeds.
class DeepCwRxBackend final : public CwRxBackend {
    Q_OBJECT
public:
    explicit DeepCwRxBackend(QObject* parent = nullptr);
    ~DeepCwRxBackend() override;

    // Owner thread, while stopped. False without HAVE_ONNX or on a bad model.
    bool loadModel(const QString& modelPath);
    bool modelLoaded() const { return m_loaded.load(); }

    void start() override;
    void stop() override;
    void reset() override;
    void feedFixed24(const DecoderPcmBlock& block) override;
    bool isRunning() const override { return m_running.load(); }
    float estimatedPitch() const override { return m_pitch.load(); }

private:
    void decodeLoop();

    std::unique_ptr<DeepCwEngine> m_engine;
    std::atomic<bool> m_loaded{false};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_resetRequested{false};
    std::atomic<float> m_pitch{0.0f};
    QThread* m_worker{nullptr};

    // Mono float32 @24 kHz handoff ring, capped at kRingCapacity samples.
    QMutex m_ringMutex;
    std::vector<float> m_ring;
    static constexpr std::size_t kRingCapacity = 24000 * 4;
};

} // namespace AetherSDR
