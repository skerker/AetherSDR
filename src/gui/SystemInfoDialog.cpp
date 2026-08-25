#include "SystemInfoDialog.h"

#include "LogSyntaxHighlighter.h"
#include "core/LogManager.h"
#include "core/SystemInfoCollector.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

namespace AetherSDR {

namespace {

constexpr int kLogPollMs = 500;
constexpr qint64 kInitialTailBytes = 64 * 1024;
constexpr qsizetype kMaxStoredLines = 5000;

enum Column { ColName = 0, ColTid, ColCpu, ColTotal, ColumnCount };

}  // namespace

SystemInfoDialog::SystemInfoDialog(QWidget* parent)
    : PersistentDialog(QStringLiteral("System Info"),
                       QStringLiteral("SystemInfoDialogGeometry"), parent)
{
    auto* layout = new QVBoxLayout(bodyWidget());
    auto* tabs = new QTabWidget(bodyWidget());
    tabs->addTab(buildThreadsTab(), QStringLiteral("Threads"));
    tabs->addTab(buildLogsTab(), QStringLiteral("Logs"));
    layout->addWidget(tabs);
    resize(900, 600);
}

SystemInfoDialog::~SystemInfoDialog()
{
    stopSampling();
    closeLogTail();
}

// ── Threads ─────────────────────────────────────────────────────────────────

QWidget* SystemInfoDialog::buildThreadsTab()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);

    m_threadSummary = new QLabel(QStringLiteral("Sampling…"), page);
    layout->addWidget(m_threadSummary);

    m_threadTable = new QTableWidget(0, ColumnCount, page);
    m_threadTable->setHorizontalHeaderLabels(
        {QStringLiteral("Thread"), QStringLiteral("TID"),
         QStringLiteral("CPU %"), QStringLiteral("Total CPU (s)")});
    m_threadTable->horizontalHeaderItem(ColCpu)->setToolTip(
        QStringLiteral("Share of ONE core, 0-100. Not a share of the machine: a "
                       "single thread pinning one core reads 100 % here however "
                       "many cores are idle."));
    m_threadTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_threadTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_threadTable->setSortingEnabled(true);
    // The name is the variable-width column; the three numeric ones size to
    // their content. Stretching the last section instead left Total CPU
    // enormous and clipped the CPU % header, which is the column the table
    // exists to rank by.
    m_threadTable->horizontalHeader()->setStretchLastSection(false);
    m_threadTable->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    m_threadTable->horizontalHeader()->setSectionResizeMode(ColTid, QHeaderView::ResizeToContents);
    m_threadTable->horizontalHeader()->setSectionResizeMode(ColCpu, QHeaderView::ResizeToContents);
    m_threadTable->horizontalHeader()->setSectionResizeMode(ColTotal, QHeaderView::ResizeToContents);
    // The hot thread is the question being asked, so it starts at the top.
    m_threadTable->sortItems(ColCpu, Qt::DescendingOrder);
    layout->addWidget(m_threadTable, 1);

    return page;
}

void SystemInfoDialog::applySample(const QVector<ThreadCpuSample>& threads)
{
    if (m_threadTable == nullptr) {
        return;
    }

    // Preserve whatever column the operator sorted by; refilling would
    // otherwise snap the view back and make a moving table unreadable.
    const int sortColumn = m_threadTable->horizontalHeader()->sortIndicatorSection();
    const Qt::SortOrder sortOrder = m_threadTable->horizontalHeader()->sortIndicatorOrder();
    m_threadTable->setSortingEnabled(false);
    m_threadTable->setRowCount(threads.size());

    double busiest = 0.0;
    QString busiestName;
    for (int row = 0; row < threads.size(); ++row) {
        const ThreadCpuSample& sample = threads.at(row);

        // An unnamed row is a raw std::thread Qt never saw — say so rather than
        // leaving a blank cell that reads as a rendering fault.
        const QString name = sample.name.isEmpty()
            ? QStringLiteral("(unnamed)")
            : sample.name;
        auto* nameItem = new QTableWidgetItem(name);

        // setData rather than setText for the numeric columns: a text item
        // sorts lexicographically, which puts 9 % above 80 %.
        auto* tidItem = new QTableWidgetItem;
        tidItem->setData(Qt::DisplayRole, static_cast<qulonglong>(sample.tid));
        auto* cpuItem = new QTableWidgetItem;
        cpuItem->setData(Qt::DisplayRole, QString::number(sample.cpuPercentOfCore, 'f', 1).toDouble());
        auto* totalItem = new QTableWidgetItem;
        totalItem->setData(Qt::DisplayRole,
                           QString::number(static_cast<double>(sample.cpuUsecs) / 1e6, 'f', 1).toDouble());

        m_threadTable->setItem(row, ColName, nameItem);
        m_threadTable->setItem(row, ColTid, tidItem);
        m_threadTable->setItem(row, ColCpu, cpuItem);
        m_threadTable->setItem(row, ColTotal, totalItem);

        if (sample.cpuPercentOfCore > busiest) {
            busiest = sample.cpuPercentOfCore;
            busiestName = name;
        }
    }

    m_threadTable->setSortingEnabled(true);
    m_threadTable->sortItems(sortColumn, sortOrder);

    if (m_threadSummary != nullptr) {
        m_threadSummary->setText(
            QStringLiteral("%1 threads · busiest %2 at %3 % of one core")
                .arg(threads.size())
                .arg(busiestName.isEmpty() ? QStringLiteral("—") : busiestName)
                .arg(busiest, 0, 'f', 1));
    }
}

