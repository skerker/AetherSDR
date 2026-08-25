#include "SystemInfoCollector.h"

#include "ThreadName.h"

#include <QMetaType>
#include <QTimer>

namespace AetherSDR {

SystemInfoCollector::SystemInfoCollector(QObject* parent)
    : QObject(parent)
{
    // A queued connection copies the argument through the metatype system, so
    // the payload type has to be registered or the emit is silently dropped
    // with a runtime warning rather than failing to compile.
    qRegisterMetaType<QVector<AetherSDR::ThreadCpuSample>>("QVector<AetherSDR::ThreadCpuSample>");
}

void SystemInfoCollector::init()
{
    if (m_timer != nullptr) {
        return;  // already running; init() can arrive again after a restart
    }

    // The sampler is itself a thread in the table it produces. Naming it means
    // an operator can see the observer's own cost rather than wondering which
    // unnamed row it is.
    setCurrentThreadName("SystemInfoCollector");

    // Seed the baseline so the first published sample is a real interval rather
    // than every thread's whole lifetime divided by a few milliseconds.
    m_previous = SystemInfo::enumerateThreads();
    m_sinceLastSample.start();

    m_timer = new QTimer(this);
    m_timer->setInterval(kSampleIntervalMs);
    m_timer->setTimerType(Qt::CoarseTimer);  // a diagnostic, not a deadline
    connect(m_timer, &QTimer::timeout, this, &SystemInfoCollector::sampleOnce);
    m_timer->start();
}

void SystemInfoCollector::sampleOnce()
{
    const QVector<ThreadTimes> current = SystemInfo::enumerateThreads();
    if (current.isEmpty()) {
        return;  // enumeration failed; publishing an empty table would read as "no threads"
    }

    // Measured elapsed time, not the nominal interval: a late timer would
    // otherwise inflate every percentage by the amount the sampler itself was
    // delayed — precisely the case a busy machine produces, and precisely when
    // the numbers need to be trustworthy.
    const qint64 elapsedUsecs = m_sinceLastSample.nsecsElapsed() / 1000;
    m_sinceLastSample.restart();

    if (!m_previous.isEmpty() && elapsedUsecs > 0) {
        emit sampleReady(SystemInfo::cpuPercentBetween(
            m_previous, current, static_cast<quint64>(elapsedUsecs)));
    }
    m_previous = current;
}

}  // namespace AetherSDR
