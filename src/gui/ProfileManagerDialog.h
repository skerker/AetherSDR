#pragma once

#include "PersistentDialog.h"

#include <QMap>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QCheckBox;
class QTabWidget;
class QTimer;

namespace AetherSDR {

class RadioModel;

class ProfileManagerDialog : public PersistentDialog {
    Q_OBJECT

public:
    explicit ProfileManagerDialog(RadioModel* model, QWidget* parent = nullptr);

private:
    QWidget* buildProfileTab(const QString& type, const QStringList& profiles,
                             const QString& active);
    QWidget* buildAutoSaveTab();
    void refreshTab(const QString& type);
    void onGlobalSaveResult(const QString& name, int code, const QString& body);

    RadioModel* m_model;
    QTabWidget* m_tabs;

    // Per-tab widgets (indexed by type: "global", "transmit", "mic")
    struct TabWidgets {
        QLineEdit*   nameEdit;
        QListWidget* list;
        QPushButton* loadBtn;
        QPushButton* saveBtn;
        QPushButton* deleteBtn;
    };
    QMap<QString, TabWidgets> m_tabWidgets;

    QCheckBox* m_autoSaveTx{nullptr};

    // Global-tab save feedback (#4362). TX/Mic don't need it — their
    // Create path already reports via the QMessageBox flows above.
    QLabel* m_globalSaveStatus{nullptr};
    QTimer* m_globalSaveStatusTimer{nullptr};
};

} // namespace AetherSDR
