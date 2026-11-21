#pragma once

#include <QMainWindow>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>

#include <QByteArray>
#include <QJsonObject>
#include <QVector>

class QCloseEvent;
class QComboBox;
class QStandardItemModel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QLabel;
class QTableWidget;

class BenchmarkWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit BenchmarkWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void refreshModels();

private:
    void browseConfig();
    void loadConfig(const QString &path);
    void browseSbcl();
    void browseOutput();
    void startBenchmark();
    void aggregateResults();
    void loadResults();
    void pollResults();
    void appendResultLine(const QByteArray &line);
    void showSelectedResult(int row, int column);
    void updateSummary();
    void cancelProcess();
    void appendProcessOutput();
    void finishProcess(int exitCode, QProcess::ExitStatus exitStatus);
    void setRunning(bool running);
    void updateModelsDisplayText();

    QLineEdit *m_configPath;
    QLineEdit *m_endpoint;
    QComboBox *m_models;
    QStandardItemModel *m_modelsModel = nullptr;
    QPushButton *m_refreshModels;
    QSpinBox *m_repetitions;
    QLineEdit *m_sbclPath;
    QLineEdit *m_apiKey;
    QLineEdit *m_outputPath;
    QPlainTextEdit *m_output;
    QPlainTextEdit *m_resultDetails;
    QTableWidget *m_results;
    QLabel *m_summary;
    QPushButton *m_runButton;
    QPushButton *m_cancelButton;
    QPushButton *m_aggregateButton;
    QPushButton *m_loadResultsButton;
    QProcess *m_process = nullptr;
    QTemporaryDir m_temporaryDirectory;
    QTimer m_resultsTimer;
    qint64 m_jsonlPosition = 0;
    QByteArray m_pendingJsonl;
    QVector<QJsonObject> m_records;
};
