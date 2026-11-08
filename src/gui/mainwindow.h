// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <DMainWindow>

#include <QElapsedTimer>
#include <QPointer>
#include <QProcess>

DWIDGET_BEGIN_NAMESPACE
class DFileChooserEdit;
class DListView;
class DLabel;
class DTipLabel;
class DSuggestButton;
class DWaterProgress;
DWIDGET_END_NAMESPACE

class QStackedWidget;
class QStandardItemModel;
class QPlainTextEdit;
class QTimer;
class QToolButton;
class QPushButton;

DWIDGET_USE_NAMESPACE

class MainWindow : public DMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private Q_SLOTS:
    void refreshDevices();
    void scheduleRefresh();
    void validateSelection();
    void startWriting();
    void readHelperOutput();
    void helperFinished(int exitCode, QProcess::ExitStatus status);

private:
    QWidget *createWelcomePage();
    QWidget *createSelectPage();
    QWidget *createProgressPage();
    QWidget *createResultPage();

    void setIsoFile(const QString &path);
    QString selectedDevice() const;
    qint64 isoSize() const;
    void handleLine(const QString &line);
    void appendLog(const QString &text);
    void showResult(bool success, const QString &message);
    bool confirmErase(const QString &device);
    bool isWorking() const;

    QStackedWidget *m_stack = nullptr;

    // Selección
    DFileChooserEdit *m_isoEdit = nullptr;
    DTipLabel *m_isoInfo = nullptr;
    DListView *m_deviceView = nullptr;
    QStandardItemModel *m_deviceModel = nullptr;
    DLabel *m_noDevices = nullptr;
    DTipLabel *m_selectHint = nullptr;
    DSuggestButton *m_startButton = nullptr;
    QTimer *m_refreshTimer = nullptr;

    // Progreso
    DWaterProgress *m_water = nullptr;
    DLabel *m_stepLabel = nullptr;
    DTipLabel *m_detailLabel = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QToolButton *m_logToggle = nullptr;
    QPushButton *m_cancelButton = nullptr;

    // Resultado
    DLabel *m_resultIcon = nullptr;
    DLabel *m_resultTitle = nullptr;
    DTipLabel *m_resultText = nullptr;

    QPointer<QProcess> m_helper;
    QString m_buffer;
    QString m_lastError;
    QString m_device;
    bool m_cancelRequested = false;
    bool m_done = false;
    QElapsedTimer m_copyTimer;
};