void SystemInfoDialog::startSampling()
{
    if (m_collectorThread != nullptr) {
        return;
    }
    m_collectorThread = new QThread(this);
    m_collectorThread->setObjectName(QStringLiteral("SystemInfoCollector"));
    m_collector = new SystemInfoCollector;   // no parent — moved to the thread
    m_collector->moveToThread(m_collectorThread);
    connect(m_collectorThread, &QThread::started, m_collector, &SystemInfoCollector::init);
    connect(m_collector, &SystemInfoCollector::sampleReady,
            this, &SystemInfoDialog::applySample);
    m_collectorThread->start();
}

void SystemInfoDialog::stopSampling()
{
    if (m_collectorThread == nullptr) {
        return;
    }
    m_collectorThread->quit();
    m_collectorThread->wait();
    // Deleted outright rather than via the usual finished→deleteLater: once
    // wait() returns, the worker's event loop is gone, so a deferred delete has
    // nothing left to run it. The thread is stopped here, so this is both safe
    // and deterministic.
    delete m_collector;
    m_collector = nullptr;
    delete m_collectorThread;
    m_collectorThread = nullptr;
}

// ── Logs ────────────────────────────────────────────────────────────────────

QWidget* SystemInfoDialog::buildLogsTab()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);

    m_filterRow = new QHBoxLayout;
    layout->addLayout(m_filterRow);

    // Follow the categories that are actually switched on, live. A fixed list
    // would go stale as categories are added, and — worse — would offer a
    // ticked box above an empty pane whenever the category behind it was not
    // being logged at all, which says "showing" while meaning "nothing is
    // being written".
    connect(&LogManager::instance(), &LogManager::categoryChanged,
            this, [this](const QString&, bool) {
                rebuildCategoryFilters();
                rebuildLogView();
            });
    rebuildCategoryFilters();

    m_logViewer = new QPlainTextEdit(page);
    m_logViewer->setReadOnly(true);
    m_logViewer->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_logViewer->setMaximumBlockCount(static_cast<int>(kMaxStoredLines));
    new LogSyntaxHighlighter(m_logViewer->document());
    layout->addWidget(m_logViewer, 1);

    return page;
}

