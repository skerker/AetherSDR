#include "DeepCwRxBackend.h"
#include "core/DeepCwCommitter.h"
#include "core/DeepCwEngine.h"
#include "core/LogManager.h"
#include "core/Resampler.h"
#include <QMutexLocker>
#include <QThread>

namespace AetherSDR {

DeepCwRxBackend::DeepCwRxBackend(QObject* parent) : CwRxBackend(parent) {}

DeepCwRxBackend::~DeepCwRxBackend()
{
    stop();
}

bool DeepCwRxBackend::loadModel(const QString& modelPath)
{
    if (isRunning()) { return false; }
    if (!m_engine) { m_engine = std::make_unique<DeepCwEngine>(); }
    const bool ok = m_engine->loadModel(modelPath.toStdString());
    m_loaded = ok;
    qCInfo(lcDsp) << "DeepCwRxBackend: model load" << (ok ? "ok" : "FAILED") << modelPath;
    return ok;
}

void DeepCwRxBackend::start()
{
    if (isRunning()) { return; }
    {
        QMutexLocker lock(&m_ringMutex);
        m_ring.clear();
    }
    m_resetRequested = false;
    m_running = true;
    m_worker = QThread::create([this]() { decodeLoop(); });
    m_worker->setObjectName("DeepCwRx");
    m_worker->start();
    qCDebug(lcDsp) << "DeepCwRxBackend: started";
}

void DeepCwRxBackend::stop()
{
    if (!m_worker) { return; }
    m_running = false;
    m_worker->wait();
    delete m_worker;
    m_worker = nullptr;
    QMutexLocker lock(&m_ringMutex);
    m_ring.clear();
}

void DeepCwRxBackend::reset()
{
    QMutexLocker lock(&m_ringMutex);
    m_ring.clear();
    m_resetRequested = true;
}

void DeepCwRxBackend::feedFixed24(const DecoderPcmBlock& block)
{
    if (!isRunning() || !block.current()) { return; }
    QMutexLocker lock(&m_ringMutex);
    if (block.discontinuity) {
        m_ring.clear();
        m_resetRequested = true;
    }
    m_ring.insert(m_ring.end(), block.samples.cbegin(), block.samples.cend());
    if (m_ring.size() > kRingCapacity) {
        m_ring.erase(m_ring.begin(),
                     m_ring.begin() + static_cast<std::ptrdiff_t>(m_ring.size() - kRingCapacity));
    }
}

// K5PTB's DeepCW worker loop (prototype CwDecoder::decodeLoopDeep), unchanged
// apart from its input: drain the 24 kHz mono ring, resample to the model's
// 3200 Hz with an anti-aliased r8brain SRC (a 7.5x decimation; a naive
// drop/linear resample would fold energy into the 400-1200 Hz analysis band),
// and hand it to DeepCwCommitter: a sliding window re-decoded every 2 s whose
// characters are shown only once they are holdSec behind the live edge.
// holdSec defaults to 5 s; AETHER_DEEPCW_HOLD_S overrides it (local bench knob).
void DeepCwRxBackend::decodeLoop()
{
    constexpr int kRate = DeepCwEngine::kModelSampleRate;  // 3200 Hz (post-resample)

    double holdSec = 5.0;
    bool holdOk = false;
    const double envHold = qEnvironmentVariable("AETHER_DEEPCW_HOLD_S").toDouble(&holdOk);
    if (holdOk && envHold >= 1.0 && envHold <= 14.0) { holdSec = envHold; }

    // Worker-local: neither the resampler nor the committer is thread-safe.
    auto resampler = std::make_unique<Resampler>(24000.0, static_cast<double>(kRate));
    auto committer = std::make_unique<DeepCwCommitter>(holdSec);

    qCDebug(lcDsp) << "DeepCwRxBackend: loop running, modelLoaded:" << m_loaded.load()
                   << "hold" << committer->holdSec() << "s window" << committer->windowSec() << "s";

    while (m_running) {
        if (m_resetRequested.exchange(false)) {
            resampler = std::make_unique<Resampler>(24000.0, static_cast<double>(kRate));
            committer = std::make_unique<DeepCwCommitter>(holdSec);
        }

        std::vector<float> in24k;
        {
            QMutexLocker lock(&m_ringMutex);
            in24k.swap(m_ring);
        }
        if (!in24k.empty() && m_loaded && m_engine) {
            const QByteArray out = resampler->process(in24k.data(), static_cast<int>(in24k.size()));
            const auto* r = reinterpret_cast<const float*>(out.constData());
            const auto m = static_cast<std::size_t>(out.size() / static_cast<int>(sizeof(float)));
            const DeepCwCommitter::Result res = committer->push(r, m, *m_engine);

            // Dominant-tone pitch (a CTC model has no speed estimate).
            if (res.decoded && res.pitchHz > 0.0f) {
                m_pitch = res.pitchHz;
                emit statsUpdated(res.pitchHz, 0.0f);
            }
            // Mean CTC confidence mapped to the panel's cost convention (lower = better).
            if (!res.text.empty()) {
                emit textDecoded(QString::fromStdString(res.text), 1.0f - res.meanConf);
            }
        }

        QThread::msleep(200);
    }

    qCDebug(lcDsp) << "DeepCwRxBackend: loop exiting";
}

} // namespace AetherSDR
