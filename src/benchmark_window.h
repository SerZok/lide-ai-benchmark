#pragma once

#include <QMainWindow>
#include <QProcess>
#include <QTemporaryDir>

class QCloseEvent;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

class BenchmarkWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit BenchmarkWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void browseConfig();
    void loadConfig(const QString &path);
    void browseSbcl();
    void browseOutput();
    void startBenchmark();
    void aggregateResults();
    void cancelProcess();
    void appendProcessOutput();
    void finishProcess(int exitCode, QProcess::ExitStatus exitStatus);
    void setRunning(bool running);

    QLineEdit *m_configPath;
    QLineEdit *m_endpoint;
    QLineEdit *m_models;
    QSpinBox *m_repetitions;
    QLineEdit *m_sbclPath;
    QLineEdit *m_apiKey;
    QLineEdit *m_outputPath;
    QPlainTextEdit *m_output;
    QPushButton *m_runButton;
    QPushButton *m_cancelButton;
    QPushButton *m_aggregateButton;
    QProcess *m_process = nullptr;
    QTemporaryDir m_temporaryDirectory;
};