void SystemInfoDialog::rebuildCategoryFilters()
{
    if (m_filterRow == nullptr) {
        return;
    }

    // Snapshot which categories we already had boxes for BEFORE clearing them:
    // it is the only way to tell "new category, show it" from "the operator
    // unticked this one, leave it unticked".
    QSet<QString> previouslyKnown;
    for (auto it = m_categoryBoxes.constBegin(); it != m_categoryBoxes.constEnd(); ++it) {
        previouslyKnown.insert(it.key());
    }

    while (QLayoutItem* item = m_filterRow->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_categoryBoxes.clear();

    const QList<LogManager::Category> categories = LogManager::instance().categories();
    QSet<QString> stillEnabled;
    m_filterRow->addWidget(new QLabel(QStringLiteral("Show:"), m_logViewer->parentWidget()));

    for (const LogManager::Category& category : categories) {
        if (!category.enabled) {
            continue;   // not being logged, so there is nothing to offer
        }
        stillEnabled.insert(category.id);

        auto* box = new QCheckBox(category.label, m_logViewer->parentWidget());
        box->setToolTip(QStringLiteral("%1 — %2").arg(category.id, category.description));
        // A category newly switched on starts visible; one the operator has
        // deliberately unticked here stays hidden across rebuilds.
        box->setChecked(previouslyKnown.contains(category.id)
                            ? m_enabledCategories.contains(category.id)
                            : true);
        if (box->isChecked()) {
            m_enabledCategories.insert(category.id);
        }
        const QString id = category.id;
        connect(box, &QCheckBox::toggled, this, [this, id](bool on) {
            if (on) {
                m_enabledCategories.insert(id);
            } else {
                m_enabledCategories.remove(id);
            }
            rebuildLogView();
        });
        m_categoryBoxes.insert(category.id, box);
        m_filterRow->addWidget(box);
    }

    if (m_categoryBoxes.isEmpty()) {
        auto* hint = new QLabel(
            QStringLiteral("No log categories are switched on — enable them in "
                           "Help \u2192 Support."),
            m_logViewer->parentWidget());
        m_filterRow->addWidget(hint);
    }

    // Drop view-filter state for categories that are no longer logged at all.
    m_enabledCategories.intersect(stillEnabled);
    m_filterRow->addStretch(1);
}

QString SystemInfoDialog::categoryFromLine(const QString& line)
{
    // "[time] LEVEL category: message" — the same shape the network dialog
    // parses, kept identical so the two tails agree about what a category is.
    static const QRegularExpression categoryRe(
        QStringLiteral("^\\[[^\\]]+\\]\\s+\\S+\\s+([^:]+):"));
    const QRegularExpressionMatch match = categoryRe.match(line);
    if (!match.hasMatch()) {
        return QStringLiteral("default");
    }
    const QString category = match.captured(1).trimmed();
    return category.isEmpty() ? QStringLiteral("default") : category;
}

void SystemInfoDialog::openLogTail()
{
    if (m_logFile.isOpen()) {
        return;
    }
    const QString path = LogManager::instance().logFilePath();
    if (path.isEmpty()) {
        return;
    }
    m_logFile.setFileName(path);
    if (!m_logFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    // Start from the tail, not the beginning: a long session's log is tens of
    // megabytes and none of it is the stall being investigated right now.
    const qint64 size = m_logFile.size();
    if (size > kInitialTailBytes) {
        m_logFile.seek(size - kInitialTailBytes);
        m_logFile.readLine();  // discard the partial line the seek landed in
    }
    pollLog();

    if (m_logTimer == nullptr) {
        m_logTimer = new QTimer(this);
        m_logTimer->setInterval(kLogPollMs);
        connect(m_logTimer, &QTimer::timeout, this, &SystemInfoDialog::pollLog);
    }
    m_logTimer->start();
}

void SystemInfoDialog::closeLogTail()
{
    if (m_logTimer != nullptr) {
        m_logTimer->stop();
    }
    if (m_logFile.isOpen()) {
        m_logFile.close();
    }
}

void SystemInfoDialog::pollLog()
{
    if (!m_logFile.isOpen()) {
        return;
    }
    while (!m_logFile.atEnd()) {
        const QString line = QString::fromUtf8(m_logFile.readLine()).trimmed();
        if (!line.isEmpty()) {
            appendLogLine(line);
        }
    }
}

void SystemInfoDialog::appendLogLine(const QString& line)
{
    const QString category = categoryFromLine(line);
    m_logLines.push_back({category, line});
    while (m_logLines.size() > kMaxStoredLines) {
        m_logLines.removeFirst();
    }
    if (m_logViewer == nullptr || !m_enabledCategories.contains(category)) {
        return;
    }
    m_logViewer->appendPlainText(line);
    m_logViewer->verticalScrollBar()->setValue(
        m_logViewer->verticalScrollBar()->maximum());
}

void SystemInfoDialog::rebuildLogView()
{
    if (m_logViewer == nullptr) {
        return;
    }
    // Re-filtering replays what was kept rather than re-reading the file, so
    // toggling a category cannot lose lines that have already rolled past.
    m_logViewer->clear();
    for (const auto& entry : m_logLines) {
        if (m_enabledCategories.contains(entry.first)) {
            m_logViewer->appendPlainText(entry.second);
        }
    }
    m_logViewer->verticalScrollBar()->setValue(
        m_logViewer->verticalScrollBar()->maximum());
}

// ── Visibility ──────────────────────────────────────────────────────────────

void SystemInfoDialog::showEvent(QShowEvent* event)
{
    PersistentDialog::showEvent(event);
    startSampling();
    openLogTail();
}

void SystemInfoDialog::hideEvent(QHideEvent* event)
{
    stopSampling();
    closeLogTail();
    PersistentDialog::hideEvent(event);
}

}  // namespace AetherSDR
