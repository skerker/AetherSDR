#pragma once

#include "SystemInfo.h"

#include <QElapsedTimer>
#include <QObject>
#include <QVector>

class QTimer;

namespace AetherSDR {

// Samples per-thread CPU on a worker thread and publishes the result to the GUI
// (#2554).
//
// Shaped like the other workers in this codebase rather than as a self-owning
// thread: the object is parentless, moved onto a QThread by its owner, and
// init() is connected to QThread::started — the same wiring FlexBackend uses for
// PanadapterStream and RadioConnection. That keeps the lifetime decision at the
// call site, which matters while it is still open whether sampling should run
// only while the dialog is visible.
//
// Sampling off the GUI thread is the point: enumerating every thread is the
// kind of work that would otherwise be measured by the very metric it is
// gathering, and a stall in the collector would be indistinguishable from the
// stall an operator opened the dialog to investigate.
class SystemInfoCollector : public QObject {
    Q_OBJECT

public:
    // The cadence the issue asks for, matching the status bar's existing
    // CPU/Mem refresh so the two surfaces cannot disagree about "now".
    static constexpr int kSampleIntervalMs = 1500;

    explicit SystemInfoCollector(QObject* parent = nullptr);

public slots:
    // Connect to QThread::started. Creates the timer HERE rather than in the
    // constructor: a QTimer belongs to the thread that started it, and one
    // created on the GUI thread would fire there no matter which thread owns
    // this object.
    void init();

    // Stop and destroy the timer ON THE THREAD THAT CREATED IT. A QTimer belongs
    // to its owning thread; deleting the collector from the GUI thread after the
    // worker has exited destroys a timer whose thread is gone, which Qt reports
    // as "Timers cannot be stopped from another thread" and is undefined
    // behaviour. Invoke this with a blocking queued connection before quit().
    void shutdown();

signals:
    // Queued to the GUI thread by Qt, since emitter and receiver differ.
    void sampleReady(const QVector<AetherSDR::ThreadCpuSample>& threads);

private:
    void sampleOnce();

    QTimer* m_timer{nullptr};
    QVector<ThreadTimes> m_previous;   // last raw snapshot, for the delta
    QElapsedTimer m_sinceLastSample;
};

}  // namespace AetherSDR
